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

#define MINIMP4_IMPLEMENTATION 1
#include "minimp4.h"
#include <arm_neon.h>
#include <chitralekha.h>
#include <widgets/window.h>
#include <widgets/base.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <pthread.h>
extern "C" {
#include "h264bsd/h264bsd_decoder.h"
#include "libhelix-aac/aacdec.h"
}
#include <stdio.h>
#include <string.h>

typedef struct {
	FILE* f;
} Mp4File;

ChitralekhaApp* app;
ChWindow* mainWin;
int sound;

#define FRAMEBUFFER_WIDTH  700
#define FRAMEBUFFER_HEIGHT 500

/**
 *  TODO: proper UI and refactoring
 *  
 */

static const int16_t kYMul = 74;   /* 1.164 * 64 */
static const int16_t kRVMul = 102; /* 1.596 * 64, uses V only for R */
static const int16_t kGUMul = -25; /* -0.391 * 64 */
static const int16_t kGVMul = -52; /* -0.813 * 64 */
static const int16_t kBUMul = 129; /* 2.018 * 64 */
static HAACDecoder aac_dec = nullptr;

static inline uint64_t now_us() {
	uint64_t cnt, frq;
	__asm__ volatile("isb\n mrs %0, cntvct_el0" : "=r"(cnt));
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
	return (cnt / frq) * 1000000ULL + ((cnt % frq) * 1000000ULL) / frq;
}

static void init_audio_decoder(MP4D_track_t* at) {
	aac_dec = AACInitDecoder();

	AACFrameInfo frameinfo;
	memset(&frameinfo, 0, sizeof(frameinfo));
	frameinfo.nChans = at->SampleDescription.audio.channelcount;
	_KePrint("Num channel count : %d \r\n", at->SampleDescription.audio.channelcount);
	frameinfo.sampRateCore = at->SampleDescription.audio.samplerate_hz;
	frameinfo.profile = AAC_PROFILE_LC;
	AACSetRawBlockParams(aac_dec, 0, &frameinfo);
}

static uint32_t decode_audio_sample(const uint8_t* data, uint32_t size) {
	static int16_t pcm[4096];
	unsigned char* inbuf = (unsigned char*)data;
	int bytes_left = (int)size;
	uint32_t frames = 0;

	while (bytes_left > 0) {
		int err = AACDecode(aac_dec, &inbuf, &bytes_left, pcm);
		if (err != ERR_AAC_NONE) {
			_KePrint("aac decode error: %d \r\n", err);
			break;
		}

		AACFrameInfo info;
		AACGetLastFrameInfo(aac_dec, &info);
		size_t pcm_bytes = (size_t)info.outputSamps * sizeof(int16_t);
		_KeWriteFile(sound, pcm, pcm_bytes);
		frames += info.outputSamps / info.nChans;
	}
	return frames;
}
void yuv420_to_bgra_neon(const uint8_t* y_plane,
						 int y_stride,
						 const uint8_t* u_plane,
						 int u_stride,
						 const uint8_t* v_plane,
						 int v_stride,
						 uint8_t* dst,
						 int dst_stride,
						 int width,
						 int height) {
	int16x8_t yc = vdupq_n_s16(kYMul);
	int16x8_t rv = vdupq_n_s16(kRVMul);
	int16x8_t gu = vdupq_n_s16(kGUMul);
	int16x8_t gv = vdupq_n_s16(kGVMul);
	int16x8_t bu = vdupq_n_s16(kBUMul);
	uint8x8_t alpha = vdup_n_u8(0xFF);

	for (int row = 0; row < height; row += 2) {
		const uint8_t* yrow0 = y_plane + row * y_stride;
		const uint8_t* yrow1 = yrow0 + y_stride;
		const uint8_t* urow = u_plane + (row / 2) * u_stride;
		const uint8_t* vrow = v_plane + (row / 2) * v_stride;
		uint8_t* drow0 = dst + row * dst_stride;
		uint8_t* drow1 = drow0 + dst_stride;

		for (int col = 0; col < width; col += 16) {
			uint8x8_t u8v = vld1_u8(urow + col / 2);
			uint8x8_t v8v = vld1_u8(vrow + col / 2);
			int16x8_t u16 = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(u8v)), vdupq_n_s16(128));
			int16x8_t v16 = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(v8v)), vdupq_n_s16(128));

			int16x8_t rd = vshrq_n_s16(vmulq_s16(v16, rv), 6);
			int16x8_t gd = vshrq_n_s16(vaddq_s16(vmulq_s16(u16, gu), vmulq_s16(v16, gv)), 6);
			int16x8_t bd = vshrq_n_s16(vmulq_s16(u16, bu), 6);

			/* U/V deltas apply to a 2x2 luma block -> duplicate each lane
             * so we get 16 deltas covering 16 luma samples. */
			int16x8x2_t rd2 = vzipq_s16(rd, rd);
			int16x8x2_t gd2 = vzipq_s16(gd, gd);
			int16x8x2_t bd2 = vzipq_s16(bd, bd);

			const uint8_t* yrows[2] = {yrow0, yrow1};
			uint8_t* drows[2] = {drow0, drow1};

			for (int r = 0; r < 2; r++) {
				uint8x16_t y8 = vld1q_u8(yrows[r] + col);
				int16x8_t yl16 = vshrq_n_s16(
					vmulq_s16(vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(y8))),
										vdupq_n_s16(16)),
							  yc),
					6);
				int16x8_t yh16 = vshrq_n_s16(
					vmulq_s16(vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(y8))),
										vdupq_n_s16(16)),
							  yc),
					6);

				uint8x8_t rL = vqmovun_s16(vaddq_s16(yl16, rd2.val[0]));
				uint8x8_t gL = vqmovun_s16(vaddq_s16(yl16, gd2.val[0]));
				uint8x8_t bL = vqmovun_s16(vaddq_s16(yl16, bd2.val[0]));
				uint8x8_t rH = vqmovun_s16(vaddq_s16(yh16, rd2.val[1]));
				uint8x8_t gH = vqmovun_s16(vaddq_s16(yh16, gd2.val[1]));
				uint8x8_t bH = vqmovun_s16(vaddq_s16(yh16, bd2.val[1]));

				uint8x8x4_t pixL = {bL, gL, rL, alpha};
				uint8x8x4_t pixH = {bH, gH, rH, alpha};
				vst4_u8(drows[r] + col * 4, pixL);
				vst4_u8(drows[r] + (col + 8) * 4, pixH);
			}
		}
	}
}

static int mp4_read_cb(int64_t offset, void* buffer, size_t size, void* token) {
	Mp4File* mf = (Mp4File*)token;

	if (fseek(mf->f, (long)offset, SEEK_SET) != 0)
		return 1;

	size_t got = fread(buffer, 1, size, mf->f);
	if (got != size) {
		if (got > 0)
			memset((uint8_t*)buffer + got, 0, size - got);
		else
			return 1;
	}
	return 0;
}

void blit_yuv420_to_fb(const uint8_t* y_plane,
					   int y_stride,
					   const uint8_t* u_plane,
					   int u_stride,
					   const uint8_t* v_plane,
					   int v_stride,
					   int vid_w,
					   int vid_h,
					   uint32_t* fb,
					   int fb_width,
					   int fb_height) {
	static uint32_t* frame_buf = nullptr;
	static int cached_w = 0, cached_h = 0;

	if (!frame_buf || cached_w != vid_w || cached_h != vid_h) {
		if (frame_buf)
			free(frame_buf);
		frame_buf = (uint32_t*)malloc((size_t)vid_w * vid_h * 4);
		cached_w = vid_w;
		cached_h = vid_h;
	}

	yuv420_to_bgra_neon(y_plane,
						y_stride,
						u_plane,
						u_stride,
						v_plane,
						v_stride,
						(uint8_t*)frame_buf,
						vid_w * 4,
						vid_w,
						vid_h);
	const int fb_stride_pixels = mainWin->info->width;
	int dst_x = (fb_width - vid_w) / 2;
	if (dst_x < 0)
		dst_x = 0;
	int dst_y = (fb_height - vid_h) / 2;
	if (dst_y < 0)
		dst_y = 0;

	int copy_w = (vid_w < fb_width - dst_x) ? vid_w : (fb_width - dst_x);
	int copy_h = (vid_h < fb_height - dst_y) ? vid_h : (fb_height - dst_y);

	for (int row = 0; row < copy_h; row++) {
		uint32_t* src_row = frame_buf + (size_t)row * vid_w;
		uint32_t* dst_row = fb + (size_t)(dst_y + row) * fb_stride_pixels + dst_x;
		memcpy(dst_row, src_row, copy_w * sizeof(uint32_t));
	}
}

void test_demux(const char* path) {
	printf("test_demux : %s \r\n", path);
	Mp4File mf;
	mf.f = fopen(path, "rb");
	if (!mf.f) {
		printf("mp4: failed to open %s\n", path);
		return;
	}

	fseek(mf.f, 0, SEEK_END);
	int64_t file_size = ftell(mf.f);
	fseek(mf.f, 0, SEEK_SET);

	MP4D_demux_t mp4;
	if (!MP4D_open(&mp4, mp4_read_cb, &mf, file_size)) {
		printf("mp4: MP4D_open failed (bad/unsupported file) \r\n");
		fclose(mf.f);
		return;
	}

	printf("mp4: %d tracks(s)\n", mp4.track_count);

	int video_track = -1;
	for (int i = 0; i < mp4.track_count; i++) {
		MP4D_track_t* t = &mp4.track[i];
		printf(" track %d: type=%d samples=%u\r\n", i, t->object_type_indication, t->sample_count);

		if (t->object_type_indication == MP4_OBJECT_TYPE_AVC) {
			video_track = i;
			printf("    video %ux%u\n",
				   t->SampleDescription.video.width,
				   t->SampleDescription.video.height);
		}
	}

	if (video_track < 0) {
		printf("mp4: no H.264 track found \n");
		MP4D_close(&mp4);
		fclose(mf.f);
		return;
	}

	MP4D_track_t* vt = &mp4.track[video_track];
	unsigned max_samples = vt->sample_count < 5 ? vt->sample_count : 5;

	uint8_t buf[65536];
	for (unsigned s = 0; s < max_samples; s++) {
		unsigned frame_bytes = 0, timestamp = 0, duration = 0;
		MP4D_file_offset_t off =
			MP4D_frame_offset(&mp4, video_track, s, &frame_bytes, &timestamp, &duration);

		if (frame_bytes > sizeof(buf)) {
			printf(" sample %u: too big (%u bytes), skipping\n", s, frame_bytes);
			continue;
		}

		if (mp4_read_cb((int64_t)off, buf, frame_bytes, &mf) != 0) {
			printf(" sample %u: read failed\n");
			continue;
		}

		printf("  sample %u: %u bytes, pts=%u\n", s, frame_bytes, timestamp);

		size_t p = 0;
		while (p + 4 <= frame_bytes) {
			uint32_t nal_len = (buf[p] << 24) | (buf[p + 1] << 16) | (buf[p + 2] << 8) | buf[p + 3];
			p += 4;
			if (p + nal_len > frame_bytes)
				break;
			uint8_t nal_type = buf[p] & 0x1F;
			printf("    NAL type=%u len=%u\n", nal_type, nal_len);
			p += nal_len;
		}
	}

	MP4D_close(&mp4);
	fclose(mf.f);
}

static void feed_nal(storage_t* h264, const uint8_t* nal, uint32_t len, bool present) {
	static uint8_t tmp[1024 * 1024];
	static const uint8_t start_code[4] = {0, 0, 0, 1};
	if (len + 4 > sizeof(tmp))
		return;

	uint8_t nal_type = nal[0] & 0x1F;

	memcpy(tmp, (void*)start_code, 4);
	memcpy(tmp + 4, (void*)nal, len);
	u32 read_bytes = 0;
	u32 status = h264bsdDecode(h264, tmp, len + 4, pic_id, &read_bytes);

	while (status == H264BSD_HDRS_RDY) {
		status = h264bsdDecode(h264, tmp, len + 4, pic_id, &read_bytes);
	}

	if (status == H264BSD_PIC_RDY) {
		u32 pic_id_out, is_idr, num_err_mbs;
		u8* pic = h264bsdNextOutputPicture(h264, &pic_id_out, &is_idr, &num_err_mbs);
		if (pic && present) {
			u32 w = h264bsdPicWidth(h264) * 16;
			u32 h = h264bsdPicHeight(h264) * 16;
			//printf("frame ready> %ux%u\n", w, h);

			uint8_t* y_plane = pic;
			uint8_t* u_plane = pic + (w * h);
			uint8_t* v_plane = u_plane + ((w / 2) * (h / 2));

			blit_yuv420_to_fb(y_plane,
							  w,
							  u_plane,
							  w / 2,
							  v_plane,
							  w / 2,
							  w,
							  h,
							  mainWin->canv->buffer,
							  FRAMEBUFFER_WIDTH,
							  FRAMEBUFFER_HEIGHT);

			ChWindowUpdate(mainWin, 0, 0, FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT, 0, 1);
		}

	} else if (status == H264BSD_ERROR || status == H264BSD_PARAM_SET_ERROR) {
		_KePrint("decode error : status = %d\r\n", status);
	}
}

static void feed_avcc_params(storage_t* h264, MP4D_track_t* vt) {
	const uint8_t* dsi = vt->dsi;
	uint32_t n = vt->dsi_bytes;

	printf("dsi=%p, dsi_bytes =%u\n", (void*)dsi, n);
	if (!dsi || n < 6) {
		printf("no avCC / dsi found \n");
		return;
	}

	for (uint32_t i = 0; i < n; i++) {
		printf("%02X ", dsi[i]);
		if ((i + 1) % 16 == 0)
			printf("\n");
	}
	printf("n");

	uint32_t p = 0;
	uint8_t num_sps = dsi[p++] & 0x1F;
	printf("num_sps=%u\n", num_sps);
	for (uint8_t i = 0; i < num_sps && p + 2 <= n; i++) {
		uint16_t sps_len = (dsi[p] << 8) | dsi[p + 1];
		p += 2;
		if (p + sps_len > n)
			break;
		printf(
			"SPS[%u] len=%u nal_type=%u profile_idc=%u\n", i, sps_len, dsi[p] & 0x1F, dsi[p + 1]);
		feed_nal(h264, dsi + p, sps_len, 0);
		p += sps_len;
	}

	if (p >= n) {
		printf("ran out of dsi bytes before PPS \n");
		return;
	}
	uint8_t num_pps = dsi[p++];
	printf("num pps=%u\n", num_pps);
	for (uint8_t i = 0; i < num_pps && p + 2 <= n; i++) {
		uint16_t pps_len = (dsi[p] << 8) | dsi[p + 1];
		p += 2;
		if (p + pps_len > n)
			break;
		printf("PPS[%u] len=%u nal_type=%u\n", i, pps_len, dsi[p] & 0x1F);
		feed_nal(h264, dsi + p, pps_len, 0);
		p += pps_len;
	}
}

void play_video(const char* path) {
	Mp4File mf;
	mf.f = fopen(path, "rb");
	if (!mf.f) {
		printf("failed to open %s \r\n", path);
		return;
	}

	fseek(mf.f, 0, SEEK_END);
	int64_t file_size = ftell(mf.f);
	fseek(mf.f, 0, SEEK_SET);

	MP4D_demux_t mp4;
	if (!MP4D_open(&mp4, mp4_read_cb, &mf, file_size)) {
		printf("MP4D_open failed \n");
		fclose(mf.f);
		return;
	}

	int video_track = -1;
	for (int i = 0; i < mp4.track_count; i++) {
		if (mp4.track[i].object_type_indication == MP4_OBJECT_TYPE_AVC) {
			video_track = i;
			break;
		}
	}

	int audio_track = -1;
	for (int i = 0; i < mp4.track_count; i++) {
		if (mp4.track[i].object_type_indication == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3) {
			audio_track = i;
			break;
		}
	}

	if (video_track < 0) {
		printf("no H.264 track \n");
		MP4D_close(&mp4);
		fclose(mf.f);
		return;
	}

	storage_t h264;
	if (h264bsdInit(&h264, 0) != 0) {
		printf("h264bsdInit failed \n");
		MP4D_close(&mp4);
		fclose(mf.f);
		return;
	}

	MP4D_track_t* vt = &mp4.track[video_track];
	feed_avcc_params(&h264, vt);

	MP4D_track_t* at = nullptr;
	if (audio_track >= 0) {
		at = &mp4.track[audio_track];
		init_audio_decoder(at);
	}
	//uint8_t buf[65536];
	unsigned v_sample = 0, a_sample = 0;

	uint8_t* video_sample_buf = (uint8_t*)malloc(2 * 1024 * 1024);
	memset(video_sample_buf, 0, 2 * 1024 * 1024);

	while (v_sample < vt->sample_count || (at && a_sample < at->sample_count)) {
		bool have_v = v_sample < vt->sample_count;
		bool have_a = at && a_sample < at->sample_count;

		unsigned v_frame_bytes = 0, v_ts = 0, v_dur = 0;
		unsigned a_frame_bytes = 0, a_ts = 0, a_dur = 0;
		MP4D_file_offset_t v_off = 0, a_off = 0;

		if (have_v)
			v_off = MP4D_frame_offset(&mp4, video_track, v_sample, &v_frame_bytes, &v_ts, &v_dur);

		//if (have_a)
		//	a_off = MP4D_frame_offset(&mp4, audio_track, a_sample, &a_frame_bytes, &a_ts, &a_dur);

		//bool take_audio = have_a && (!have_v || a_ts <= v_ts + 300);

		//if (take_audio) {
		/**uint8_t* buf = (uint8_t*)malloc(a_frame_bytes);**/
		//	if (mp4_read_cb((int64_t)a_off, audio_sample_buffer, a_frame_bytes, &mf) == 0) {
		//		decode_audio_sample(audio_sample_buffer, a_frame_bytes);
		//	} else {
		//		_KePrint("audio sample %d read failed \r\n", a_sample);
		//	}
		//free(buf);
		//	a_sample++;
		//} else if (have_v) {
		//uint8_t* buf = (uint8_t*)malloc(v_frame_bytes);
		if (mp4_read_cb((int64_t)v_off, video_sample_buf, v_frame_bytes, &mf) == 0) {
			size_t p = 0;
			while (p + 4 <= v_frame_bytes) {
				uint32_t nal_len = (video_sample_buf[p] << 24) | (video_sample_buf[p + 1] << 16) |
								   (video_sample_buf[p + 2] << 8) | video_sample_buf[p + 3];
				p += 4;
				if (p + nal_len > v_frame_bytes) {
					printf("p + nal_len > frame_bytes \n");
					break;
				}
				feed_nal(&h264, video_sample_buf + p, nal_len, 0);
				p += nal_len;
			}
		} else {
			_KePrint("sample read failed \r\n");
		}
		//free(buf);
		v_sample++;
	}

	printf("h264bsd shutdowning \r\n");
	free(video_sample_buf);
	//free(audio_sample_buffer);
	h264bsdShutdown(&h264);
	MP4D_close(&mp4);
	fclose(mf.f);
}

typedef struct _sound_card_list {
	char name[32];
	int cardID;
	struct _sound_card_list* next;
} aurora_snd_card_list;

void open_sound() {
	sound = _KeOpenFile("/dev/sound", FILE_OPEN_READ_ONLY);

	XEFileIOControl ioctl;
	memset(&ioctl, 0, sizeof(XEFileIOControl));

	/* uint_1 holds the millisecond to sleep after
	* one frame playback */
	ioctl.uint_1 = 0;
	ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;
	/** uint_2 must hold the sound card number to use **/
	ioctl.uint_2 = -1;
	int num_card_count = _KeFileIoControl(sound, SOUND_GET_CARD_TOTALNUM, &ioctl);
	if (num_card_count == 0)
		return;

	ioctl.uint_1 = num_card_count;
	aurora_snd_card_list* list =
		(aurora_snd_card_list*)malloc(sizeof(aurora_snd_card_list) * num_card_count);
	ioctl.ulong_1 = (uint64_t)list;
	if (_KeFileIoControl(sound, SOUND_GET_CARD_LIST, &ioctl)) {
		_KePrint("[init]: failed to get sound card list \r\n");
		_KePauseThread();
	}

	/** just print all card name once **/
	for (int i = 0; i < num_card_count; i++) {
		_KePrint("[init]: %s sound is installed, id : %d \r\n", list[i].name, list[i].cardID);
	}

	/** let's use default first sound card here **/
	ioctl.uint_2 = list->cardID;

	_KeFileIoControl(sound, SOUND_REGISTER_SNDPLR, &ioctl);
}

void* audio_thread(void* arg) {
	open_sound();

	Mp4File mf;
	mf.f = fopen("/test.mp4", "rb");
	if (!mf.f) {
		printf("audio thread: failed to open file \r\n");
		_KePrint("failed to open file \r\n");
		_KePauseThread();
	}

	fseek(mf.f, 0, SEEK_END);
	int64_t file_size = ftell(mf.f);
	fseek(mf.f, 0, SEEK_SET);

	MP4D_demux_t mp4;
	if (!MP4D_open(&mp4, mp4_read_cb, &mf, file_size)) {
		_KePrint("MP4 failed \r\n");
		fclose(mf.f);
		_KePauseThread();
	}

	int audio_track = -1;
	for (int i = 0; i < mp4.track_count; i++) {
		if (mp4.track[i].object_type_indication == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3) {
			audio_track = i;
			break;
		}
	}

	if (audio_track < 0) {
		printf("no audio track \r\n");
		MP4D_close(&mp4);
		fclose(mf.f);
		_KePauseThread();
	}

	MP4D_track_t* at = &mp4.track[audio_track];
	init_audio_decoder(at);
	_KePrint("Audio decoder initialized \r\n");
	printf("decoding audio samples in audio thread \r\n");

	uint8_t* audio_sample_buffer = (uint8_t*)malloc(64 * 1024);
	memset(audio_sample_buffer, 0, 64 * 1024);

	const uint64_t rate = at->SampleDescription.audio.samplerate_hz;
	const uint64_t LEAD_US = 100000;
	uint64_t frames_out = 0;

	for (unsigned s = 0; s < at->sample_count; s++) {
		unsigned frame_bytes = 0, ts = 0, dur = 0;
		MP4D_file_offset_t off = MP4D_frame_offset(&mp4, audio_track, s, &frame_bytes, &ts, &dur);

		if (frame_bytes > 64 * 1024) {
			printf("audio sample too big: skipping \r\n");
			continue;
		}

		if (mp4_read_cb((int64_t)off, audio_sample_buffer, frame_bytes, &mf) != 0)
			continue;

		frames_out += decode_audio_sample(audio_sample_buffer, frame_bytes);

		if (!g_audio_start_us)
			g_audio_start_us = now_us();

		for (;;) {
			uint64_t queued_us = frames_out * 1000000ULL / rate;
			uint64_t played_us = now_us() - g_audio_start_us;
			if (queues_us <= played_us + LEAD_US)
				break;
			_KeProcessSleep(10);
		}
		//_KeProcessSleep(1);
	}

	free(audio_sample_buffer);
	//_KePauseThread();
	pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
	printf("videoplayer started :%d\r\n", argc);

	app = ChitralekhaStartApp(argc, argv);
	mainWin =
		ChCreateWindow(app, WINDOW_FLAG_MOVABLE, "Video Player", 400, 480 / 2 - 400 / 2, 700, 500);

	ChWindowPaint(mainWin);

	pthread_t thr;
	pthread_create(&thr, NULL, audio_thread, NULL);

	play_video("/test.mp4");
	while (1) {
		_KePauseThread();
	}

	return 0;
}