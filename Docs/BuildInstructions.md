# XenevaOS Build Instructions

## Prerequisites

Before building XenevaOS, make sure all dependencies are installed and all steps are performed in the sequential order mentioned in this document.
XenevaOS is built purely in a Windows environment.
<br>
<br>

## Visual Studio 2019 Community edition

Visual Studio 2019 Community Edition is required to open and edit XenevaOS projects. The Community Edition is a free version of Microsoft Visual Studio. You can also use any paid edition of 2019. If a later version of Visual Studio is used, projects need to be configured manually to meet compiler version requirements. This is done by going to:
- Open Visual Studio and open the desired solution
- Select the desired Project from Solution Explorer and right click
- ```Project -> Properties -> General -> Platform Toolset```
<br>

## Netwide Assembler (NASM) 

Download and install Netwide Assembler (NASM) from [NASM Official website](https://www.nasm.us/). NASM needs to be integrated with Visual Studio to compile assembly source code. This can be done by following steps: <br>
- ```Go to the Resources directory in the XenevaOS repository, then copy and extract nasmprops.zip.```
- ```Copy "nasm.props", "nasm.targets", "nasm.xml" from nasmprops.zip to C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Microsoft\VC\v160\BuildCustomizations```
<br>
<br>

## _Setting up USB Flash Drive_ 
Format a USB Flash drive (Minimum 2 GiB space required) with a GPT partition and FAT32 filesystem.
You can use __Rufus__ to format the USB Flash drive (_by downloading Rufus_) or ```diskpart``` to format the disk (available by default on Windows):
- Open ```cmd.exe``` with _Run as administrator_ mode
- type ```diskpart``` press Enter
- type ```list disk``` press Enter
- Identify your USB disk number from the list 
- type ```select diskX``` replace X with USB disk number
- type ```clean``` to clean the disk
- type ```convert gpt create partition primary``` press Enter
- type ```format fs=fat32 unit=4096 quick```
- type ```assign X``` replace 'X' with a drive letter

## _Setting up Environment Variables_
Required Environment Variables are:
- __XENEVA_BUILDS__ with the variable value pointing to the location of the USB Flash Drive.

Add a User Environment Variable with __"Variable Name: XENEVA_BUILDS"__ and __"Variable Value"__ as the location of the USB Flash Drive. This can be done simply by clicking the __"Browse Directory"__ button. 
<br>

## _Copying required resources_ 
__Please make sure that all files from the resource directory inside the XenevaOS repository are copied as they are to the __XENEVA_BUILDS__ PATH (_i.e., to the USB Flash Drive_) and make sure they are copied prior to starting the build process.__

XenevaOS requires resource files to run. Some important resource files are:
- _"audrv.cnf"_ contains PCI/PCIe class-subclass codes with driver file paths, required by the _'Kernel'_
- _"ftlst.cnf"_ contains font names and their location paths
- _"lnch.cnf"_ contains Application entries and their location paths for the __AppTray__ to recognize and display installed applications.
Go to __Resources\resources\__ directory in the repository and copy all the resource files as it is, to __"XENEVA_BUILDS"__ PATH (_i.e to the USB Flash Drive_)<br><br>

# Building the Solution

Before opening the solution, please make sure that Netwide Assembler (NASM) is installed and configured as mentioned above. Open the _"Aurora.sln"_ file from the repository and everything should get automatically configured. 
- In the Solution Explorer, select the solution. It should be named _'Solution Aurora'_ 
- ```Right Click on 'Solution Aurora' from Solution Explorer, then click "Rebuild Solution" ```
- This should rebuild the entire solution and produce the binaries in the _"Build"_ folder of the XenevaOS repository.
- The solution will automatically copy all the binaries to the __"XENEVA_BUILDS"__ PATH (_i.e., the USB Flash Drive_).

## More Information

At this point you should see all the necessary files inside your __"XENEVA_BUILDS"__ PATH. Now you need to setup VM for XenevaOS. You can visit [Testing environment setup instructions](VMSetup.md).





