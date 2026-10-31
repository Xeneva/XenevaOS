# Drivers in XenevaOS

Drivers are specialized software components that enable an operating system to communicate with hardware devices. In XenevaOS, kernel-mode drivers help the entire OS communicate with hardware devices. There are two types of drivers: 
- __Kernel mode drivers__
- __User mode drivers__

Current versions of XenevaOS support only _Kernel mode drivers_.

Boot-time drivers are drivers that get loaded prior to the kernel for basic system runtime. Boot-time drivers are loaded by XNLDR, which loads all necessary boot drivers and maps them to _Kernel Space_ (_higher-half memory_). Some examples of boot-time drivers include all _Storage Drivers_, _File System drivers_, and the ACPI driver. Storage Drivers include (_NVMe driver, AHCI/SATA Controller driver, IDE drivers, and USB Mass Storage driver_). <br>
The XenevaOS Kernel depends on _Storage Drivers_ and _File System_ drivers to boot the system. The drivers are loaded and mapped to Kernel Space by XNLDR, and their addresses are passed to the kernel through the _Boot Information_. The XenevaOS Kernel includes special functions that handle boot-time drivers separately from runtime drivers. The function `AuBootDriverInitialise` starts the loading of all boot drivers. Linking of boot drivers to kernel functions is done internally in the kernel. 

## Runtime Drivers
Once boot drivers (like _Storage Drivers_) are loaded and initialized successfully, the kernel uses those drivers to load and start runtime drivers. Runtime Drivers include the _Network Drivers_, _Sound Drivers_, and _Graphics Drivers_. _Network Drivers_ include Ethernet Drivers, while _Sound Drivers_ include the _High Definition Audio Driver_. Runtime drivers are loaded and linked during kernel runtime. <br>
First, the kernel opens and reads the driver configuration file from the root filesystem. The driver configuration file contains driver paths along with PCIe class-subclass-progif values. The kernel reads each entry from the configuration file and loads all PCIe drivers by matching their class-subclass-progif values. 

## Initialization
Though boot-time drivers and runtime drivers are handled separately, the protocols they follow are the same. The drivers must meet requirements that the kernel expects to run properly. Some kernel function calls are commonly required:
- ***AuPCIEScanClass*** : This function returns the bus, device and function number by its classCode and subClassCode from PCIe ( _NOTE: XenevaOS only supports PCIe_)
- ***AuPCIERead/AuPCIEWrite*** : Reads from and Writes to PCIe device
- ***AuPCIEAllocMSI*** : Allocates MSI/MSI-X signal for provided device
- ***setvect*** : Register an interrupt vector for provided device
- ***struct AuDevice*** : The Device structure that holds all device-related information
- ***AuRegisterDevice*** : Registers the device to the Kernel driver subsystem

Expected functions from drivers by the kernel include:
- ***AuDriverMain*** - The main entry point for the driver
- ***AuDriverUnload*** - Clears all the resources allocated by the driver and unloads it from the Kernel

Example for writing a Xeneva Kernel mode driver:

```
AU_EXTERN AU_EXPORT int AuDriverUnload() {
    /*
     * Free up all allocated resources 
     * and return
     */
}
AU_EXTERN AU_EXPORT int AuDriverMain() {
   int bus,dev, func = 0;
   uint64_t device = AuPCIEScanClass(classCode, subClassCode, &bus, &dev, &func);
   if (device == UINT32_MAX)
       return 0;
    ...Now get all PCIe information like base address, setup command register for dma, interrupt etc...
    
    /* Allocate an MSI/MSI-X signal for this device and 
       print out the result to Kernel screen */
    if (AuPCIEAllocMSI(device,36,bus,dev,func))
        SeTextOut("MSI Allocated for device %x \r\n",device);
    setvect(36, DeviceInterruptHandler); /* Here 36 vector number is used randomely, you can use your own vector number */

    AuDevice *audev = (AuDevice*)kmalloc(sizeof(AuDevice));
	audev->classCode = classCode;
	audev->subClassCode = subClassCode;
	audev->progIf = progIf;
	audev->initialized = true;
	audev->aurora_dev_class = DEVICE_CLASS_x;
	audev->aurora_driver_class = DRIVER_CLASS_x;
	AuRegisterDevice(audev);
    return 1;
} 
```
Up to this point, the kernel will initialize the driver, power up the destination hardware, and register an interrupt handler to process interrupts.

## devfs (The Device Filesystem)
In XenevaOS, devices can be controlled through filesystem callbacks. For all devices, the driver creates a file in the device filesystem (`devfs`), similar to a Unix environment. For example, to control each storage disk of an AHCI controller, you can open the disk file at `dev/disk0/ahci1` to read disk number one. Similarly, to read the first SSD from the NVMe controller, you can open `dev/disk1/nvme0`. 

## Interrupt handling
During external driver initialization, the kernel temporarily disables interrupts to ensure that drivers initialize in a stable and predictable manner. However, certain drivers depend on interrupts to complete their initialization sequence. In such cases, a driver may temporarily enable interrupts during its initialization process and disable them again once initialization is complete.<br><br>
Before continuing with the initialization of the next driver, the kernel ensures that interrupts are properly disabled. This prevents unexpected interrupt handling during subsequent driver initialization stages and helps maintain a smooth and controlled boot process.<br><br>

On x86_64 systems, interrupts are disabled using `x64_cli()` and enabled using `x64_sti()`, both declared in `x86_64_lowlevel.h`.  <br><br>
On ARM64 systems, interrupts are disabled using `mark_irqs()` and enabled using `enable_irqs()`, declared in `aa64lowlevel.h`.

## Registering Interrupt handler
 The kernel maintains a list of interrupt handler functions that are invoked whenever a corresponding interrupt is triggered. Both x86_64 and ARM64 architectures follow this interrupt handling model. <br><br>
 Before an interrupt handler can receive interrupts, the corresponding interrupt number must be enabled in the interrupt controller. By default, all interrupts remain disabled until explicitly enabled. <br><br>
 ### x86_64 systems: 
 On x86_64 systems, interrupts handlers can be registered using: ```setvect(size_t vector, void(*function)(size_t vector, void* param))```
 - ``vector`` specifies the interrupt vector number.
 - ``function`` is the interrupt handler function associated with that interrupt.

 Before calling ``setvect``, the interrupt must already be enabled at both the bus level and the interrupt controller level.

 ### arm64 systems:
 On ARM64 systems, interrupt handlers can be registered using:
 ```GICRegisterSPIHandler(void* fptr, int spi)```
 - ``spi`` specifies the Shared Peripheral Interrupt (SPI) number.
 - ``fptr`` points to the interrupt handler function.

 Before calling ``GICRegisterSPIHandler()``, the interrupt must be enabled at both the bus level and the interrupt controller level.
 At the interrupt controller level the interrupt can be enabled using : ``GICEnableSPIIRQ(uint32_t irq)`` where ``irq`` is the interrupt number.

 ## Driver Framework
 Drivers in XenevaOS can be caregorized into layers where each layers depends on the other. For example on arm based devices, a peripheral IP may not respond properly until it's clock and power source has been enabled. The peripheral's voltage can also be controlled using regulator devices. It's a good design decision to have a specific driver for each components and peripheral. For example having a proper driver for the clock controller or power controller can enable the kernel to power up specific clock and power to get desired peripheral working. This design introduces the dependency needs of each driver to the other within the kernel.

 XenevaOS introduces two concepts of layer based driver:
 - Bordoisila Platform Driver
 - Bordoisila Resource Driver

 ### Bordoisila Platform Driver:
 Bordoisila Platform Driver defines the main peripheral driver, i.e each main driver register itself as a ___Bordoisila Platform Driver___. Within the driver structure, the kernel expects specific function pointers from the peripheral driver to manipulate the hardware over time based on demanding situation. For example, on power saving pressure kernel may put a specific hardware on suspend state, so it will call suspend function pointer of it's peripheral driver. Its upto the driver implementation how it manages its internal data structures and register writes to satisfy the kernel call. Below is the platform driver structure.

 ```
 typedef struct _bordoisila_driver_ {
	const char* name;
	const char* compat; //match list in DT/acpi bindings
	BordoisilaDriverResource* resources;
	int num_resource;
	enum driver_type type;

	/* Ops*/
	int (*scan)(struct _bordoisila_driver_* dev);
	int (*probe)(struct _bordoisila_driver_* dev);
	int (*remove)(struct _bordoisila_driver_* dev);
	int (*suspend)(struct _bordoisila_driver_* dev);
	int (*resume)(struct _bordoisila_driver_* dev);

	void* priv;
	uint8_t driver_state;
	int refcount;
	struct _bordoisila_driver_* parent;
	struct _bordoisila_driver_* next;
}BordoisilaDriver;
```
|Field | Description |
|------|-------------|
| `name` | Name of the Platform Driver, for example _"dw_hdmi"_
| `compat`| Compatible string matching within Device Tree or ACPI subsystem |
| `resources` | Number of ___BordosiilaDriverResources___ this platform driver use, for example number of ___BordoisilaClk___ or ___BordoisilaPower___. This list helps the kernel to manage resources in an efficient way by not performing any removing or modification to the resource until it's completely free from every Platform drivers.
| `num_resource` | Number of ___BordosiilaDriverResources___ this driver has allocated or used from the kernel |
| `type` | Type of the platform driver, In the next table types are properly mentioned |
| `scan` | Address of the scan function within the driver, this function is only important if the platform driver is a bus type driver 
| `probe` | Address of the prob function within the driver, this function initializes all the data structures needed and put the hardware into initial state. This function needed to be called from `AuDriverMain()` entry of the driver.
| `remove` | Address of the remove function within the driver, this function cleans all the allocated data structures and put the hardware into power down state if necessary. This function is needed to be called from `AuDriverUnload()` of the driver
| `suspend` | Address of the suspend function within the driver, this function perform all the necessary operations to put the hardware into temporarily suspended state and notifies the kernel. This function will be called by kernel whenever necessary or in user demand, or hardware's own condition.
| `resume` | Address of the resume function within the driver, this function resumes the driver if it was suspended early. This function will be called by kernel whenever necessary or in user demand, or hardware's own condition
| `priv` | Pointer to driver's own internal data structure
| `driver_state` | Current state of the platform driver, whether if it's probed/unprobed/suspended. Below code snippet describe all available state: ___B_DRIVER_STATE_PROBED___, ___B_DRIVER_STATE_FAILED___, ___B_DRIVER_STATE_UNBOUND___
| `refcount` | Reference count is incremented by the number of the driver is shared between other drivers
| `parent` | Pointer to parent platform driver node
| `next`   | Pointer to next platform driver node

### Platform Driver types

```
enum driver_type {
	BORDOISILA_DRIVER_NORMAL,
	BORDOISILA_DRIVER_BUS_I2C,
	BORDOISILA_DRIVER_BUS_SPI,
	BORDOISILA_DRIVER_BUS_USB,
};
```

| `driver_type` | _Description_ 
|---------------|---------------
| `BORDOISILA_DRIVER_NORMAL` | _Driver is a normal peripheral driver_ 
| `BORDOISILA_DRIVER_BUS_I2C` | _Driver is a i2c bus driver_ |
| `BORDOISILA_DRIVER_BUS_SPI` | _Driver is a SPI bus driver_
| `BORDOISILA_DRIVER_BUS_USB` | _Driver is a USB bus driver_ |


