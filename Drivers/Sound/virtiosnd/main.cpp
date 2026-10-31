/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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
#include <Drivers/uart.h>
#include <aucon.h>
#include <stdint.h>
#include <pcie.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/vmmngr.h>
#include <audrv.h>
#include <Drivers/virtio.h>
#include <Mm/pmmngr.h>
#include <Hal/AA64/gic.h>
#include <string.h>
#include <_null.h>
#include <Sound/sound.h>
#include <Mm/kmalloc.h>

#define VIRTIO_F_VERSION_1		  (1ull << 32)
#define VIRTIO_PCI_CAP_ID		  0x09
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define VIRTQ_DESC_F_NEXT		  1
#define VIRTQ_DESC_F_WRITE		  2

struct virtio_snd_config {
	uint32_t jacks;
	uint32_t streams;
	uint32_t chmaps;
	uint32_t controls;
};

enum {
	/* jack control request types */
	VIRTIO_SND_R_JACK_INFO = 1,
	VIRTIO_SND_R_JACK_REMAP,

	/* pcm control request types */
	VIRTIO_SND_R_PCM_INFO = 0x0100,
	VIRTIO_SND_R_PCM_SET_PARAMS,
	VIRTIO_SND_R_PCM_PREPARE,
	VIRTIO_SND_R_PCM_RELEASE,
	VIRTIO_SND_R_PCM_START,
	VIRTIO_SND_R_PCM_STOP,

	/* channel map control request types */
	VIRTIO_SND_R_CHMAP_INFO = 0x0200,

	VIRTIO_SND_R_PCM_XFER = 0x1000,

	/* control element request types */
	VIRTIO_SND_R_CTL_INFO = 0x0300,
	VIRTIO_SND_R_CTL_ENUM_ITEMS,
	VIRTIO_SND_R_CTL_READ,
	VIRTIO_SND_R_CTL_WRITE,
	VIRTIO_SND_R_CTL_TLV_READ,
	VIRTIO_SND_R_CTL_TLV_WRITE,
	VIRTIO_SND_R_CTL_TLV_COMMAND,

	/* jack event types */
	VIRTIO_SND_EVT_JACK_CONNECTED = 0x1000,
	VIRTIO_SND_EVT_JACK_DISCONNECTED,

	/* PCM event types */
	VIRTIO_SND_EVT_PCM_PERIOD_ELAPSED = 0x1100,
	VIRTIO_SND_EVT_PCM_XRUN,

	/* control element event types */
	VIRTIO_SND_EVT_CTL_NOTIFY = 0x1200,

	/* common status codes */
	VIRTIO_SND_S_OK = 0x8000,
	VIRTIO_SND_S_BAD_MSG,
	VIRTIO_SND_S_NOT_SUPP,
	VIRTIO_SND_S_IO_ERR
};

/* supported PCM stream features */
enum {
	VIRTIO_SND_PCM_F_SHMEM_HOST = 0,
	VIRTIO_SND_PCM_F_SHMEM_GUEST,
	VIRTIO_SND_PCM_F_MSG_POLLING,
	VIRTIO_SND_PCM_F_EVT_SHMEM_PERIODS,
	VIRTIO_SND_PCM_F_EVT_XRUNS
};

/* supported PCM sample formats */
enum {
	/* analog formats (width/ physical width) */
	VIRTIO_SND_PCM_FMT_IMA_ADPCM = 0, // 4 / 4 bits
	VIRTIO_SND_PCM_FMT_MU_LAW,
	VIRTIO_SND_PCM_FMT_A_LAW,
	VIRTIO_SND_PCM_FMT_S8,
	VIRTIO_SND_PCM_FMT_U8,
	VIRTIO_SND_PCM_FMT_S16,
	VIRTIO_SND_PCM_FMT_U16,
	VIRTIO_SND_PCM_FMT_S18_3,
	VIRTIO_SND_PCM_FMT_U18_3,
	VIRTIO_SND_PCM_FMT_S20_3,
	VIRTIO_SND_PCM_FMT_U20_3,
	VIRTIO_SND_PCM_FMT_S24_3,
	VIRTIO_SND_PCM_FMT_U24_3,
	VIRTIO_SND_PCM_FMT_S20,
	VIRTIO_SND_PCM_FMT_U20,
	VIRTIO_SND_PCM_FMT_S24,
	VIRTIO_SND_PCM_FMT_U24,
	VIRTIO_SND_PCM_FMT_S32,
	VIRTIO_SND_PCM_FMT_U32,
	VIRTIO_SND_PCM_FMT_FLOAT,
	VIRTIO_SND_PCM_FMT_FLOAT64,
	VIRTIO_SND_PCM_FMT_DSD_U8,
	VIRTIO_SND_PCM_FMT_DSD_U16,
	VIRTIO_SND_PCM_FMT_DSD_U32,
	VIRTIO_SND_PCM_FMT_IEC958_SUBFRAME,
};

/* supported PCM frame rates */
enum {
	VIRTIO_SND_PCM_RATE_5512 = 0,
	VIRTIO_SND_PCM_RATE_8000,
	VIRTIO_SND_PCM_RATE_11025,
	VIRTIO_SND_PCM_RATE_16000,
	VIRTIO_SND_PCM_RATE_22050,
	VIRTIO_SND_PCM_RATE_32000,
	VIRTIO_SND_PCM_RATE_44100,
	VIRTIO_SND_PCM_RATE_48000,
	VIRTIO_SND_PCM_RATE_64000,
	VIRTIO_SND_PCM_RATE_88200,
	VIRTIO_SND_PCM_RATE_96000,
	VIRTIO_SND_PCM_RATE_176400,
	VIRTIO_SND_PCM_RATE_192000,
	VIRTIO_SND_PCM_RATE_384000,
};

struct virtio_snd_hdr {
	uint32_t code;
};

struct virtio_snd_info {
	uint32_t hda_fn_nid;
};

typedef struct _virtio_snd_query_info_ {
	virtio_snd_hdr hdr;
	uint32_t start_id;
	uint32_t count;
	uint32_t size;
} virtio_snd_query_info;

struct virtio_snd_event {
	struct virtio_snd_hdr;
	uint32_t data;
};

/* jack info response */
typedef struct {
	virtio_snd_hdr hdr;
	uint32_t hda_fn_nid;
	uint32_t hda_pin_default;
	uint32_t features;
	uint8_t conected;
	uint8_t padding[7];
} virtio_snd_jack_info;

struct virtio_snd_pcm_hdr {
	struct virtio_snd_hdr hdr;
	uint32_t stream_id;
};

struct virtio_snd_pcm_info {
	struct virtio_snd_info info_hdr;
	uint32_t features; // 1 << VIRTIO_SND_PCM_F_XXX
	uint64_t formats;  // 1 << VIRTIO_SND_PCM_FMT_XXX
	uint64_t rates;	   // 1 << VIRTIO_SND_PCM_RATE_XXX
	uint8_t directions;
	uint8_t channels_min;
	uint8_t channels_max;
	uint8_t padding[5];
};

struct virtio_snd_pcm_set_params {
	struct virtio_snd_pcm_hdr hdr;
	uint32_t buffer_bytes;
	uint32_t period_bytes;
	uint32_t features; // 1 << VIRTIO_SND_PCM_F_XXX
	uint8_t channels;
	uint8_t format;
	uint8_t rate;
	uint8_t padding;
};

typedef struct _snd_xfer_ {
	//virtio_snd_hdr hdr;
	uint32_t stream_id;
} virtio_snd_pcm_xfer;

typedef struct {
	//virtio_snd_hdr hdr;
	uint32_t status;
	uint32_t latency_bytes;
} virtio_snd_pcm_status;

/** ------------------------------------------------------------------------------------
 * | Low Latency  |  128 frame | 3 Period | 512 (period_bytes) | 1,536 (buffer_bytes)
 * | General      |  512 frame | 4 period | 2048 (period_bytes) | 8,192 (buffer_bytes) 
 * | Media/Battery| 2048 frame | 3 period | 8,192 (period_byte) | 24,576 (buffer_bytes)
 * --------------------------------------------------------------------------------------
 * 
 * Latency profiles
 * |--------------------------------
 * | Low latency | 8ms    | 1 Page
 * | General     | 43 ms  | 2 page
 * |Media/Battery| 128 ms | 6 Pages
 * |---------------------------------
 * 
 * Formulas:
 * frame_bytes = channels (2=stereo) x bytes_per_sample
 * period_bytes = period_frames x frame_bytes
 * byffer_bytes = period_bytes x N_periods
 * latency = buffer_bytes / (rate x frame_bytes) = N x period_frames / rate)
 * period_frames = rate x target_latency / N
 * 
 * Sample bytes value
 * U8/S8 = 1 bytes
 * S16 = 2 bytes
 * S24 = 3 bytes
 * S24 in 32_BIT Container = 4 bytes
 * S32/FLOAT32 = 4 bytes
 * FLOAT64 = 8 bytes 
 * 
 * Let's choose General profile 
 */
#define VIRTIO_PCM_PERIOD 2048 //4608
#define VIRTIO_PCM_BUFFER (VIRTIO_PCM_PERIOD * 4)

static struct VirtioPCIDevice snd_dev;
static struct VirtqDesc* ctrl_desc;
static struct VirtqAvailHdr* ctrl_avail;
static struct VirtqUsedHdr* ctrl_used;
static uint16_t ctrl_qsz;
static uint16_t ctrl_used_idx;
static struct VirtqDesc* tx_desc;
static struct VirtqAvailHdr* tx_avail;
static struct VirtqUsedHdr* tx_used;
static uint16_t tx_qsz;
static uint16_t tx_used_idx;
static void* cmd_virt;
static uint64_t cmd_phys;
static void* resp_virt;
static uint64_t resp_phys;
static void* pcm_virt;
static uint64_t pcm_phys;
static uint32_t output_stream;
static bool dev_ready;
static bool _output_running;

static int queue_wait(struct VirtqUsedHdr* used, uint16_t qsz, uint16_t* last, uint32_t limit) {
	uint32_t spins = 0;
	while (used->idx == *last) {
		dsb_ish();
		if (++spins > limit) {
			UARTDebugOut("[virtio-snd]: queue completion timeout \r\n");
			return 1;
		}
	}
	(*last)++;
	(void)qsz;
	return 0;
}

/* One control message: device-readable request, then a writable status. */
static int snd_ctrl(uint32_t req_len, uint32_t resp_len) {
	volatile virtio_snd_hdr* resp;
	uint16_t slot;
	if (!dev_ready || !ctrl_qsz)
		return 1;
	resp = (volatile virtio_snd_hdr*)resp_virt;
	memset((void*)resp, 0, resp_len);
	dsb_ish();

	ctrl_desc[0].addr = cmd_phys;
	ctrl_desc[0].len = req_len;
	ctrl_desc[0].flags = VIRTQ_DESC_F_NEXT;
	ctrl_desc[0].next = 1;
	ctrl_desc[1].addr = resp_phys;
	ctrl_desc[1].len = resp_len;
	ctrl_desc[1].flags = VIRTQ_DESC_F_WRITE;
	ctrl_desc[1].next = 0;

	slot = (uint16_t)(ctrl_avail->idx % ctrl_qsz);
	ctrl_avail->ring[slot] = 0;
	dsb_ish();
	ctrl_avail->idx = (uint16_t)(ctrl_avail->idx + 1);
	dsb_ish();
	AuVirtioPCINotifyQueue(&snd_dev, 0);
	if (queue_wait(ctrl_used, ctrl_qsz, &ctrl_used_idx, 5000000))
		return 1;
	if (resp->code != VIRTIO_SND_S_OK) {
		UARTDebugOut("[virtio-snd]: control status %x \r\n", resp->code);
		return 1;
	}
	return 0;
}

/* TX chain is xfer header, PCM (device reads), status (device writes).
 * Length is one period so QEMU's virtio-sound returns the buffer. */
static int snd_tx(uint32_t len) {
	virtio_snd_pcm_xfer* xfer;
	virtio_snd_pcm_status* st;
	uint16_t slot;
	if (!dev_ready || !tx_qsz)
		return 1;
	xfer = (virtio_snd_pcm_xfer*)cmd_virt;
	st = (virtio_snd_pcm_status*)resp_virt;
	xfer->stream_id = output_stream;
	memset(st, 0, sizeof(*st));
	dsb_ish();

	tx_desc[0].addr = cmd_phys;
	tx_desc[0].len = sizeof(*xfer);
	tx_desc[0].flags = VIRTQ_DESC_F_NEXT;
	tx_desc[0].next = 1;
	tx_desc[1].addr = pcm_phys;
	tx_desc[1].len = len;
	tx_desc[1].flags = VIRTQ_DESC_F_NEXT;
	tx_desc[1].next = 2;
	tx_desc[2].addr = resp_phys;
	tx_desc[2].len = sizeof(*st);
	tx_desc[2].flags = VIRTQ_DESC_F_WRITE;
	tx_desc[2].next = 0;

	slot = (uint16_t)(tx_avail->idx % tx_qsz);
	tx_avail->ring[slot] = 0;
	dsb_ish();
	tx_avail->idx = (uint16_t)(tx_avail->idx + 1);
	dsb_ish();
	AuVirtioPCINotifyQueue(&snd_dev, 2);
	/* QEMU returns the buffer only after the host has played one period. */
	int wrc = queue_wait(tx_used, tx_qsz, &tx_used_idx, 200000000);
	static bool first_tx_logged = false;
	if (!wrc && !first_tx_logged) {
		first_tx_logged = true;
		UARTDebugOut("[virtio-snd]: first tx status %x (8000 = OK) \r\n", st->status);
	}
	return wrc;
}

static int snd_pcm_set_params(void) {
	virtio_snd_pcm_set_params* parm = (virtio_snd_pcm_set_params*)cmd_virt;
	memset(parm, 0, sizeof(*parm));
	parm->hdr.hdr.code = VIRTIO_SND_R_PCM_SET_PARAMS;
	parm->hdr.stream_id = output_stream;
	parm->buffer_bytes = VIRTIO_PCM_BUFFER;
	parm->period_bytes = VIRTIO_PCM_PERIOD;
	parm->features = 0;
	parm->channels = 2;
	parm->format = VIRTIO_SND_PCM_FMT_S16;
	parm->rate = VIRTIO_SND_PCM_RATE_48000;
	return snd_ctrl(sizeof(*parm), sizeof(virtio_snd_hdr));
}

static int snd_pcm_simple(uint32_t code) {
	virtio_snd_pcm_hdr* pcm = (virtio_snd_pcm_hdr*)cmd_virt;
	memset(pcm, 0, sizeof(*pcm));
	pcm->hdr.code = code;
	pcm->stream_id = output_stream;
	return snd_ctrl(sizeof(*pcm), sizeof(virtio_snd_hdr));
}

/* Opt in to runtime unloading (see audrv.h): AuDriverUnload below undoes every
 * kernel-visible registration this driver makes. */
AU_DRIVER_FLAGS(AU_DRV_FLAG_UNLOADABLE);

static AuSound* g_ausnd = 0;

/*
* AuDriverUnload -- deattach the driver from
* aurora system. Returning non-zero vetoes the unload. Bound audio clients
* are detached by AuSoundUnregisterCard, not treated as a reason to refuse.
*/
AU_EXTERN AU_EXPORT int AuDriverUnload() {
	/* 1. stop the kernel from calling into this image */
	if (g_ausnd) {
		if (AuSoundUnregisterCard(g_ausnd) != 0)
			return 1;
		kfree(g_ausnd);
		g_ausnd = 0;
	}
	dev_ready = false;
	_output_running = false;
	/* 2. reset the device so it stops touching our buffers */
	if (snd_dev.common)
		snd_dev.common->DeviceStatus = 0;
	/* 3. give back the command pages (the kernel also drops their MMIO alias) */
	if (cmd_phys) {
		AuPmmngrReleasePage(cmd_phys);
		cmd_phys = 0;
		cmd_virt = 0;
	}
	if (resp_phys) {
		AuPmmngrReleasePage(resp_phys);
		resp_phys = 0;
		resp_virt = 0;
	}
	if (pcm_phys) {
		AuPmmngrReleasePage(pcm_phys);
		pcm_phys = 0;
		pcm_virt = 0;
	}
	return 0;
}

int virtio_snd_write(uint8_t* buffer, size_t len) {
	if (!dev_ready || !buffer)
		return 1;
	if (len > 8192)
		len = 8192;
	memset(pcm_virt, 0, 8192);
	memcpy(pcm_virt, buffer, len);
	return snd_tx(len);
}

int virtio_snd_read(uint8_t* buffer, size_t len) {
	(void)buffer;
	(void)len;
	return 0;
}

int virtio_snd_output_stop() {
	if (!dev_ready)
		return 1;
	if (!_output_running)
		return 0;
	if (snd_pcm_simple(VIRTIO_SND_R_PCM_STOP)) {
		UARTDebugOut("[virtio-snd]: failed to stop output stream \r\n");
		return 1;
	}
	_output_running = false;
	return 0;
}

int virtio_snd_output_start() {
	if (!dev_ready)
		return 1;
	if (_output_running)
		return 0;
	if (snd_pcm_simple(VIRTIO_SND_R_PCM_PREPARE) || snd_pcm_simple(VIRTIO_SND_R_PCM_START)) {
		UARTDebugOut("[virtio-snd]: failed to start output stream \r\n");
		return 1;
	}
	_output_running = true;
	return 0;
}

int virtio_snd_set_vol(uint8_t vol) {
	(void)vol;
	return 0;
}

static int snd_alloc_page(uint64_t* phys, void** virt) {
	*phys = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	if (!*phys)
		return 1;
	*virt = AuMapMMIO(*phys, 1);
	if (!*virt)
		return 1;
	memset(*virt, 0, PAGE_SIZE);
	return 0;
}

static int snd_alloc_pages(uint64_t* phys, void** virt, int num_pages) {
	uint64_t p1 = 0;
	for (int i = 0; i < num_pages; i++) {
		uint64_t p = AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		void* v = AuMapMMIO(p, 1);
		if (p1 == 0) {
			p1 = p;
			*virt = v;
		}
	}
	if (p1 == 0)
		return 1;
	*phys = p1;
	memset(*virt, 0, PAGE_SIZE * num_pages);
	return 0;
}
/*
* AuDriverMain -- virtio-sound entry
*/
AU_EXTERN AU_EXPORT int AuDriverMain(AuDriver* drv) {
	struct VirtqDesc* d;
	struct VirtqAvailHdr* a;
	struct VirtqUsedHdr* u;
	uint16_t q;
	int i;
	AuTextOut("[virtio-sound]: initializing virtio sound driver \r\n");
	AuTextOut("bus : %d, dev : %d, func : %d \r\n", drv->bus, drv->dev, drv->func);
	int bus = drv->bus;
	int dev = drv->dev;
	int func = drv->func;
	dev_ready = false;
	_output_running = false;
	output_stream = 0;
	ctrl_used_idx = 0;
	tx_used_idx = 0;

	uint64_t device = AuPCIEScanClass(drv->classCode, drv->subClassCode, &bus, &dev, &func);
	if (!device || device == 0xFFFFFFFFFFFFFFFFULL) {
		UARTDebugOut("[virtio-snd]: pci scan failed \r\n");
		return 1;
	}
	uint16_t command = AuPCIERead(device, PCI_COMMAND, bus, dev, func);
	command |= 7;
	AuPCIEWrite(device, PCI_COMMAND, command, bus, dev, func);
	isb_flush();
	dsb_ish();

	/* Modern virtio: FEATURES_OK before queues, separate desc/avail/used
	 * rings. The old single-page VirtioQueue never committed VERSION_1,
	 * so QEMU's virtio-sound (disable-legacy) never played a period and
	 * PulseAudio stayed silent. */
	if (!AuVirtioPCIInit(device, bus, dev, func, 0, &snd_dev)) {
		UARTDebugOut("[virtio-snd]: feature negotiation failed \r\n");
		return 1;
	}
	ctrl_qsz = AuVirtioPCISetupQueue(
		&snd_dev, 0, &ctrl_desc, &ctrl_avail, &ctrl_used, VIRTIO_MSI_NO_VECTOR);
	q = AuVirtioPCISetupQueue(&snd_dev, 1, &d, &a, &u, VIRTIO_MSI_NO_VECTOR);
	tx_qsz =
		AuVirtioPCISetupQueue(&snd_dev, 2, &tx_desc, &tx_avail, &tx_used, VIRTIO_MSI_NO_VECTOR);
	q = AuVirtioPCISetupQueue(&snd_dev, 3, &d, &a, &u, VIRTIO_MSI_NO_VECTOR);
	(void)q;
	if (!ctrl_qsz || !tx_qsz || !snd_dev.deviceCfg) {
		UARTDebugOut("[virtio-snd]: queue setup failed \r\n");
		snd_dev.common->DeviceStatus |= VIRTIO_STATUS_FAILED;
		return 1;
	}
	snd_dev.common->DeviceStatus |= VIRTIO_STATUS_DRIVER_OK;
	isb_flush();
	dsb_ish();

	if (snd_alloc_page(&cmd_phys, &cmd_virt) || snd_alloc_page(&resp_phys, &resp_virt)) {
		UARTDebugOut("[virtio-snd]: buffer allocation failed \r\n");
		return 1;
	}

	if (snd_alloc_pages(&pcm_phys, &pcm_virt, 2)) {
		UARTDebugOut("[virtio-snd]: failed to allocate PCM buffer \r\n");
	}
	dev_ready = true;

	volatile uint32_t* sc = (volatile uint32_t*)snd_dev.deviceCfg;
	uint32_t n_streams = sc[1];
	UARTDebugOut("[virtio-snd]: streams %d \r\n", n_streams);
	if (n_streams == 0 || n_streams > 8) {
		UARTDebugOut("[virtio-snd]: unexpected stream count \r\n");
		dev_ready = false;
		return 1;
	}

	virtio_snd_query_info* qinfo = (virtio_snd_query_info*)cmd_virt;
	memset(qinfo, 0, sizeof(*qinfo));
	qinfo->hdr.code = VIRTIO_SND_R_PCM_INFO;
	qinfo->start_id = 0;
	qinfo->count = n_streams;
	qinfo->size = sizeof(virtio_snd_pcm_info);
	if (snd_ctrl(sizeof(*qinfo),
				 sizeof(virtio_snd_hdr) + sizeof(virtio_snd_pcm_info) * n_streams)) {
		UARTDebugOut("[virtio-snd]: pcm info query failed \r\n");
		dev_ready = false;
		return 1;
	}
	volatile virtio_snd_pcm_info* pcm =
		(volatile virtio_snd_pcm_info*)((uint8_t*)resp_virt + sizeof(virtio_snd_hdr));
	output_stream = 0;
	for (i = 0; i < (int)n_streams; i++) {
		UARTDebugOut("[virtio-snd]: stream %d dir %d \r\n", i, pcm[i].directions);
		/* virtio-snd spec: this field is a direction ENUM (D_OUTPUT = 0,
		 * D_INPUT = 1), not a bitmask. `& 1` selected the INPUT stream, and
		 * the device rejected every playback buffer with BAD_MSG. */
		if (pcm[i].directions == 0) {
			output_stream = (uint32_t)i;
			break;
		}
	}

	if (snd_pcm_set_params()) {
		UARTDebugOut("[virtio-snd]: failed to set output stream parameters \r\n");
		dev_ready = false;
		return 1;
	}
	if (snd_pcm_simple(VIRTIO_SND_R_PCM_PREPARE) || snd_pcm_simple(VIRTIO_SND_R_PCM_START)) {
		UARTDebugOut("[virtio-snd]: failed to start output stream \r\n");
		dev_ready = false;
		return 1;
	}
	_output_running = true;
	UARTDebugOut("[virtio-snd]: output stream %d started \r\n", output_stream);

	AuSound* ausnd = (AuSound*)kmalloc(sizeof(AuSound));
	memset(ausnd, 0, sizeof(AuSound));
	strcpy(ausnd->name, "virtio-sound");
	ausnd->read = &virtio_snd_read;
	ausnd->write = &virtio_snd_write;
	ausnd->stop_output = &virtio_snd_output_stop;
	ausnd->start_output = &virtio_snd_output_start;
	ausnd->set_vol = &virtio_snd_set_vol;
	ausnd->_force_write = 1;
	if (AuSoundRegisterCard(ausnd)) {
		UARTDebugOut("[aurora]: failed to register sound card : %s \r\n", ausnd->name);
		kfree(ausnd);
		dev_ready = false;
		return 1;
	}
	g_ausnd = ausnd;
	UARTDebugOut("[virtio-snd]: initialized successfully \r\n");
	return 0;
}
