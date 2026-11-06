#include <stdint.h>
#include <stddef.h>

extern void UARTDebugOut(const char* msg);
extern void* kmalloc(size_t size);
extern void kfree(void* ptr);
extern int register_virtio_driver(void* driver);

static void mod_print(const char* msg) {
	UARTDebugOut(msg);
}

struct pci_device_id {
	uint32_t vendor;
	uint32_t device;
	uint32_t subvendor;
	uint32_t subdevice;
	uint32_t class;
	uint32_t class_mask;
	uint32_t driver_data;
};

static const struct pci_device_id id_table[] = {
	{0x1AF4, 0x1041, 0xFFFF, 0xFFFF, 0, 0, 0},
	{0x1AF4, 0x1000, 0xFFFF, 0xFFFF, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0},
};

int init(void) {
	mod_print("[test_module]: init called\r\n");

	void* p = kmalloc(128);
	if (p) {
		mod_print("[test_module]: kmalloc(128) OK\r\n");
		kfree(p);
		mod_print("[test_module]: kfree OK\r\n");
	} else {
		mod_print("[test_module]: kmalloc FAILED\r\n");
	}

	mod_print("[test_module]: loading complete\r\n");
	return 0;
}

void exit(void) {
	mod_print("[test_module]: exit called, unloading\r\n");
}
