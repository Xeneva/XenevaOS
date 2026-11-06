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

#ifndef __NVME_H__
#define __NVME_H__

#include <stdint.h>
#include <list.h>
#include <Fs/vfs.h>
#include <aurora.h>

#pragma pack(push,1)
typedef struct _nvme_identify_cmd_ {
	enum {
		CNSNamespace = 0,
		CNSContoller = 1,
		CNSNamespaceList = 2,
	};
#pragma pack(push,1)
	struct {
		uint32_t cns : 8;
		uint32_t reserved : 8;
		uint32_t cntID : 16;
	};
#pragma pack(pop)
	uint32_t nvmSetID;
}NVMeIdentifyCommand;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _nvme_create_io_comp_queue_cmd_ {
#pragma pack(push,1)
	struct {
		uint32_t queueID : 16;
		uint32_t queueSize : 16;
	};
#pragma pack(pop)

#pragma pack(push,1)
	struct {
		uint32_t contiguous : 1;
		uint32_t intEnable : 1;
		uint32_t reserved : 14;
		uint32_t intVector : 16;
	};
#pragma pack(pop)
}NVMeCreateIOCompletionQueueCommand;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _nvme_create_io_sub_queue_{
#pragma pack(push,1)
	struct {
		uint32_t queueID : 16;
		uint32_t queueSize : 16;
	};
#pragma pack(pop)

#pragma pack(push,1)
	struct {
		uint32_t contiguous : 1;
		uint32_t priority : 2;
		uint32_t reserved : 13;
		uint32_t cqID : 16;
	};
#pragma pack(pop)
}NVMeCreateIOSubmissionQueueCommand;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _nvme_delete_io_queue_cmd_ {
	uint32_t queueID;
}NVMeDeleteIOQueueCommand;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _nvme_set_features_cmd_ {
	enum{
		FeatureIDNumberOfQueues = 0x7,
	};
#pragma pack(push,1)
	struct {
		uint32_t featureID : 8;
		uint32_t reserved : 23;
		uint32_t save : 1;
	};
#pragma pack(pop)
	uint32_t dw11;
	uint32_t dw12;
	uint32_t dw13;
}NVMeSetFeatureCommand;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _NVMeReadCommand_ {
	uint64_t startLBA;
#pragma pack(push,1)
	struct {
		uint32_t blockNum : 16;
		uint32_t reserved : 10;
		uint32_t prInfo : 4;
		uint32_t forceUnitAccess : 1;
		uint32_t limitedRetry : 1;
	};
}NVMeReadCommand;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _NVMeWriteCommand_ {
	uint64_t startLBA;
#pragma pack(push,1)
	struct {
		uint32_t blockNum : 16;
		uint32_t reserved2 : 4;
		uint32_t directiveType : 4;
		uint32_t reserved : 2;
		uint32_t prInfo : 4;
		uint32_t forceUnitAccess : 1;
		uint32_t limitedRetry : 1;
	};
#pragma pack(pop)
}NVMeWriteCommand;
#pragma pack(pop)


#pragma pack(push,1)
typedef struct _nvme_cmd_ {
#pragma pack(push,1)
	struct {
		uint32_t opcode : 8;
		uint32_t fuse : 2;
		uint32_t reserved : 4;
		uint32_t psdt : 2;
		uint32_t commandID : 16;
	};
#pragma pack(pop)
	uint32_t nsid;
	uint64_t resv;
	uint64_t metadataPtr;
	uint64_t prp1;
	uint64_t prp2;
	union {
		struct{
			uint32_t cmdDwords[6];
		};
		NVMeIdentifyCommand identify;
		NVMeCreateIOCompletionQueueCommand createIOCQ;
		NVMeCreateIOSubmissionQueueCommand createIOSQ;
		NVMeDeleteIOQueueCommand deleteIOQ;
		NVMeSetFeatureCommand setFeatures;
		NVMeReadCommand read;
		NVMeWriteCommand write;
	};
}NVMeCommand;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _nvme_completion_ {
	uint32_t dw0;
	uint32_t reserved;
#pragma pack(push,1)
	struct {
		uint32_t sqHead : 16;
		uint32_t sqID : 16;
	};
#pragma pack(pop)

#pragma pack(push,1)
	struct {
		uint32_t commandID : 16;
		uint32_t phaseTag : 1;
		uint32_t status : 15;
	};
#pragma pack(pop)
}NVMeCompletion;
#pragma pack(pop)

enum AdminCommands {
	AdminCmdDeleteIOSubmissionQueue = 0x0,
	AdminCmdCreateIOSubmissionQueue = 0x1,
	AdminCmdGetLogPage = 0x2,
	AdminCmdDeleteIOCompletionQueue = 0x4,
	AdminCmdCreateIOCompletionQueue = 0x5,
	AdminCmdIdentify = 0x6,
	AdminCmdSetFeatures = 0x9,
};


typedef volatile struct _controller_identity_ {
	uint16_t vendorID;
	uint16_t subsystemVendorID;
	int8_t serialNumber[20];
	int8_t modelNumber[40];
	int8_t firmwareRevision[8];
	uint8_t recommendedArbitrationBurst;
	uint8_t ieee[3];
	uint8_t cmic;
	uint8_t maximumDataTransferSize;
	uint16_t controllerID;
	uint32_t version;
	uint32_t rtd3ResumeLatency;
	uint32_t rtd3EntryLatency;
	uint32_t oaes;
	uint32_t controllerAttributes;
	uint16_t rrls;
	uint8_t reserved[9];
	uint8_t controllerType;
	uint8_t fGUID[16];
	uint16_t crdt[3];
	uint8_t reserved2[122];
	uint16_t oacs;
	uint8_t acl;
	uint8_t aerl;
	uint8_t firmwareUpdates;
	uint8_t logPageAttributes;
	uint8_t errorLogPageEntries;
	uint8_t numberOfPowerStates;
	uint8_t apsta;
	uint16_t wcTemp;
	uint16_t ccTemp;
	uint16_t mtfa;
	uint32_t hostMemoryBufferPreferredSize;
	uint32_t hostMemoryBufferMinimumSize;
	uint8_t unused[232];
	uint8_t sqEntrySize;
	uint8_t cqEntrySize;
	uint16_t maxCmd;
	uint32_t numNamespaces;
	uint8_t unused2[248];
	int8_t name[256];
	uint8_t unused3[3072];
}NVMeControllerIdentity;
//static_assert(sizeof(NVMeControllerIdentity) == 4096,"NVMe packing error !");

union NVMeLBAFormat{
	uint32_t dw;
#pragma pack(push,1)
	struct {
		uint32_t metadataSize : 16;
		uint32_t lbaDataSize : 8;
		uint32_t relativePerformance : 2;
		uint32_t reserved : 6;
	};
#pragma pack(pop)
};

#pragma pack(push,1)
typedef struct _namespace_id_ {
	uint64_t namespaceSize;
	uint64_t nsCap;
	uint64_t nsUse;
	uint8_t nsFeat;
	uint8_t numLBAFormat;
	uint8_t fmtLBASize;
	uint8_t unused[101];
	NVMeLBAFormat lbaFormat[16];
	uint8_t reserved[192];
	uint8_t vendor[3712];
}NamespaceIdentity;
#pragma pack(pop)

typedef struct _nvme_queue_ {
	uint16_t queueId;
	uint64_t completionMMIOBase;
	uint64_t submissionMMIOBase;
	uint64_t completionPhysBase;
	uint64_t submissionPhysBase;

	NVMeCompletion* completionQueue;
	NVMeCommand* submissionQueue;

	uint32_t nvmeCQDoorbell;
	uint32_t nvmeSQDoorbell;

	uint16_t cq_size;
	uint16_t sq_size;
	uint16_t cq_count;
	uint16_t sq_count;
	uint16_t sq_tail;
	uint16_t cq_head;
	bool completion_cycle_state;
	uint16_t nextCommandId;
}NVMeQueue;

typedef struct _nvme_dev_ {
	char* nvmedevpath;
	AuVFSNode* devfs;
	uint64_t mmiobase;
	uint8_t majorVersion;
	uint8_t minorVersion;
	uint16_t maxQueueEntries;
	uint32_t minPageSize;
	uint32_t maxPageSize;
	list_t* NVMeQueueList;
	uint32_t doorbellStride;
	uint16_t numCQEAllocated;
	uint16_t numSQEAllocated;
	uint16_t queueAllocatedID;
}NVMeDev;

#define NVME_REGISTER_CAP 0x00
#define NVME_REGISTER_VS  0x08 //version
#define NVME_REGISTER_INTMS 0x0C //interrupt mask set
#define NVME_REGISTER_INTMC 0x10 //interrupt mask clear
#define NVME_REGISTER_CC 0x14  //controller configuration
#define NVME_REGISTER_CSTS 0x1C //controller status
#define NVME_REGISTER_AQA 0x24 //Admin queue attributes
#define NVME_REGISTER_ASQ 0x28 //Admin submission queue
#define NVME_REGISTER_ACQ 0x30 //Admin completion queue

#define NVME_CC_DISABLE   0
#define NVME_CC_EN        0x1
#define NVME_CC_EN_MASK   0x1
#define NVME_CC_CSNVME    0x0
#define NVME_CC_CSS_MASK  0x70
#define NVME_CC_MPS_MASK  0x780
#define NVME_CC_MPS_SHIFT 7
#define NVME_CC_AMS_ROUNDROBIN  0
#define NVME_CC_AMS_WIGHTED     (1<<11)
#define NVME_CC_AMS_MASK        (0x7 << 11)

#define NVME_CAP_MPS_MASK 0xfU
#define NVME_CAP_MPSMAX(x) ((x >> 52) & NVME_CAP_MPS_MASK)
#define NVME_CAP_MPSMIN(x) ((x >> 48) & NVME_CAP_MPS_MASK)

#define NVME_CAP_MQES_MASK 0xffffU
#define NVME_CAP_MQES(x) ((x) & NVME_CAP_MQES_MASK)

#define NVME_CFG_MPS_MASK 0xfUL
#define NVME_CFG_MPS(x)  (((x) & NVME_CFG_MPS_MASK) << 7)
#define NVME_CFG_CSS_MASK 0x7
#define NVME_CFG_CSS(x) (((x) & NVME_CFG_CSS_MASK) << 4)
#define NVME_CFG_ENABLE (1<<0)

#define NVME_CONFIG_COMMAND_SET_NVM 0x0
#define NVME_CONFIG_COMMAND_ALL_SUPPORTED_IO 0x6
#define NVME_CONFIG_COMMAND_SET_ADMIN_ONLY 0x7

#define NVME_CFG_DEFAULT_IOCQES (4<<20)
#define NVME_CFG_DEFAULT_IOSQES (6<<16)

#define NVME_AQA_AQS_MASK 0xfffU
#define NVME_AQA_ACQS(x) (((x) & NVME_AQA_AQS_MASK) << 16)
#define NVME_AQA_ASQS(x) (((x) & NVME_AQA_AQS_MASK) << 0)

#define NVME_CAP_DSTRD_MASK 0xfU
#define NVME_CAP_DSTRD(x) (((x) >> 32) & NVME_CAP_DSTRD_MASK)

#define NVME_NSSR_RESET_VALUE 0x4e564d65


/* NVMeGetQueue -- returns a queue specified by queue id
* @param queueid -- NVMe Queue ID number
*/
extern NVMeQueue* NVMeGetQueue(uint16_t queueid);
/*
* NVMeSubmitCommand -- submits commands and waits for completion data
* @param queue -- NVMe Queue data structure
* @param cmd -- NVMe command
* @param comp -- Completion data structure
*/
extern void NVMeSubmitCommand(NVMeQueue *queue, NVMeCommand* cmd, NVMeCompletion* comp);


#endif