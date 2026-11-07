#include <stdint.h>
#include <stddef.h>

struct kernel_export {
	char* name;
	void* addr;
};

/* Drivers declarations */
extern void AuPCIEAllocMSI(void);
extern void AuPCIEScanClass(void);
extern void AuPCIEWrite(void);
extern void AuPCIERead(void);
extern void dsb_ish(void);
extern void isb_flush(void);
extern void dc_ivac(void);
extern void dc_cvac(void);
extern void GICEnableSPIIRQ(void);
extern void AuGICAllocateSPI(void);
extern void GICRegisterSPIHandler(void);
extern void UARTDebugOut(void);
extern void AuPmmngrAllocPage(void);
extern void AuPmmngrAllocPages(void);
extern void AuPmmngrReleasePage(void);
extern void AuPmmngrReleasePages(void);
extern void AuTextOut(void);
extern void AuAddNetAdapter(void);
extern void AuNetAddConnectedRoute4(void);
extern void AuNetAddDefaultRoute4(void);
extern void AuNetAddConnectedRoute6(void);
extern void AuNetAddDefaultRoute6(void);
extern void AuNetRegisterRxPoll(void);
extern void AuNetRxPoll(void);
extern void AuDnsSetServer4(void);
extern void AuMapMMIO(void);
extern void AuVirtioPCIInit(void);
extern void AuVirtioPCISetupQueue(void);
extern void AuVirtioPCINotifyQueue(void);
extern void AuVirtioPCIPostAvail(void);
extern void strcpy(void);
extern void memset(void);
extern void memcpy(void);
extern void kmalloc(void);
extern void kfree(void);
extern void AuEthernetHandle(void);
extern void P2V(void);
extern void V2P(void);
extern void AuPCIEGetDevice(void);
extern void AuPCIERead64(void);
extern void AuPCIEReadBAR(void);
extern void AuIsPCIeInitialized(void);
extern void register_virtio_driver(void);
extern void virtio_device_register(void);
/* Linux .ko symbol surface (linux_kmod_shim.c / virtio_shim.c / _print.c) */
extern void __register_virtio_driver(void);
extern void unregister_virtio_driver(void);
extern void virtio_reset_device(void);
extern void virtqueue_add_inbuf(void);
extern void virtqueue_get_buf(void);
extern void virtqueue_kick(void);
extern void hwrng_register(void);
extern void hwrng_unregister(void);
extern void complete(void);
extern void wait_for_completion_killable(void);
extern void __init_swait_queue_head(void);
extern void ida_alloc_range(void);
extern void ida_free(void);
extern void sg_init_one(void);
extern void _sprintf(void);
extern void __kmalloc_cache_noprof(void);
extern void kmalloc_caches(void);
extern void AuWifiPortalSetTx(void);
extern void AuWifiPortalTx(void);
extern void AuWifiPortalSetRx(void);
extern void AuWifiPortalRx(void);
extern void AuRouteSetFlag4(void);
extern void AuGetNetworkAdapter(void);
extern void AuVFSFind(void);
extern void AuDevFSAddFile(void);
extern void AuDevFSRemoveFile(void);
extern void AuGetBootInfoStruc(void);
extern void AuMapPage(void);
extern void AuConsoleSetConInfo(void);
extern void AuConsoleSetPresentHook(void);
extern void AuConsoleRestoreBootFb(void);
extern void strcmp(void);
extern void strlen(void);
extern void memcmp(void);

struct kernel_export k_exports[] = {
	{"AuPCIEAllocMSI", (void*)AuPCIEAllocMSI},
	{"AuPCIEScanClass", (void*)AuPCIEScanClass},
	{"AuPCIEWrite", (void*)AuPCIEWrite},
	{"AuPCIERead", (void*)AuPCIERead},
	{"dsb_ish", (void*)dsb_ish},
	{"isb_flush", (void*)isb_flush},
	{"dc_ivac", (void*)dc_ivac},
	{"dc_cvac", (void*)dc_cvac},
	{"GICEnableSPIIRQ", (void*)GICEnableSPIIRQ},
	{"AuGICAllocateSPI", (void*)AuGICAllocateSPI},
	{"GICRegisterSPIHandler", (void*)GICRegisterSPIHandler},
	{"UARTDebugOut", (void*)UARTDebugOut},
	{"AuPmmngrAllocPage", (void*)AuPmmngrAllocPage},
	{"AuPmmngrAllocPages", (void*)AuPmmngrAllocPages},
	{"AuPmmngrReleasePage", (void*)AuPmmngrReleasePage},
	{"AuPmmngrReleasePages", (void*)AuPmmngrReleasePages},
	{"P2V", (void*)P2V},
	{"AuTextOut", (void*)AuTextOut},
	{"AuAddNetAdapter", (void*)AuAddNetAdapter},
	{"AuNetAddConnectedRoute4", (void*)AuNetAddConnectedRoute4},
	{"AuNetAddDefaultRoute4", (void*)AuNetAddDefaultRoute4},
	{"AuNetAddConnectedRoute6", (void*)AuNetAddConnectedRoute6},
	{"AuNetAddDefaultRoute6", (void*)AuNetAddDefaultRoute6},
	{"AuNetRegisterRxPoll", (void*)AuNetRegisterRxPoll},
	{"AuNetRxPoll", (void*)AuNetRxPoll},
	{"AuDnsSetServer4", (void*)AuDnsSetServer4},
	{"AuMapMMIO", (void*)AuMapMMIO},
	{"AuVirtioPCIInit", (void*)AuVirtioPCIInit},
	{"AuVirtioPCISetupQueue", (void*)AuVirtioPCISetupQueue},
	{"AuVirtioPCINotifyQueue", (void*)AuVirtioPCINotifyQueue},
	{"AuVirtioPCIPostAvail", (void*)AuVirtioPCIPostAvail},
	{"strcpy", (void*)strcpy},
	{"memset", (void*)memset},
	{"memcpy", (void*)memcpy},
	{"kmalloc", (void*)kmalloc},
	{"kfree", (void*)kfree},
	{"AuEthernetHandle", (void*)AuEthernetHandle},
	{"AuPCIEGetDevice", (void*)AuPCIEGetDevice},
	{"AuPCIERead64", (void*)AuPCIERead64},
	{"AuPCIEReadBAR", (void*)AuPCIEReadBAR},
	{"AuIsPCIeInitialized", (void*)AuIsPCIeInitialized},
	{"register_virtio_driver", (void*)register_virtio_driver},
	{"virtio_device_register", (void*)virtio_device_register},
	{"__register_virtio_driver", (void*)__register_virtio_driver},
	{"unregister_virtio_driver", (void*)unregister_virtio_driver},
	{"virtio_reset_device", (void*)virtio_reset_device},
	{"virtqueue_add_inbuf", (void*)virtqueue_add_inbuf},
	{"virtqueue_get_buf", (void*)virtqueue_get_buf},
	{"virtqueue_kick", (void*)virtqueue_kick},
	{"hwrng_register", (void*)hwrng_register},
	{"hwrng_unregister", (void*)hwrng_unregister},
	{"complete", (void*)complete},
	{"wait_for_completion_killable", (void*)wait_for_completion_killable},
	{"__init_swait_queue_head", (void*)__init_swait_queue_head},
	{"ida_alloc_range", (void*)ida_alloc_range},
	{"ida_free", (void*)ida_free},
	{"sg_init_one", (void*)sg_init_one},
	{"sprintf", (void*)_sprintf},
	{"__kmalloc_cache_noprof", (void*)__kmalloc_cache_noprof},
	{"kmalloc_caches", (void*)kmalloc_caches},
	{"AuWifiPortalSetTx", (void*)AuWifiPortalSetTx},
	{"AuWifiPortalTx", (void*)AuWifiPortalTx},
	{"AuWifiPortalSetRx", (void*)AuWifiPortalSetRx},
	{"AuWifiPortalRx", (void*)AuWifiPortalRx},
	{"AuRouteSetFlag4", (void*)AuRouteSetFlag4},
	{"AuGetNetworkAdapter", (void*)AuGetNetworkAdapter},
	{"AuVFSFind", (void*)AuVFSFind},
	{"AuDevFSAddFile", (void*)AuDevFSAddFile},
	{"AuDevFSRemoveFile", (void*)AuDevFSRemoveFile},
	{"AuGetBootInfoStruc", (void*)AuGetBootInfoStruc},
	{"AuMapPage", (void*)AuMapPage},
	{"AuConsoleSetConInfo", (void*)AuConsoleSetConInfo},
	{"AuConsoleSetPresentHook", (void*)AuConsoleSetPresentHook},
	{"AuConsoleRestoreBootFb", (void*)AuConsoleRestoreBootFb},
	{"strcmp", (void*)strcmp},
	{"strlen", (void*)strlen},
	{"memcmp", (void*)memcmp},
};

int k_exports_count = sizeof(k_exports) / sizeof(struct kernel_export);
