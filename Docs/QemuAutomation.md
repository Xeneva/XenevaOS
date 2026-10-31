# QEMU VHD Automation Runner

This script automates the process of detaching a Virtual Hard Disk (`.vhd` / `.vhdx`) from the Windows host, launching XenevaOS inside QEMU, and automatically reattaching the VHD once the QEMU emulation window is closed. 

This eliminates the manual effort of unmounting/mounting disks during building the project.

## Prerequisites

1. **Python 3.x** must be installed and added to your system `PATH`.
2. **QEMU** must be installed, and your architecture binary (e.g., `qemu-system-aarch64`) must be accessible via your terminal.
3. **Administrator Privileges**: Windows requires elevated permissions to modify disk attachments (`diskpart`).



##  Critical Requirement: Run Visual Studio as Administrator

Because the script runs inside the Visual Studio build lifecycle, **Visual Studio itself must be running with full Administrator privileges**. If it is not, the script will fail to detach the VHD, and QEMU will crash with an "Access Denied" error.

* **How to do this:** Close Visual Studio entirely. Find your Visual Studio shortcut or executable, **Right-Click** it, and select **Run as administrator**.



## Setup Guide: Integrating with Visual Studio

Follow these steps to configure your project to run this script automatically on every successful build, or configure it to run with any project build of your choice:

1. Open the **Properties** of the project in Visual Studio.
2. Navigate to **Build Events** and select **Post-Build Event**.
3. In the **Command Line** field, click the dropdown button and select **Edit**.
4. In the Command Line editor, go to a new line and enter: `python $(ProjectDir)script-1.py`
5. Click **OK** to save the post-build event, and then click **Apply**.
