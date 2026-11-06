import subprocess
import sys
import time
import os
import ctypes

#physical location of VHD file on the host system. Update this path if your VHD is located elsewhere.
VHD_PATH = r"C:\XenevaOS\Xeneva.vhd"

#QEMU command that will be run
QEMU_COMMAND = [
    "qemu-system-aarch64",
    "-machine", "virt,gic-version=2,acpi=off",
    "-cpu", "cortex-a72",
    "-m", "1024M",
    "-bios", "edk2-aarch64-code.fd",
    "-drive", f"file={VHD_PATH},format=raw,if=none,id=hd0",
    "-device", "virtio-blk-pci,drive=hd0,disable-legacy=on",
    "-drive", "file=C:\\XenevaOS\\ext2_disk.img,format=raw,if=none,id=hd1",
    "-device", "virtio-blk-pci,drive=hd1,disable-legacy=on",
    "-device", "ramfb",
    "-device", "virtio-keyboard-pci",
    "-device", "virtio-tablet-pci",
    "-display", "gtk",
    "-device", "usb-ehci",
    "-device", "usb-kbd",
    "-serial", "stdio",
    "-d", "guest_errors,unimp,trace:virtio_gpu*"
]

def is_admin():
    try:
        return ctypes.windll.shell32.IsUserAnAdmin()
    except:
        return False

def run_diskpart_script(commands):
    temp_script = os.path.join(os.environ["TEMP"], "vhd_diskpart.txt")
    try:
        with open(temp_script, "w") as f:
            f.write("\n".join(commands))
        
        result = subprocess.run(["diskpart", "/s", temp_script], capture_output=True, text=True)
        if "error" in result.stdout.lower() or "failed" in result.stdout.lower():
            print(f"[-] Diskpart error: {result.stdout.strip()}", file=sys.stderr)
            return False
        return True
    finally:
        if os.path.exists(temp_script):
            os.remove(temp_script)

def detach_vhd():
    print(f"[*] Attempting to detach VHD: {VHD_PATH}")
    cmds = [f'select vdisk file="{VHD_PATH}"', "detach vdisk"]
    return run_diskpart_script(cmds)

def attach_vhd():
    print(f"[*] Attempting to attach VHD: {VHD_PATH}")
    cmds = [f'select vdisk file="{VHD_PATH}"', "attach vdisk"]
    return run_diskpart_script(cmds)

def wait_for_file_release(path, timeout=10):
    """Loops until the file is completely released by Windows filesystem."""
    print("[*] Waiting for Windows to release file lock...")
    start_time = time.time()
    while time.time() - start_time < timeout:
        try:
            file_object = open(path, "a+b")
            file_object.close()
            print("[+] File lock verified released.")
            return True
        except IOError:
            time.sleep(0.5)
    return False

def main():
    if not is_admin():
        print("[-] ERROR: This script requires administrator privileges.")
        print("[-] Please restart Visual Studio as an Administrator.")
        sys.exit(1)

    if not os.path.exists(VHD_PATH):
        print(f"[-] ERROR: VHD target file not found at {VHD_PATH}")
        sys.exit(1)

    if not detach_vhd():
        print("[-] Failed to detach VHD. It might already be detached or busy.")

    if not wait_for_file_release(VHD_PATH):
        print("[-] ERROR: File is still locked by an unknown process. Aborting QEMU.")
        sys.exit(1)

    try:
        print("[*] Launching QEMU...")
        subprocess.run(QEMU_COMMAND, check=True)
        print("[+] QEMU window closed by user.")
    except Exception as e:
        print(f"[-] QEMU Runtime Exception: {e}", file=sys.stderr)
    finally:
        print("\n[-] Cleaning up...")
        time.sleep(1)
        attach_vhd()

if __name__ == "__main__":
    main()