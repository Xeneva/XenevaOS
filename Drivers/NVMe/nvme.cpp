/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2024, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

#include <aurora.h>
#include <aucon.h>
#include <Hal/hal.h>
#include <Hal/x86_64_hal.h>
#include <Hal/serial.h>
#include "nvme.h"
#include <pcie.h>
#include <Mm/kmalloc.h>
#include <Hal/hal.h>
#include <Mm/vmmngr.h>
#include <_null.h>
#include <Mm/pmmngr.h>
#include "namespace.h"
#include <Fs/vdisk.h>

NVMeDev *nvme;
/*
* NVMeOutl -- write 32 bit data to nvme register
* @param reg -- Register
* @param value -- value to write
*/
void NVMeOutl(int reg, uint32_t value) {
	volatile uint32_t* mmio = (uint32_t*)(nvme->mmiobase + reg);
	*mmio = value;
}

/*
* NVMeInl -- read a 32 bit data from nvme register
* @param reg -- Register
*/
uint32_t NVMeInl(int reg) {
	volatile uint32_t* mmio = (uint32_t*)(nvme->mmiobase + reg);
	return *mmio;
}

/*
* NVMeOutW -- write 16 bit data to nvme register
* @param reg -- Register
* @param value -- value to write
*/
void NVMeOutW(int reg, uint16_t value) {
	volatile uint16_t* mmio = (uint16_t*)(nvme->mmiobase + reg);
	*mmio = value;
}

/*
* NVMeInW -- reads 16 bit data from nvme register
* @param reg -- Register
*/
uint16_t NVMeInW(int reg) {
	volatile uint16_t* mmio = (uint16_t*)(nvme->mmiobase + reg);
	return *mmio;
}

/*
* NVMeOutB -- writes 8 bit data to nvme register
* @param reg -- Register
* @param value -- data to write
*/
void NVMeOutB(int reg, uint8_t value) {

	volatile uint8_t* mmio = (uint8_t*)(nvme->mmiobase + reg);
	*mmio = value;
}

/*
* NVMeInB -- reads 8 bit data to nvme register
* @param reg -- Register
*/
uint8_t NVMeInB(int reg) {
	volatile uint8_t* mmio = (uint8_t*)(nvme->mmiobase + reg);
	return *mmio;
}

/*
* NVMeOutB -- writes 64 bit data to nvme register
* @param reg -- Register
* @param value -- data to write
*/
void NVMeOutQ(int reg, uint64_t value) {

	volatile uint64_t* mmio = (uint64_t*)(nvme->mmiobase + reg); //| (nvme->mmiobase + (reg + 4)) << 32);
	*mmio = value;
}

/*
* NVMeInB -- reads 8 bit data to nvme register
* @param reg -- Register
*/
uint64_t NVMeInQ(int reg) {
	volatile uint64_t* mmio = (uint64_t*)(nvme->mmiobase + reg);
	AuTextOut("Base addr q -> %x \n", mmio);
	return *mmio;
}

void NVMeResetController() {
	uint32_t nvmeCC = NVMeInl(NVME_REGISTER_CC);
	nvmeCC = (nvmeCC & ~(NVME_CC_EN_MASK)) | NVME_CC_DISABLE;
	NVMeOutl(NVME_REGISTER_CC, nvmeCC);
}

void NVMeDisable() {
	uint32_t config = NVMeInl(NVME_REGISTER_CC);
	config &= ~NVME_CFG_ENABLE;
	NVMeOutl(NVME_REGISTER_CC, config);
}

void NVMeEnable() {
	uint32_t config = NVMeInl(NVME_REGISTER_CC);
	config |= NVME_CFG_ENABLE;
	NVMeOutl(NVME_REGISTER_CC, config);
}

void NVMeSetMemoryPageSize(uint32_t size) {
	if (size < nvme->minPageSize)
		return;
	if (size > nvme->maxPageSize)
		return;

	uint32_t i = 0;
	while (!(size & 1)){
		i++;
		size >>= 1;
	}

	if (i < 12)
		return;
	uint32_t config = NVMeInl(NVME_REGISTER_CC);
	config = (config & ~NVME_CFG_MPS(NVME_CFG_MPS_MASK)) | NVME_CFG_MPS(i - 12);
	AuTextOut("Memory page size setuped %d \n", i);
	NVMeOutl(NVME_REGISTER_CC, config);
}

void NVMeSetCommandSet(uint8_t set) {
	uint32_t config = NVMeInl(NVME_REGISTER_CC);
	config = (config & ~NVME_CFG_CSS(NVME_CFG_CSS_MASK)) | NVME_CFG_CSS(set);
	NVMeOutl(NVME_REGISTER_CC, config);
}

void NVMeSetAdminCQSize(uint16_t sz) {
	uint32_t aqa = NVMeInl(NVME_REGISTER_AQA);
	aqa = (aqa & ~NVME_AQA_ACQS(NVME_AQA_AQS_MASK)) | NVME_AQA_ACQS(sz - 1);
	NVMeOutl(NVME_REGISTER_AQA, aqa);
}

void NVMeSetAdminSQSize(uint16_t sz) {
	uint32_t aqa = NVMeInl(NVME_REGISTER_AQA);
	aqa = (aqa & ~NVME_AQA_ASQS(NVME_AQA_AQS_MASK)) | NVME_AQA_ASQS(sz - 1);
	NVMeOutl(NVME_REGISTER_AQA, aqa);
}


uint32_t NVMeGetSubmissionDoorbell(uint16_t queueID) {
	uint32_t offset = 0x1000 + (2 * queueID) * (4 << nvme->doorbellStride);
	return offset;
}

uint32_t NVMeGetCompletionDoorbell(uint16_t queueID) {
	uint32_t offset = 0x1000 + (2 * queueID + 1) * (4 << nvme->doorbellStride);
	return offset;
}

/*
 * NVMeCreateQueue -- create a new queue
 * @param queueID -- queue id
 * @param cqSize -- completion queue size
 * @param sqSize -- submission queue size
 */
NVMeQueue* NVMeCreateQueue(uint16_t queueID, uint64_t cqSize, uint64_t sqSize){
	NVMeQueue* qu = (NVMeQueue*)kmalloc(sizeof(NVMeQueue));
	memset(qu, 0, sizeof(NVMeQueue));
	qu->queueId = queueID;
	qu->cq_size = cqSize;
	qu->cq_count = (cqSize / sizeof(NVMeCompletion));
	qu->sq_size = sqSize;
	qu->sq_count = (sqSize / sizeof(NVMeCommand));
	qu->nextCommandId = 0;
	qu->completion_cycle_state = true;
	qu->sq_tail = 0;
	qu->cq_head = 0;
	return qu;
}

/*
 * NVMeInterrupt -- interrupt handler for NVMe
 */
void NVMeInterrupt(size_t vector, void* param) {
	AuDisableInterrupt();
	AuInterruptEnd(0);
	AuEnableInterrupt();
}

/* NVMeGetQueue -- returns a queue specified by queue id
 * @param queueid -- NVMe Queue ID number
 */
NVMeQueue* NVMeGetQueue(uint16_t queueid) {
	for (int i = 0; i < nvme->NVMeQueueList->pointer; i++) {
		NVMeQueue* queue = (NVMeQueue*)list_get_at(nvme->NVMeQueueList, i);
		if (queue->queueId == queueid)
			return queue;
	}
	return NULL;
}

long time_diff(timeval *old, timeval *newt){
	long seconds = newt->tv_sec - old->tv_sec;
	long micros = newt->tv_usec - old->tv_usec;
	return seconds * 1000000 + micros;
}

/*
 * NVMeSubmitCommand -- submits commands and waits for completion data
 * @param queue -- NVMe Queue data structure
 * @param cmd -- NVMe command
 * @param comp -- Completion data structure
 */
void NVMeSubmitCommand(NVMeQueue *queue, NVMeCommand* cmd, NVMeCompletion* comp) {
	//atomic lock required 
	cmd->commandID = queue->nextCommandId++;
	if (queue->nextCommandId == 0xffff)
		queue->nextCommandId = 0;

	memcpy(&queue->submissionQueue[queue->sq_tail], cmd, sizeof(NVMeCommand));

	queue->sq_tail = (queue->sq_tail + 1) % queue->sq_count;
	*raw_offset<volatile uint32_t*>(nvme->mmiobase, queue->nvmeSQDoorbell) = queue->sq_tail;


	timeval old;
	timeval newt;
	AuGetTimeOfTheDay(&old);
	while (queue->completion_cycle_state == !queue->completionQueue[queue->cq_head].phaseTag){
		AuGetTimeOfTheDay(&newt);
		long val = time_diff(&old, &newt);
		if (val >= 500000){
			return;
		}
	}
	
	memcpy(comp, &queue->completionQueue[queue->cq_head], sizeof(NVMeCompletion));
	
	
	if (++queue->cq_head >= queue->cq_count){
		queue->cq_head = 0;
		queue->completion_cycle_state = !queue->completion_cycle_state;
	}

	*raw_offset<volatile uint32_t*>(nvme->mmiobase, queue->nvmeCQDoorbell) = queue->cq_head;
	//atomic lock release
}

/*
 * NVMeAllocateQueues -- try allocating number of queues
 * @param num -- number of queues needed to allocate
 */
void NVMeAllocateQueues(uint16_t num) {
	NVMeQueue* queue = NVMeGetQueue(0);

	NVMeCommand cmd;
	memset(&cmd, 0, sizeof(NVMeCommand));

	cmd.opcode = (AdminCmdSetFeatures & UINT8_MAX);
	cmd.setFeatures.featureID = (NVMeSetFeatureCommand::FeatureIDNumberOfQueues & UINT8_MAX);
	cmd.setFeatures.dw11 = (num << 16) | num;

	NVMeCompletion completion;
	memset(&completion, 0, sizeof(NVMeCompletion));
	NVMeSubmitCommand(queue, &cmd, &completion);

	if (completion.status > 0)
		SeTextOut("[NVMe]: status -> %d \r\n", completion.status);
	
	nvme->numCQEAllocated = (completion.dw0 >> 16) & 0xffff;
	nvme->numSQEAllocated = completion.dw0 & 0xffff;
}

NVMeQueue* NVMeCreateIOQueue(){
	NVMeQueue* queue = NULL;
	uint64_t sqPhysBase = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	uint64_t cqPhysBase = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);

	uint16_t queueID = nvme->queueAllocatedID;

	NVMeQueue *Admin = NVMeGetQueue(0);
	queue = NVMeCreateQueue(queueID,( nvme->maxQueueEntries),
		(nvme->maxQueueEntries));
	if (!queue)
		return NULL;
	NVMeCompletion completion;
	NVMeCommand command;
	memset(&completion, 0, sizeof(NVMeCompletion));

	memset(&command, 0, sizeof(NVMeCommand));
	command.opcode = (AdminCmdCreateIOCompletionQueue & UINT8_MAX);
	command.createIOCQ.contiguous = 1;
	command.createIOCQ.queueID = queueID;
	command.createIOCQ.queueSize = ((queue->cq_size - 1) & UINT16_MAX);
	command.prp1 = cqPhysBase & UINT64_MAX;

	NVMeSubmitCommand(Admin, &command, &completion);

	if (completion.status > 0) {
		SeTextOut("[NVMe]: Creating I/O comp QUEUE status %d %x\r\n", completion.status, completion.status);
		AuPmmngrReleasePage((uint64_t)cqPhysBase);
		AuPmmngrReleasePage((uint64_t)sqPhysBase);
		kfree(queue);
		return NULL;
		
	}

	uint64_t sqMMIOBase = (uint64_t)AuMapMMIO(sqPhysBase, 1);
	uint64_t cqMMIOBase = (uint64_t)AuMapMMIO(cqPhysBase, 1);
	memset((void*)sqMMIOBase, 0, PAGE_SIZE);
	memset((void*)cqMMIOBase, 0, PAGE_SIZE);
	

	memset(&completion, 0, sizeof(NVMeCompletion));

	memset(&command, 0, sizeof(NVMeCommand));
	command.opcode = (AdminCmdCreateIOSubmissionQueue & UINT8_MAX);
	command.createIOSQ.contiguous = 1;
	command.createIOSQ.queueID = queueID;
	command.createIOSQ.queueSize = ((queue->sq_size - 1) & UINT16_MAX);
	command.createIOSQ.cqID = queueID;
	command.prp1 = sqPhysBase & UINT64_MAX;

	NVMeSubmitCommand(Admin, &command, &completion);

	if (completion.status > 0){
		AuPmmngrReleasePage((uint64_t)sqPhysBase);
		SeTextOut("[NVMe]: I/O Sq creation failed %d \r\n", completion.status);
		kfree(queue);
		return NULL;
	}

	queue->queueId = queueID;
	queue->completionPhysBase = cqPhysBase;
	queue->submissionPhysBase = sqPhysBase;
	queue->completionMMIOBase = cqMMIOBase;
	queue->submissionMMIOBase = sqMMIOBase;
	queue->nvmeCQDoorbell = NVMeGetCompletionDoorbell(queueID);
	queue->nvmeSQDoorbell = NVMeGetSubmissionDoorbell(queueID);
	queue->submissionQueue = (NVMeCommand*)queue->submissionMMIOBase;
	queue->completionQueue = (NVMeCompletion*)queue->completionMMIOBase;
	queue->cq_count -= 1;
	queue->sq_count -= 1;

	list_add(nvme->NVMeQueueList, queue);
	nvme->queueAllocatedID++;
}
/*
 * NVMeIdentifyController -- identify controller command
 */
void NVMeIdentifyController() {
	uint64_t* controlphys = (uint64_t*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	memset(controlphys, 0, PAGE_SIZE);

	NVMeCommand iden;
	memset(&iden,0, sizeof(NVMeCommand));
	iden.opcode = (AdminCmdIdentify & UINT8_MAX);
	iden.prp1 = ((uint64_t)controlphys & UINT64_MAX);
	iden.identify.cns = (NVMeIdentifyCommand::CNSContoller & UINT8_MAX);

	NVMeCompletion comp;
	NVMeQueue* admin = NVMeGetQueue(0);
	NVMeSubmitCommand(admin, &iden, &comp);

	NVMeControllerIdentity* ci = (NVMeControllerIdentity*)controlphys;
	ci->modelNumber[39] = 0;
	ci->serialNumber[19] = 0;
	
	
	NVMeAllocateQueues(12);
	
	for (unsigned i = 0; i < 4; i++){
		NVMeQueue* queue = NVMeCreateIOQueue();
		if (!queue)
			break;
	}
	
	for (unsigned i = 0; i < ci->numNamespaces; i++) {
		uint64_t* physAddr = (uint64_t*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		memset(physAddr, 0, PAGE_SIZE);
		
		NVMeCommand identifyNS;
		identifyNS.opcode = (AdminCmdIdentify & UINT8_MAX);
		identifyNS.prp1 = ((uint64_t)physAddr & UINT64_MAX);
		identifyNS.identify.cns = (NVMeIdentifyCommand::CNSNamespace & UINT8_MAX);
		identifyNS.nsid = i + 1;

		NVMeCompletion comp;
		memset(&comp, 0, sizeof(NVMeCompletion));
		NVMeSubmitCommand(admin, &identifyNS, &comp);
		
		if (comp.status > 0) {
			AuPmmngrReleasePage((uint64_t)physAddr);
			continue;
		}
		NamespaceIdentity *ni = (NamespaceIdentity*)physAddr;
		NVMeInitialiseNamespace(nvme,ci, ni, i + 1);

		AuPmmngrReleasePage((uint64_t)physAddr);
	}

	AuPmmngrReleasePage((uint64_t)controlphys);
}
/*
* NVMeInitialise -- start nvme storage class
*/
int NVMeInitialise() {
	int bus, dev, func = 0;
	uint64_t device = AuPCIEScanClass(0x01, 0x08, &bus, &dev, &func);
	if (device == UINT32_MAX) {
		AuTextOut("[NVMe]: no nvme class found \n");
		return -1;
	}

	uint64_t base32 = AuPCIERead(device, PCI_BAR0, bus, dev, func);
	base32 &= 0xFFFFFFFC;
	base32 |= (static_cast<uint64_t>(AuPCIERead(device, PCI_BAR1, bus, dev, func)) & 0xFFFFFFF0) << 32;

	// enable bus master and memory space
	uint64_t command = AuPCIERead(device, PCI_COMMAND, bus, dev, func);
	command |= (1 << 10);
	command |= 0x6;
	AuPCIEWrite(device, PCI_COMMAND, command, bus, dev, func);


	uint64_t nvmemmio = (uint64_t)AuMapMMIO(base32, 16);

	nvme = (NVMeDev*)kmalloc(sizeof(NVMeDev));
	memset(nvme, 0, sizeof(NVMeDev));
	nvme->mmiobase = nvmemmio;


	uint32_t nvmeVer = NVMeInl(NVME_REGISTER_VS);
	uint8_t minor = nvmeVer >> 8 & 0xff;
	uint8_t major = nvmeVer >> 16 & 0xffff;

	nvme->majorVersion = major;
	nvme->minorVersion = minor;

	uint64_t cap = NVMeInQ(NVME_REGISTER_CAP);
	AuTextOut("[NVMe]: version %d.%d \n", major, minor);

	uint8_t css = (cap >> 37) & UINT8_MAX;
	if (css & 0x1)
		AuTextOut("[NVMe]: only NVMe Command set is supported \n");
	if ((css >> 6) & 0x1)
		AuTextOut("[NVMe]: one or more I/O command sets supported \n");
	if ((css >> 7) & 0x1)
		AuTextOut("[NVMe]: no I/O command sets are supported \n");

	NVMeResetController();

	uint32_t MaxMemoryPageSz = PAGE_SIZE << NVME_CAP_MPSMAX(cap);
	uint32_t MinMemoryPageSz = PAGE_SIZE << NVME_CAP_MPSMIN(cap);
	
	nvme->minPageSize = MinMemoryPageSz;
	nvme->maxPageSize = MaxMemoryPageSz;

	uint16_t MaxQueueEntries = NVME_CAP_MQES(cap) + 1;
	if (MaxQueueEntries <= 0)
		MaxQueueEntries = UINT16_MAX;

	nvme->maxQueueEntries = MaxQueueEntries;
	nvme->doorbellStride = NVME_CAP_DSTRD(cap);

	NVMeSetMemoryPageSize(PAGE_SIZE);
	NVMeSetCommandSet(NVME_CONFIG_COMMAND_SET_NVM);

	uint32_t config = NVMeInl(NVME_REGISTER_CC);
	config |= NVME_CFG_DEFAULT_IOCQES | NVME_CFG_DEFAULT_IOSQES;
	NVMeOutl(NVME_REGISTER_CC, config);

	uint64_t adminSQ = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	memset((void*)adminSQ, 0, PAGE_SIZE);
	uint64_t adminCQ = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	memset((void*)adminCQ, 0, PAGE_SIZE);

	uint64_t adminSQMMIOBase = (uint64_t)AuMapMMIO(adminSQ, 1);
	uint64_t adminCQMMIOBase = (uint64_t)AuMapMMIO(adminCQ, 1);
	
	NVMeOutQ(NVME_REGISTER_ASQ, (adminSQ & UINT64_MAX));
	NVMeOutQ(NVME_REGISTER_ACQ, (adminCQ & UINT64_MAX));

	NVMeSetAdminCQSize(PAGE_SIZE);
	NVMeSetAdminSQSize(PAGE_SIZE);

	config = NVMeInl(NVME_REGISTER_CC);
	config |= ((NVME_CC_AMS_ROUNDROBIN & 0x7) << 11);
	NVMeOutl(NVME_REGISTER_CC, config);

	nvme->NVMeQueueList = initialize_list();
	
	/* setup the admin queue data structure */
	NVMeQueue *adminQe = NVMeCreateQueue(0, PAGE_SIZE-1, PAGE_SIZE-1);
	adminQe->submissionPhysBase = adminSQ;
	adminQe->submissionMMIOBase = adminSQMMIOBase;
	adminQe->completionPhysBase = adminCQ;
	adminQe->completionMMIOBase = adminCQMMIOBase;
	adminQe->submissionQueue = (NVMeCommand*)adminQe->submissionMMIOBase;
	adminQe->completionQueue = (NVMeCompletion*)adminQe->completionMMIOBase;
	adminQe->nvmeCQDoorbell = NVMeGetCompletionDoorbell(0);
	adminQe->nvmeSQDoorbell = NVMeGetSubmissionDoorbell(0);
	
	list_add(nvme->NVMeQueueList, adminQe);

	nvme->numSQEAllocated = 1;
	nvme->numCQEAllocated = 1;
	nvme->queueAllocatedID = 1;

	size_t vector = 77;
	if (!AuPCIEAllocMSI(device, vector, bus, dev, func)) {
		AuTextOut("[NVMe]: failed to allocate MSI/MSI-X interrupt \n");
		return -1;
	}
	setvect(vector, NVMeInterrupt);

	/* enable the controller */
	NVMeEnable();

	/* poll for the ready bit */
	uint32_t status = NVMeInl(NVME_REGISTER_CSTS);
	while (!(status & 0x1))
		status = NVMeInl(NVME_REGISTER_CSTS);

	return 0;
}



/*
* AuDriverUnload -- deattach the driver from
* aurora system
*/
AU_EXTERN AU_EXPORT int AuDriverUnload() {

	return 0;
}

#define MIN(x,y) ((x > y) ? y : x)

/*
* AuDriverMain -- Main entry for nvme driver
*/
AU_EXTERN AU_EXPORT int AuDriverMain() {
	AuDisableInterrupt();
	if (NVMeInitialise() == -1)
		return -1;
	AuEnableInterrupt();

	AuVFSNode* devfs = AuVFSFind("/dev");
	/* Add an entry to device file system */
	char* nvmepath = (char*)kmalloc(32);
	memset(nvmepath, 0, 32);
	AuVDiskCreateStorageFile(nvmepath);
	nvme->nvmedevpath = nvmepath;
	nvme->devfs = devfs;

	NVMeIdentifyController();
	return 0;
}