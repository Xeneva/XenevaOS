#ifndef CAPABILITY_H
#define CAPABILITY_H



#include <aurora.h>
#include <Cred/user.h>
#include <stdint.h>
//#include <stdbool.h>



#define CAP_READ      (1 << 0)
#define CAP_WRITE     (1 << 1)

#define CAP_EXECUTE   (1 << 2)
#define CAP_SEEK      (1 << 3)
#define CAP_IOCTL     (1 << 4)

#define CAP_DUP       (1 << 5)

#define CAP_TRANSFER  (1 << 6)
/* hardware-handle rights (CAP_OBJ_PCIDEV) */
#define CAP_HW_CFG    (1 << 7)    /* PCI config space access */
#define CAP_HW_MAP    (1 << 8)    /* map BARs into the process */
#define CAP_HW_DMA    (1 << 9)    /* allocate DMA buffers */
#define CAP_HW_IRQ    (1 << 10)   /* receive the device's interrupts */
#define CAP_HW_RIGHTS_MASK (CAP_HW_CFG | CAP_HW_MAP | CAP_HW_DMA | CAP_HW_IRQ)

#define CAP_RIGHTS_NONE      0

#define CAP_FILE_RIGHTS_MASK  (CAP_READ | CAP_WRITE | CAP_SEEK | CAP_IOCTL | CAP_DUP | CAP_TRANSFER)

/* Return codes */
#define CAP_OK            0
#define CAP_ERR_INVALID  -1
#define CAP_ERR_PERM     -2

#define CAP_OBJ_FILE   1
#define CAP_OBJ_PCIDEV 2   /* a claimed PCI function, see hwdev.h */

#define CAP_FLAG_NONE         0

#define CAP_FLAG_NO_INHERIT   (1 << 0)



/* Capability entry */
typedef struct _au_capability_ {

	void* object;


	uint8_t object_type;


	CapRights rights;


	UID_NUM owner;


	uint16_t flags;


	unsigned char valid;
}AuCapability;

extern int BordoisilaCapCreate(void* proc, int fd, void* object, uint8_t type, CapRights rights);
extern AuCapability* BordoisilaCapLookup(void* proc, int fd);
extern bool BordoisilaCapCheckRights(void* proc, int fd, CapRights required);
extern int BordoisilaCapDup(void* proc, int oldfd, int newfd);
extern int BordoisilaCapRestrict(void* proc, int fd, CapRights new_rights);
extern void BordoisilaCapDestroy(void* proc, int fd);
extern void BordoisilaCapCleanupProcess(void* proc);

/* called by the capability table before a CAP_OBJ_PCIDEV slot is cleared, so
 * the device can be unmapped, its DMA freed and its claim released */
extern void AuHwdevCapReleased(void* proc, AuCapability* cap);
extern void BordoisilaCapInheritTable(void* parent, void* child);

#endif
