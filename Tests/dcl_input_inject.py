#!/usr/bin/env python3
"""Host-side input injection for the DCL virtio-input and USB HID gate.

usage: dcl_input_inject.py --log LOG [--marker TEXT] [--usb-marker TEXT]
                           [--usb-device ID] [--tablet-port PATH]
                           [--msc-image PATH] [--uas-image PATH]
                           [--uas-lun1-image PATH] [--bot-image PATH]
                           [--bot-lun1-image PATH] [--timeout SEC]
                           [--monitor SOCK] [--qmp SOCK]

The guest only proves that the vendored input core attached its devices.
This helper supplies the other half: once both devices and the xHCI HID
poll thread are up it sends a key through QEMU's HMP monitor and an
absolute move plus click through QMP, then repeats them with device=gpu0
so QEMU routes the events to the usb-kbd/usb-tablet bound to that console.
A pass means the events travelled both PCI -> virtio ring -> input core
and xHCI -> HID report -> /dev/kybrd and /dev/mice. It then removes and
re-adds the usb-kbd and usb-tablet through QMP and repeats the injection,
which exercises the host controller's port-change and disconnect paths.
When --msc-image is given, it also attaches a usb-storage device, removes
it, and attaches it again to prove the mass-storage path survives a
re-plug and reuses its vdisk.  --uas-image runs the same check with a
SuperSpeed usb-uas device, which exercises the stream cleanup path;
--uas-lun1-image adds a second LUN to that device so the shared transport
entry and its per-LUN vdisk are rebuilt as well. --bot-image and
--bot-lun1-image repeat the replug check with a usb-bot controller: the
first attach warms up QEMU's SCSI unit attention, then two detach/attach
cycles prove REPORT LUNS discovery and per-LUN vdisk reuse.

Exit codes: 0 passed, 2 guest marker never appeared, 3 monitor/QMP failed,
4 events were sent but the guest did not decode them, 5 hotplug failed.
"""

import json
import socket
import sys
import time


def parse_args(argv):
    opts = {}
    i = 0
    while i < len(argv):
        key = argv[i]
        if key.startswith("--") and i + 1 < len(argv):
            # Option names are spelled with dashes on the command line and
            # looked up with underscores, matching the rest of the script.
            opts[key[2:].replace("-", "_")] = argv[i + 1]
            i += 2
        else:
            raise SystemExit("unexpected argument %r" % (key,))
    return opts


def wait_for_text(path, needles, timeout):
    """True once every needle appears in path, polling until timeout."""
    end = time.time() + timeout
    while time.time() < end:
        try:
            with open(path, "r", errors="replace") as fh:
                text = fh.read()
            if all(needle in text for needle in needles):
                return True
        except OSError:
            pass
        time.sleep(0.4)
    return False


def log_count(path, needle):
    try:
        with open(path, "r", errors="replace") as fh:
            return fh.read().count(needle)
    except OSError:
        return 0


def wait_for_count(path, needle, minimum, timeout):
    """True once needle occurs at least minimum times in path."""
    end = time.time() + timeout
    while time.time() < end:
        if log_count(path, needle) >= minimum:
            return True
        time.sleep(0.2)
    return False


def wait_for_qom_gone(qmp, name, timeout=10):
    """True once name leaves QEMU's /machine/peripheral container.

    A deleted SCSI LUN releases its block backend only when QEMU gets
    around to unrealizing the qdev, which can happen after the guest has
    already reported the port disconnect.  Reusing the same backend node
    or port before then fails with a permission conflict or "port in use",
    so callers wait for the object itself.
    """
    end = time.time() + timeout
    while time.time() < end:
        reply = qmp.command("qom-list", {"path": "/machine/peripheral"})
        if reply and "error" not in reply and all(
                entry.get("name") != name
                for entry in reply.get("return", [])):
            return True
        time.sleep(0.2)
    return False


def connect_unix(path, timeout):
    end = time.time() + timeout
    while True:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(2.0)
        try:
            sock.connect(path)
            return sock
        except OSError:
            sock.close()
            if time.time() > end:
                return None
            time.sleep(0.3)


def hmp_command(path, command, timeout):
    """One HMP command over a fresh connection.  True when it was sent."""
    sock = connect_unix(path, timeout)
    if not sock:
        return False

    buf = b""

    def until_prompt():
        nonlocal buf
        end = time.time() + timeout
        while b"(qemu)" not in buf:
            if time.time() > end:
                return False
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                return False
            buf += chunk
        return True

    try:
        if not until_prompt():
            return False
        buf = b""
        sock.sendall((command + "\n").encode())
        return until_prompt()
    finally:
        sock.close()


class Qmp:
    """The small part of the QMP wire protocol input-send-event needs."""

    def __init__(self, path, timeout):
        self.sock = connect_unix(path, timeout)
        self.buf = b""
        self.timeout = timeout
        if self.sock:
            self._line()  # greeting
            self.command("qmp_capabilities")

    def _line(self):
        end = time.time() + self.timeout
        while b"\n" not in self.buf:
            if time.time() > end:
                return None
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                return None
            self.buf += chunk
        line, _, self.buf = self.buf.partition(b"\n")
        return line

    def command(self, name, args=None):
        request = {"execute": name}
        if args is not None:
            request["arguments"] = args
        self.sock.sendall((json.dumps(request) + "\n").encode())

        end = time.time() + self.timeout
        while time.time() < end:
            line = self._line()
            if line is None:
                return None
            try:
                reply = json.loads(line)
            except ValueError:
                continue
            if reply.get("event"):
                continue
            return reply
        return None


def main():
    args = parse_args(sys.argv[1:])
    log = args.get("log")
    if not log:
        raise SystemExit("--log is required")

    marker = args.get("marker",
                      "[modtest]: virtio_input 2 device(s) registered")
    # QEMU only gives usb-kbd/usb-tablet input events once their qdev
    # display= binding names a console; the build script binds both to gpu0.
    # Its poll thread is the guest-side half, so wait for it too before
    # injecting the targeted events below.
    usb_marker = args.get("usb_marker", "[xhci-hid]: poll thread running")
    usb_device = args.get("usb_device", "gpu0")
    # When the boot tablet hangs off an external hub, put the replacement
    # back on the same hub port so the hub's connect/reset path is covered.
    tablet_port = args.get("tablet_port", "")
    # The image is not attached at boot; the helper creates a backend for it
    # through QMP so a removed device can be added again with the same node.
    msc_image = args.get("msc_image", "")
    uas_image = args.get("uas_image", "")
    uas_lun1_image = args.get("uas_lun1_image", "")
    bot_image = args.get("bot_image", "")
    bot_lun1_image = args.get("bot_lun1_image", "")
    timeout = float(args.get("timeout", "240"))
    monitor = args.get("monitor", "/tmp/xeneva-mon.sock")
    qmp_path = args.get("qmp", "/tmp/xeneva-qmp.sock")

    if not wait_for_text(log, [marker, usb_marker], timeout):
        print("dcl_input_inject: guest marker never appeared")
        return 2

    if not hmp_command(monitor, "sendkey a", 10):
        print("dcl_input_inject: HMP sendkey failed")
        return 3

    qmp = Qmp(qmp_path, 10)
    if not qmp.sock:
        print("dcl_input_inject: QMP socket unavailable")
        return 3
    try:
        # Absolute coordinates exercise the tablet's ABS path; the button
        # down/up pair exercises the native button bit mapping.  QEMU adds
        # the SYN_REPORT around each command, which is what makes the DCL
        # bridge emit one mouse packet at a time.
        move = [{"type": "abs", "data": {"axis": "x", "value": 12345}},
                {"type": "abs", "data": {"axis": "y", "value": 6789}}]
        btn_down = [{"type": "btn",
                     "data": {"button": "left", "down": True}}]
        btn_up = [{"type": "btn",
                   "data": {"button": "left", "down": False}}]
        key_down = [{"type": "key",
                     "data": {"down": True,
                              "key": {"type": "qcode", "data": "a"}}}]
        key_up = [{"type": "key",
                   "data": {"down": False,
                            "key": {"type": "qcode", "data": "a"}}}]
        caps_down = [{"type": "key",
                      "data": {"down": True,
                               "key": {"type": "qcode", "data": "caps_lock"}}}]
        caps_up = [{"type": "key",
                    "data": {"down": False,
                             "key": {"type": "qcode", "data": "caps_lock"}}}]
        commands = (
            # Unbound handlers get the broadcast: the vendored virtio pair.
            {"events": move},
            {"events": btn_down},
            {"events": btn_up},
            # device= selects the USB HID handlers bound to that console,
            # ahead of the unbound virtio fallback.
            {"device": usb_device, "events": key_down},
            {"device": usb_device, "events": key_up},
            {"device": usb_device, "events": move},
            {"device": usb_device, "events": btn_down},
            {"device": usb_device, "events": btn_up},
            # A lock key makes the guest send SET_REPORT back to the
            # keyboard, so this also covers the HID output path.
            {"device": usb_device, "events": caps_down},
            {"device": usb_device, "events": caps_up},
        )
        for command in commands:
            reply = qmp.command("input-send-event", command)
            if not reply or "error" in reply:
                print("dcl_input_inject: QMP input-send-event failed: %r"
                      % (reply,))
                return 3
    finally:
        qmp.sock.close()

    # The guest polls the event queue from its timer tick.
    expected = ["[virtio-input]: key code 30 value 1",
                "[virtio-input]: pointer",
                "[xhci-hid]: key 1E down",
                "[xhci-hid]: key 1E up",
                "[xhci-hid]: leds 2",
                "[xhci-hid]: pointer"]
    if not wait_for_text(log, expected, 10):
        print("dcl_input_inject: guest did not decode both event classes")
        return 4

    # Hotplug. The boot keyboard and tablet carry fixed qdev ids, so QMP can
    # remove each one, wait for the guest's port thread to release the slot,
    # then install a replacement on the xHCI bus. A second key/pointer
    # decode proves the new device was enumerated and bound to gpu0.
    qmp = Qmp(qmp_path, 10)
    if not qmp.sock:
        print("dcl_input_inject: QMP socket unavailable for hotplug")
        return 3

    def qmp_ok(name, args):
        reply = qmp.command(name, args)
        if not reply or "error" in reply:
            print("dcl_input_inject: QMP %s failed: %r" % (name, reply))
            return False
        return True

    try:
        # usb-audio is full speed with a 64-byte control pipe, unlike every
        # boot device's 8-byte pipe, so a transient attach exercises the
        # EP0 max-packet update.  No class driver claims it, and it is
        # removed again before the replug sequence so the hub ports that
        # sequence needs are not taken.
        ep0_updates = log_count(log, "[xhci]: ep0 max packet 64")
        unsupported = log_count(log, "has no supported interface")
        if not qmp_ok("device_add", {"driver": "usb-audio",
                                     "id": "usb-audio0",
                                     "bus": "xhci0.0"}):
            return 5
        if not wait_for_count(log, "[xhci]: ep0 max packet 64",
                              ep0_updates + 1, 10):
            print("dcl_input_inject: guest did not update the ep0 packet size")
            return 5
        if not wait_for_count(log, "has no supported interface",
                              unsupported + 1, 10):
            print("dcl_input_inject: guest did not finish usb-audio "
                  "enumeration")
            return 5
        if not qmp_ok("device_del", {"id": "usb-audio0"}):
            return 5

        disconnects = log_count(log, "[xhci-hotplug]: disconnect on port")
        if not qmp_ok("device_del", {"id": "usb-kbd0"}):
            return 5
        if not wait_for_count(log, "[xhci-hotplug]: disconnect on port",
                              disconnects + 1, 15):
            print("dcl_input_inject: guest did not release the removed keyboard")
            return 5

        enumerations = log_count(log, "[xhci-hotplug]: enumerated port")
        if not qmp_ok("device_add", {"driver": "usb-kbd", "id": "usb-kbd1",
                                     "bus": "xhci0.0", "display": usb_device}):
            return 5
        if not wait_for_count(log, "[xhci-hotplug]: enumerated port",
                              enumerations + 1, 15):
            print("dcl_input_inject: guest did not enumerate the new keyboard")
            return 5

        b_down = [{"type": "key",
                   "data": {"down": True,
                            "key": {"type": "qcode", "data": "b"}}}]
        b_up = [{"type": "key",
                 "data": {"down": False,
                          "key": {"type": "qcode", "data": "b"}}}]
        for command in ({"device": usb_device, "events": b_down},
                        {"device": usb_device, "events": b_up}):
            if not qmp_ok("input-send-event", command):
                return 5
        if not wait_for_text(log, ["[xhci-hid]: key 30 down",
                                   "[xhci-hid]: key 30 up"], 10):
            print("dcl_input_inject: hotplugged keyboard did not decode input")
            return 5

        disconnects = log_count(log, "[xhci-hotplug]: disconnect on port")
        if not qmp_ok("device_del", {"id": "usb-tablet0"}):
            return 5
        if not wait_for_count(log, "[xhci-hotplug]: disconnect on port",
                              disconnects + 1, 15):
            print("dcl_input_inject: guest did not release the removed tablet")
            return 5

        enumerations = log_count(log, "[xhci-hotplug]: enumerated port")
        tablet_add = {"driver": "usb-tablet", "id": "usb-tablet1",
                      "bus": "xhci0.0", "display": usb_device}
        if tablet_port:
            tablet_add["port"] = tablet_port
        if not qmp_ok("device_add", tablet_add):
            return 5
        if not wait_for_count(log, "[xhci-hotplug]: enumerated port",
                              enumerations + 1, 15):
            print("dcl_input_inject: guest did not enumerate the new tablet")
            return 5

        pointers = log_count(log, "[xhci-hid]: pointer")
        for command in ({"device": usb_device, "events": move},
                        {"device": usb_device, "events": btn_down},
                        {"device": usb_device, "events": btn_up}):
            if not qmp_ok("input-send-event", command):
                return 5
        if not wait_for_count(log, "[xhci-hid]: pointer", pointers + 1, 10):
            print("dcl_input_inject: hotplugged tablet did not decode input")
            return 5

        def add_raw_backend(prefix, path):
            """Attach a file-backed raw node so one drive survives both plugs."""
            return (qmp_ok("blockdev-add", {"driver": "file",
                                            "node-name": prefix + "-file0",
                                            "filename": path}) and
                    qmp_ok("blockdev-add", {"driver": "raw",
                                            "node-name": prefix + "-node0",
                                            "file": prefix + "-file0"}))

        def set_attached(prefix, value):
            """Set a hotplugged controller's QOM attached property.

            QEMU leaves a usb-uas or usb-bot added through QMP unattached
            (their models clear auto_attach for hotplugged instances), so
            this is how the helper puts one on the bus.  The bot check also
            uses it to detach and re-attach the same controller, with its
            SCSI children intact, between probe attempts.
            """
            return qmp_ok("qom-set", {
                "path": "/machine/peripheral/" + prefix,
                "property": "attached", "value": value})

        if bot_image:
            # usb-bot exposes Bulk-Only Transport through a SCSI controller
            # with child LUNs.  QEMU raises a unit attention on a freshly
            # realized scsi-hd, and its BOT target answers the follow-up
            # REQUEST SENSE with LOGICAL UNIT NOT SUPPORTED for a non-zero
            # LUN, so LUN 1 cannot probe on the first attach.  The warm-up
            # attach below takes that hit; the second attach publishes both
            # LUNs and the third must reuse both vdisks.
            #
            # The boot topology leaves two free full-speed hub ports and
            # the keyboard replug above claims one of them, so the bot uses
            # the port the msc replug below needs and releases it again.
            nlun = 2 if bot_lun1_image else 1
            vdisks = log_count(log, "[xhci-msc]: vdisk USB")
            reuses = log_count(log, "[xhci-msc]: vdisk reuse slot")
            if not add_raw_backend("bot", bot_image):
                return 5
            if bot_lun1_image and not add_raw_backend("botlun",
                                                       bot_lun1_image):
                return 5
            bot = {"driver": "usb-bot", "id": "bot-replug0",
                   "bus": "xhci0.0", "port": "4.7"}
            luns = [{"driver": "scsi-hd", "drive": "bot-node0",
                     "id": "bot-lun0", "bus": "bot-replug0.0", "lun": 0,
                     "product": "BOTRPLUG0"}]
            if bot_lun1_image:
                luns.append({"driver": "scsi-hd", "drive": "botlun-node0",
                             "id": "bot-lun1", "bus": "bot-replug0.0",
                             "lun": 1, "product": "BOTRPLUG1"})
            if not qmp_ok("device_add", bot):
                return 5
            for lun in luns:
                if not qmp_ok("device_add", lun):
                    return 5

            def bot_attached(value):
                return set_attached("bot-replug0", value)

            def bot_detach():
                disconnects = log_count(
                    log, "[xhci-hotplug]: disconnect on port")
                if not bot_attached(False):
                    return False
                if not wait_for_count(
                        log, "[xhci-hotplug]: disconnect on port",
                        disconnects + 1, 15):
                    print("dcl_input_inject: guest did not release the bot "
                          "disks")
                    return False
                return True

            if not bot_attached(True):
                return 5
            # Warm-up: LUN 0 is claimed while LUN 1 loses READ CAPACITY to
            # the stale unit attention.  The failed probe must leave LUN 0
            # and the shared transport up.
            if not wait_for_count(log, "[xhci-msc]: vdisk USB",
                                  vdisks + 1, 25):
                print("dcl_input_inject: warm-up bot did not claim lun 0")
                return 5
            if bot_lun1_image and not wait_for_text(
                    log, ["[xhci-msc]: lun 1 probe failed"], 15):
                print("dcl_input_inject: warm-up bot did not report the "
                      "expected lun 1 failure")
                return 5

            # Two clean re-plugs: the first publishes both LUNs, the second
            # reuses both of their vdisks.
            for vdisk_target, reuse_target in (
                    (vdisks + 1 + nlun, reuses + 1),
                    (vdisks + 1 + 2 * nlun, reuses + nlun + 1)):
                if not bot_detach() or not bot_attached(True):
                    return 5
                if not wait_for_count(log, "[xhci-msc]: vdisk USB",
                                      vdisk_target, 25):
                    print("dcl_input_inject: guest did not reclaim the bot "
                          "disks")
                    return 5
                if not wait_for_count(log, "[xhci-msc]: vdisk reuse slot",
                                      reuse_target, 10):
                    print("dcl_input_inject: reattached bot disks did not "
                          "reuse their vdisks")
                    return 5
            if bot_lun1_image and not wait_for_text(
                    log, ["[xhci-msc]: vdisk USB BOTRPLUG1 L1"], 10):
                print("dcl_input_inject: reattached bot did not publish lun 1")
                return 5

            # Unplug the controller: QOM detach keeps its port reserved, and
            # the msc replug below needs 4.7.  The guest disconnect proves
            # the xHCI release; the QOM poll waits for QEMU to return the
            # port itself.
            disconnects = log_count(log,
                                    "[xhci-hotplug]: disconnect on port")
            for lun in reversed(luns):
                if not qmp_ok("device_del", {"id": lun["id"]}):
                    return 5
            if not qmp_ok("device_del", {"id": "bot-replug0"}):
                return 5
            if not wait_for_count(log, "[xhci-hotplug]: disconnect on port",
                                  disconnects + 1, 15):
                print("dcl_input_inject: guest did not release the bot disks")
                return 5
            for lun in luns:
                if not wait_for_qom_gone(qmp, lun["id"]):
                    print("dcl_input_inject: old bot lun did not unplug")
                    return 5
            if not wait_for_qom_gone(qmp, "bot-replug0"):
                print("dcl_input_inject: bot controller did not unplug")
                return 5

        if msc_image:
            # A usb-storage attached with -drive disappears when QEMU removes
            # it, so build a blockdev backend here and keep it across both
            # plug events.  The second attach must reuse the vdisk the first
            # one registered instead of adding another one.
            vdisks = log_count(log, "[xhci-msc]: vdisk USB")
            reuses = log_count(log, "[xhci-msc]: vdisk reuse slot")
            if not add_raw_backend("msc", msc_image):
                return 5

            # Pin the replug disk to the hub port the bot check above just
            # released.  Without a port, QEMU's free list can hand it a
            # root port that the UAS replug below needs, because that root
            # was released by the keyboard replug.
            storage = {"driver": "usb-storage", "drive": "msc-node0",
                       "id": "msc-replug0", "bus": "xhci0.0",
                       "port": "4.7"}
            if not qmp_ok("device_add", storage):
                return 5
            if not wait_for_count(log, "[xhci-msc]: vdisk USB",
                                  vdisks + 1, 20):
                print("dcl_input_inject: guest did not claim the added disk")
                return 5

            disconnects = log_count(log,
                                    "[xhci-hotplug]: disconnect on port")
            if not qmp_ok("device_del", {"id": "msc-replug0"}):
                return 5
            if not wait_for_count(log, "[xhci-hotplug]: disconnect on port",
                                  disconnects + 1, 15):
                print("dcl_input_inject: guest did not release the disk")
                return 5
            if not wait_for_qom_gone(qmp, "msc-replug0"):
                print("dcl_input_inject: msc controller did not unplug")
                return 5

            storage["id"] = "msc-replug1"
            if not qmp_ok("device_add", storage):
                return 5
            if not wait_for_count(log, "[xhci-msc]: vdisk USB",
                                  vdisks + 2, 20):
                print("dcl_input_inject: guest did not reclaim the disk")
                return 5
            if not wait_for_count(log, "[xhci-msc]: vdisk reuse slot",
                                  reuses + 1, 5):
                print("dcl_input_inject: reattached disk did not reuse its "
                      "vdisk")
                return 5

        if uas_image:
            # usb-uas is a SCSI controller with child LUNs, so remove the
            # LUNs before the controller.  The replacement lands on the same
            # xHCI slot and must reuse the disk entries that the first one
            # registered, streams and all.  A second LUN also proves its
            # shared transport entry is rebuilt on the next claim.  It uses
            # root port 1 because a SuperSpeed device cannot sit behind the
            # full-speed hub, and the keyboard replug above freed that path.
            nlun = 2 if uas_lun1_image else 1
            vdisks = log_count(log, "[xhci-uas]: vdisk USB")
            reuses = log_count(log, "[xhci-uas]: vdisk reuse slot")
            streams = log_count(log, "[xhci-uas]: streams enabled")
            if not add_raw_backend("uas", uas_image):
                return 5
            if uas_lun1_image and not add_raw_backend("uaslun",
                                                       uas_lun1_image):
                return 5
            uas = {"driver": "usb-uas", "id": "uas-replug0",
                   "bus": "xhci0.0", "port": "1"}
            luns = [{"driver": "scsi-hd", "drive": "uas-node0",
                     "id": "uas-lun0", "bus": "uas-replug0.0", "lun": 0}]
            if uas_lun1_image:
                luns.append({"driver": "scsi-hd", "drive": "uaslun-node0",
                             "id": "uas-lun1", "bus": "uas-replug0.0",
                             "lun": 1})
            if not qmp_ok("device_add", uas):
                return 5
            for lun in luns:
                if not qmp_ok("device_add", lun):
                    return 5
            if not set_attached("uas-replug0", True):
                return 5
            if not wait_for_count(log, "[xhci-uas]: vdisk USB",
                                  vdisks + nlun, 20):
                print("dcl_input_inject: guest did not claim the added uas "
                      "disk")
                return 5
            if not wait_for_count(log, "[xhci-uas]: streams enabled",
                                  streams + 1, 5):
                print("dcl_input_inject: hotplugged uas did not use streams")
                return 5

            disconnects = log_count(log,
                                    "[xhci-hotplug]: disconnect on port")
            for lun in reversed(luns):
                if not qmp_ok("device_del", {"id": lun["id"]}):
                    return 5
            if not qmp_ok("device_del", {"id": "uas-replug0"}):
                return 5
            if not wait_for_count(log, "[xhci-hotplug]: disconnect on port",
                                  disconnects + 1, 15):
                print("dcl_input_inject: guest did not release the uas disk")
                return 5
            for lun in luns:
                if not wait_for_qom_gone(qmp, lun["id"]):
                    print("dcl_input_inject: old uas lun did not unplug")
                    return 5
            if not wait_for_qom_gone(qmp, "uas-replug0"):
                print("dcl_input_inject: old uas controller did not unplug")
                return 5

            uas["id"] = "uas-replug1"
            for i, lun in enumerate(luns):
                lun["id"] = "uas-lun%d-replug" % i
                lun["bus"] = "uas-replug1.0"
            if not qmp_ok("device_add", uas):
                return 5
            for lun in luns:
                if not qmp_ok("device_add", lun):
                    return 5
            if not set_attached("uas-replug1", True):
                return 5
            if not wait_for_count(log, "[xhci-uas]: vdisk USB",
                                  vdisks + 2 * nlun, 20):
                print("dcl_input_inject: guest did not reclaim the uas disk")
                return 5
            if not wait_for_count(log, "[xhci-uas]: vdisk reuse slot",
                                  reuses + nlun, 5):
                print("dcl_input_inject: reattached uas disk did not reuse "
                      "its vdisk")
                return 5
    finally:
        qmp.sock.close()

    print("dcl_input_inject: keyboard and tablet events reached both bridges, "
          "including hotplug")
    if msc_image:
        print("dcl_input_inject: mass-storage replug reused its vdisk")
    if uas_image:
        print("dcl_input_inject: uas replug reused its vdisk")
    if bot_image:
        print("dcl_input_inject: bot multi-lun replug reused its vdisks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
