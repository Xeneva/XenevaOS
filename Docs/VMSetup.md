# Setting up the Virtual Machine for XenevaOS

## Notice
XenevaOS is well-tested only on VMware Workstation and VirtualBox with a VMDK file mapping to a raw physical disk (_the USB Flash drive containing all required binaries_). Refer to [Setting up USB Flash Drive](BuildInstructions.md) for more information. Everything should be set up as described in that document. ***You can skip directly to [Virtual Machine Creation](#creating-the-virtual-machine-in-virtualbox) if you have downloaded the release VHD file***.

## Creating the VMDK File

In VirtualBox, the VMs will use the VMDK file on AHCI Controller Port 1. To create the VMDK, we use the VirtualBox tool `VBoxManage.exe` located at `C:\Program Files\Oracle\VirtualBox`. You can add this location to the Windows environment variable **PATH** to access the tool from any directory on your local machine.

- Check the drive number of your USB Flash Drive using `diskmgmt.msc`. On Linux or macOS, use `lsblk` or `diskutil list`, respectively.
- Start `cmd.exe` in **Run as administrator** mode.
- Run the following command:
  ```cmd
  VBoxManage createmedium -filename "Your\path\to\filename.vmdk" --variant RawDisk --format=VMDK --property RawDrive=\\.\PhysicalDriveX
  ```
  Replace `X` with your USB drive number.
- Example:
  ```cmd
  VBoxManage createmedium -filename "E:\usb.vmdk" --variant RawDisk --format=VMDK --property RawDrive=\\.\PhysicalDrive1
  ```
  (where `PhysicalDrive1` corresponds to the USB Flash Drive).
- Up to this point, you should be able to create the VMDK file successfully mapped to your physical drive. 

## Disabling VBS (Virtualization-Based Security) on Windows (*Important*)

Before running XenevaOS on a Windows machine, you need to temporarily disable the VBS feature of Windows. XenevaOS fails to access virtualized hardware memory regions on VBS-enabled Windows machines, which prevents the OS from booting. VBS completely occupies hardware resources to create an isolated memory region. 

To turn off VBS:

### 1. Turn off Memory Integrity
- Open **Windows Security**.
- Click **Device Security** in the left panel.
- Under **Core Isolation**, click **Core Isolation details**.
- Toggle **Memory Integrity** to **Off**.
- Restart the PC (or do a single restart after executing the command in the next step).

### 2. Turn off `hypervisorlaunchtype`
- Open the **Command Prompt** as Administrator.
- Run `bcdedit /set hypervisorlaunchtype off` and press **Enter**.
- Restart your PC to apply the changes.


## Creating the Virtual Machine in VirtualBox

- Create a new Virtual Machine in VirtualBox via **Machine** -> **New**.
- Give the VM a name, select **Linux** as the Type, select **Ubuntu (64-bit)** as the Version, and click **Next**.
- Set the Base Memory to **2048 MB**, set the **Processor Count to 3**, check the **Enable EFI (special OSes only)** option, and click **Next**.
- Select the **Do Not Add a Virtual Hard Disk** option and click **Next**.
- Click **Finish** to complete the process.
- The new Virtual Machine will now appear in VirtualBox. Open the **Settings** of the newly created Virtual Machine.
- Under the **System** tab, select **ICH9** as the Chipset, **USB Tablet** as the Pointing Device, and check **Enable I/O APIC** in Extended Features.
- Under **Storage**, add a new **AHCI (SATA)** controller.
- Add a new **Hard Disk** to the AHCI controller, click the **Add** button in the Hard Disk Selector, and select the VMDK file created earlier (or the downloaded release VHD file).
- Under **Audio**, select **Windows DirectSound** as the Host Audio Driver, select **Intel HD Audio** as the Audio Controller, and enable both input and output.
- Under **USB**, select **Enable USB Controller** and choose **USB 3.0 (xHCI) Controller**.
- XenevaOS is now configured and ready to boot.

## Using the Xeneva Terminal (*only for v1.0*)

When the OS boots, it automatically starts the Window Manager and launches the Xeneva Terminal. The Desktop environment must be started manually:
- Type `xelnch` and press **Enter** to start the **AppTray** process.
- Send a `Ctrl+C` signal to the terminal.
- Type `nmdapha` and press **Enter** to start the Namdapha Desktop bar.
- You can now open the **AppTray** by clicking the Xeneva logo on the Namdapha Bar and launch applications.

## Starting the `play` Application for Audio

XenevaOS has a built-in `play` command to play raw wave audio files in 48kHz / 16-bit format:
- In Xeneva Terminal, type `play -file /music/folk.wav`. This plays the default audio file in the `/music` folder.
- Press `Ctrl+C` to return to the terminal input.

## Conclusion

Currently, XenevaOS components must be started separately. You can attach a serial terminal to print internal debugging logs and track kernel exceptions. See [How to set up a serial Terminal for XenevaOS](SerialTerminalAttach.md) for more information.
