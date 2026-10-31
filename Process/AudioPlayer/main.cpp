/**
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/iocodes.h>
#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/button.h>
#include <widgets/window.h>
#include <widgets/menubar.h>
#include <widgets/msgbox.h>
#include <widgets/menu.h>
#include <widgets/scrollpane.h>
#include <widgets/icon.h>
#include <widgets/view.h>
#include <sys/mman.h>
#include <nanojpg.h>
#include <string.h>
#include <stdlib.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#define MINIID3_IMPLEMENTATION
#include "miniid3.h"

ChitralekhaApp* app;
ChWindow* mainWin;
ChCanvas* mainCanvas;
ChCanvas* browserCanvas;
list_t* mainWidgetList;
list_t* browserWidgetList;
ChListView* browseList;
ChIcon* musicIcon;
ChMenubar* mainMenubar;
bool _music_library_drawn;
char* selectedFile;
int snd;
int _file;
bool _start_player;
void* soundbuff;
void* filebuff;
//static mp3_decoder_t mp3;
//static mp3_info_t info;
bool _auto_start;

void play_mp3(const char* path);
/*
* RegisterSound -- create a new instance of sound
*/
void RegisterSound() {
	/* open the sound device-file, it is in /dev directory */
	snd = _KeOpenFile("/dev/sound", FILE_OPEN_WRITE);

	char test[2];
	/* allocate an ioctl structure where required information
	* will be putted */
	XEFileIOControl ioctl;

	memset(&ioctl, 0, sizeof(XEFileIOControl));

	/* uint_1 holds the millisecond to sleep after
	* one frame playback */
	ioctl.uint_1 = 0;

	/* most important aurora_syscall_magic number, without
	* this, iocontrol system call will not work */
	ioctl.syscall_magic = AURORA_SYSCALL_MAGIC;

	/* register the app to sound layer, as it will create
	* a private dsp-box for this app */
	_KeFileIoControl(snd, SOUND_REGISTER_SNDPLR, &ioctl);
}

void PlayerThread() {
	/* register the sound */
	RegisterSound();
	XEFileStatus stat;
	memset(&stat, 0, sizeof(XEFileStatus));
	while (1) {
		if (selectedFile) {
			if (strcat(selectedFile, ".mp3")) {
				printf("playing mp3 -- %s \r\n", selectedFile);
				play_mp3(selectedFile);
			} else {
				/*
		         * _start_player expects its sound file to be
		         * ready i.e its already opened 
		         */
				if (_start_player) {
					_KeFileStat(_file, &stat);
					if (stat.eof) {
						_KeCloseFile(_file);
						_start_player = false;
					} else {
						_KeReadFile(_file, soundbuff, 4096);
						_KeWriteFile(snd, soundbuff, 4096);
					}
				}
			}
		}

		if (!_start_player)
			_KeProcessSleep(1000);
	}
}

/*
 * WindowHandleMessage -- handles incoming deodhai messages
 * @param e -- PostBox event message structure
 */
void WindowHandleMessage(PostEvent* e) {
	switch (e->type) {
	/* handle mouse event from deodhai */
	case DEODHAI_REPLY_MOUSE_EVENT: {
		int handle = e->dword4;
		if (e->dword5 == WINDOW_HANDLE_TYPE_NORMAL) {
			ChWindow* mouseWin = ChGetWindowByHandle(mainWin, handle);
			ChWindowHandleMouse(mouseWin, e->dword, e->dword2, e->dword3);
		} else if (e->dword5 == WINDOW_HANDLE_TYPE_POPUP) {
			ChWindow* pw = ChGetPopupWindowByHandle(mainWin, handle);
			ChPopupWindowHandleMouse(pw, e->dword, e->dword2, e->dword3);
		}
		memset(e, 0, sizeof(PostEvent));
		break;
	}
		/* handle key events from deodhai */
	case DEODHAI_REPLY_KEY_EVENT: {
		int code = e->dword;
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	case DEODHAI_REPLY_FOCUS_CHANGED: {
		int focus_val = e->dword;
		int handle = e->dword2;
		ChWindow* focWin = ChGetWindowByHandle(mainWin, handle);
		ChWindowHandleFocus(focWin, focus_val, handle);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	}
}

void AboutClicked(ChWidget* wid, ChWindow* win) {
	ChMessageBox* mb = ChCreateMessageBox(mainWin,
										  "Accent v1.0",
										  "Accent Player v1.0 for XenevaOS !!",
										  MSGBOX_TYPE_ONLYCLOSE,
										  MSGBOX_ICON_SUCCESS);
	ChMessageBoxShow(mb);
}

/*
 * ExitItemClicked -- calls window close function
 * @param wid -- Pointer to widget
 * @param win -- Pointer to main Window
 */
void ExitItemClicked(ChWidget* wid, ChWindow* win) {
	if (mainWin)
		ChWindowCloseWindow(mainWin);
}

/* DrawWallpaper -- code copied from deodhai wallpaper
 * painter
*/
void DrawWallpaper(ChCanvas* canv, char* filename, int x, int y) {
	int image = _KeOpenFile(filename, FILE_OPEN_READ_ONLY);
	XEFileStatus stat;
	memset(&stat, 0, sizeof(XEFileStatus));
	_KeFileStat(image, &stat);
	void* data_ = _KeMemMap(NULL, stat.size, 0, 0, -1, 0);
	memset(data_, 0, ALIGN_UP(stat.size, 4096));
	_KeReadFile(image, data_, ALIGN_UP(stat.size, 4096));

	uint8_t* data1 = (uint8_t*)data_;

	Jpeg::Decoder* decor =
		new Jpeg::Decoder((uint8_t*)data1, ALIGN_UP(stat.size, 4096), malloc, free);
	if (decor->GetResult() != Jpeg::Decoder::OK) {
		_KePrint("Decoder error \n");
		for (;;)
			;
		return;
	}
	int w = decor->GetWidth();
	int h = decor->GetHeight();
	uint8_t* data = decor->GetImage();
	for (int i = 0; i < h; i++) {
		for (int k = 0; k < w; k++) {
			int j = k + i * w;
			uint8_t r = data[j * 3];
			uint8_t g = data[j * 3 + 1];
			uint8_t b = data[j * 3 + 2];
			uint32_t rgba = ((r << 16) | (g << 8) | (b)) & 0x00ffffff;
			rgba = rgba | 0xff000000;
			ChDrawPixel(canv, x + k, y + i, rgba);
			j++;
		}
	}

	_KeMemUnmap(data_, stat.size);
	_KeCloseFile(image);
}

/*
 * DrawScaledJpeg -- decode a JPEG from memory and scale it (nearest-neighbour)
 * to fill the rectangle (x, y, destW, destH) on the canvas.
 * @param canv   -- target canvas
 * @param jpgBuf -- raw JPEG bytes
 * @param jpgLen -- byte count
 * @param x, y   -- top-left destination corner on canvas
 * @param destW  -- destination width  (pixels)
 * @param destH  -- destination height (pixels)
 */
static void
DrawScaledJpeg(ChCanvas* canv, uint8_t* jpgBuf, size_t jpgLen, int x, int y, int destW, int destH) {
	Jpeg::Decoder* dec = new Jpeg::Decoder(jpgBuf, (int)jpgLen, malloc, free);
	if (dec->GetResult() != Jpeg::Decoder::OK) {
		_KePrint("AlbumArt: JPEG decode error\n");
		delete dec;
		return;
	}

	int srcW = dec->GetWidth();
	int srcH = dec->GetHeight();
	uint8_t* pixels = dec->GetImage(); /* RGB, 3 bytes per pixel */

	/* nearest-neighbour scale to (destW x destH) */
	for (int dy = 0; dy < destH; dy++) {
		int sy = (dy * srcH) / destH;
		for (int dx = 0; dx < destW; dx++) {
			int sx = (dx * srcW) / destW;
			int idx = sy * srcW + sx;
			uint8_t r = pixels[idx * 3];
			uint8_t g = pixels[idx * 3 + 1];
			uint8_t b = pixels[idx * 3 + 2];
			uint32_t rgba = 0xff000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
			ChDrawPixel(canv, x + dx, y + dy, rgba);
		}
	}
	delete dec;
}

/*
 * DrawMusicBackground -- draw album art from the MP3 scaled to fill the
 * wallpaper area, or fall back to /musicbk.jpg if no embedded art exists.
 * Uses miniid3_read_album_art() from miniid3.h.
 * @param canv    -- target canvas
 * @param mp3Path -- full path to MP3 (may be NULL)
 * @param x, y   -- top-left of destination rectangle
 * @param w, h   -- size of destination rectangle
 */
void DrawMusicBackground(ChCanvas* canv, const char* mp3Path, int x, int y, int w, int h) {
	if (mp3Path) {
		uint8_t* artBuf = NULL;
		size_t artLen = 0;
		if (miniid3_read_album_art(mp3Path, &artBuf, &artLen)) {
			_KePrint("AlbumArt: found %d bytes, drawing scaled JPEG\r\n", (int)artLen);
			DrawScaledJpeg(canv, artBuf, artLen, x, y, w, h);
			free(artBuf);
			return;
		}
		_KePrint("AlbumArt: none found in %s, using fallback\r\n", mp3Path);
	}
	/* fallback: static music background image */
	DrawWallpaper(canv, "/musicbk.jpg", x, y);
}

void UpdateHomeUI() {
	mainWin->canv = mainCanvas;
	mainWin->widgets = mainWidgetList;
	ChWindowUpdate(mainWin, 0, 0, mainWin->info->width, mainWin->info->height, 1, 0);
}

void UpdateBrowserUI() {
	mainWin->canv = browserCanvas;
	mainWin->widgets = browserWidgetList;
	ChWindowUpdate(mainWin, 0, 0, mainWin->info->width, mainWin->info->height, 1, 0);
}

void BrowseMenuActionHandler(ChWidget* wid, ChWindow* win) {
	if (_music_library_drawn == false) {
		mainWin->widgets = browserWidgetList;
		mainWin->canv = browserCanvas;
		ChWindowPaint(mainWin);

		ChFontSetSize(mainWin->app->baseFont, 18);
		ChFontDrawText(browserCanvas,
					   mainWin->app->baseFont,
					   "Your Music library.....",
					   10,
					   88,
					   18,
					   LIGHTBLACK);

		ChWindowUpdate(mainWin, 0, 0, mainWin->info->width, mainWin->info->height, 1, 0);
		_music_library_drawn = true;
	} else {
		_KePrint("Updating the browsing \r\n");
		UpdateBrowserUI();
		_KeProcessSleep(100);
	}
}

void SelectButtonActionHandler(ChWidget* wid, ChWindow* win) {
	ChListItem* li = ChListViewGetSelectedItem(browseList);
	if (li) {
		selectedFile = li->itemText;
	}
	_KeProcessSleep(100);
	UpdateHomeUI();

	/* redraw album art (or fallback) for the selected song */
	if (selectedFile) {
		char fullPath[64];
		memset(fullPath, 0, sizeof(fullPath));
		strcpy(fullPath, "/MUSIC/");
		strcpy(fullPath + strlen("/MUSIC/"), selectedFile);

		int artX = 10;
		int artY = 64;
		int artW = mainWin->info->width - 20;
		int artH = mainWin->info->height - 100 - artY;
		DrawMusicBackground(mainCanvas, fullPath, artX, artY, artW, artH);
		ChWindowUpdate(mainWin, artX, artY, artW, artH, 1, 0);
	}

	_KeProcessSleep(100);
}

void MusicPrepareLibrary(ChListView* lv) {
	int dirfd = _KeOpenDir("/music");
	XEDirectoryEntry* dirent = (XEDirectoryEntry*)malloc(sizeof(XEDirectoryEntry));
	memset(dirent, 0, sizeof(XEDirectoryEntry));
	while (1) {
		if (dirent->index == -1)
			break;
		int code = _KeReadDir(dirfd, dirent);
		if (code != -1) {
			if (dirent->flags & FILE_GENERAL) {
				ChListItem* li = ChListViewAddItem(mainWin, lv, dirent->filename);
				ChListViewSetListItemIcon(li, musicIcon);
			}
		}
		memset(dirent->filename, 0, 32);
	}
}

void PlayPauseButtonAction(ChWidget* wid, ChWindow* mainWin) {
	if (selectedFile != NULL) {
		if (!_auto_start) {
			char path[32];
			memset(path, 0, 32);
			strcpy(path, "/MUSIC/");
			strcpy(path + strlen("/MUSIC/"), selectedFile);
			_KePrint("Selected file -> %s \r\n", path);
			_file = _KeOpenFile(path, FILE_OPEN_READ_ONLY);
			_KePrint("_file -> %d \r\n", _file);
			_start_player = 1;
		}
		_KePrint("selected File -> %s \r\n", selectedFile);
	}
}

void PrepareBrowserUI() {
	/* allocate the browser canvas */
	browserCanvas = ChCreateCanvas(mainWin->canv->canvasWidth, mainWin->canv->canvasHeight);
	ChAllocateBuffer(browserCanvas);
	mainWidgetList = mainWin->widgets;
	browserWidgetList = initialize_list();

	musicIcon = ChCreateIcon();
	ChIconOpen(musicIcon, "/icons/music.bmp");
	ChIconRead(musicIcon);

	ChScrollPane* sp = ChCreateScrollPane(mainWin, 10, 64, 100, 380);
	browseList = ChCreateListView(10, 100, 380, 380);
	ChListViewSetScrollpane(browseList, sp);

	MusicPrepareLibrary(browseList);

	ChButton* selectButt = ChCreateButton(mainWin->info->width - 75, 36, 65, 35, "Select");
	selectButt->base.ChActionHandler = SelectButtonActionHandler;
	list_add(browserWidgetList, mainMenubar);
	list_add(browserWidgetList, browseList);
	list_add(browserWidgetList, sp);
	list_add(browserWidgetList, selectButt);
}

/*
 * AudioPlayerClose -- Close callback, which will free
 * up all allocated resources before the application
 * fully closes
 * @param win -- Pointer to main Window
 */
void AudioPlayerClose(ChWindow* win) {
	for (int i = 0; i < browserWidgetList->pointer; i++) {
		ChWidget* wid = (ChWidget*)list_get_at(browserWidgetList, i);
		if (wid == (ChWidget*)mainMenubar) {
			list_remove(browserWidgetList, i);
		} else {
			if (wid->ChDestroy)
				wid->ChDestroy(wid, win);
		}
	}
	list_clear_all(browserWidgetList);
	free(browserWidgetList);
	ChDeAllocateBuffer(browserCanvas);
	free(browserCanvas);
}

#define CHUNK	4096
#define WINDOW	(16 * 1024)
#define BUF_CAP (WINDOW + CHUNK)

static uint32_t synchsafe(const uint8_t* p) {
	return (p[0] << 21) | (p[1] << 14) | (p[2] << 7) | p[3];
}

static int refill(FILE* f, uint8_t* buf, size_t* pos, size_t* have, int* eof) {
	if (*pos) {
		memmove(buf, buf + *pos, *have - *pos);
		*have -= *pos;
		*pos = 0;
	}
	while (!*eof && *have < WINDOW) {
		size_t n = fread(buf + *have, 1, CHUNK, f);
		if (n == 0) {
			*eof = 1;
			break;
		}
		*have += n;
	}
	return (int)*have;
}

#define AUDIO_BLOCK 4096
#define NBUF		8
static uint8_t stage[NBUF][AUDIO_BLOCK];
static int cur = 0;
static size_t stage_len = 0;

static void audio_push(int audio_fd, const void* data, size_t len) {
	const uint8_t* src = (const uint8_t*)data;
	while (len) {
		size_t n = AUDIO_BLOCK - stage_len;
		if (n > len)
			n = len;
		memcpy(stage[cur] + stage_len, (void*)src, n);
		stage_len += n;
		src += n;
		len -= n;

		if (stage_len == AUDIO_BLOCK) {
			_KeWriteFile(snd, stage[cur], AUDIO_BLOCK);
			cur = (cur + 1) % NBUF;
			stage_len = 0;
			_KeProcessSleep(4);
		}
	}
}

static void audio_flush(int audio_fd) {
	if (stage_len) {
		memset(stage + stage_len, 0, AUDIO_BLOCK - stage_len);
		_KeWriteFile(audio_fd, stage, AUDIO_BLOCK);
		stage_len = 0;
	}
}

typedef struct {
	uint32_t step;
	uint32_t pos;
	int16_t prev[2];
	int init;
} Resampler;

static void rs_init(Resampler* r, uint32_t in_hz, uint32_t out_hz) {
	r->step = (uint32_t)(((uint64_t)in_hz << 16) / out_hz);
	r->pos = 0;
	r->init = 0;
}

static size_t rs_process(Resampler* r, const int16_t* in, size_t n, int16_t* out) {
	if (!n)
		return 0;
	if (!r->init) {
		r->prev[0] = in[0];
		r->prev[1] = in[1];
		r->init = 1;
	}

	size_t o = 0;
	while (((r->pos >> 16) + 1) <= n) {
		uint32_t i = r->pos >> 16;
		int32_t f = r->pos & 0xFFFF;
		for (int c = 0; c < 2; c++) {
			int32_t a = (i == 0) ? r->prev[c] : in[(i - 1) * 2 + c];
			int32_t b = in[i * 2 + c];
			out[o * 2 + c] = (int16_t)(a + (((b - a) * f) >> 16));
		}
		o++;
		r->pos += r->step;
	}
	r->pos -= (uint32_t)n << 16;
	r->prev[0] = in[(n - 1) * 2];
	r->prev[1] = in[(n - 1) * 2 + 1];
	return o;
}

static Resampler rs;
static int16_t rs_out[1300 * 2];
static int cur_hz = 0;

void play_mp3(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return;

	RegisterSound();

	static mp3dec_t dec;
	static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	uint8_t* buf = (uint8_t*)malloc(BUF_CAP);
	uint8_t* hw_buf = (uint8_t*)malloc(4096);
	size_t pos = 0, have = 0;
	int eof = 0;
	mp3dec_init(&dec);

	for (;;) {
		if (have - pos < WINDOW && !eof)
			refill(f, buf, &pos, &have, &eof);

		if (have - pos == 0)
			break;

		mp3dec_frame_info_t info;
		int samples = mp3dec_decode_frame(&dec, buf + pos, (int)(have - pos), pcm, &info);
		if (info.frame_bytes == 0) {
			if (eof)
				break;
			pos = have;
			continue;
		}

		pos += info.frame_bytes;
		if (samples) {
			if (info.hz != 48000) {
				if (info.hz != cur_hz) {
					rs_init(&rs, info.hz, 48000);
					cur_hz = info.hz;
				}
				size_t n = rs_process(&rs, pcm, samples, rs_out);
				_KeWriteFile(snd, rs_out, n * 4);
				_KeProcessSleep(2);
			} else {
				//audio_push(snd, pcm, samples * info.channels * sizeof(int16_t));
				_KeWriteFile(snd, pcm, samples * info.channels * sizeof(int16_t));
				_KeProcessSleep(2);
			}
		}
		//	audio_flush(snd);
	}

	free(buf);
	fclose(f);
}

/*
* main -- main entry
*/
int main(int argc, char* argv[]) {
	app = ChitralekhaStartApp(argc, argv);
	mainWin = ChCreateWindow(
		app, WINDOW_FLAG_MOVABLE, "Accent", 100, 100, CHITRALEKHA_DEFAULT_WIN_WIDTH, 500);

	_music_library_drawn = false;
	_start_player = false;
	selectedFile = NULL;

	if (argc > 1) {
		selectedFile = argv[1];
		_auto_start = true;
		_file = _KeOpenFile(selectedFile, FILE_OPEN_READ_ONLY);
		_start_player = true;
	}

	mainWin->ChCloseWin = AudioPlayerClose;

	mainMenubar = ChCreateMenubar(mainWin);

	ChMenuButton* file = ChCreateMenubutton(mainMenubar, "File");
	ChMenubarAddButton(mainMenubar, file);
	ChMenuButton* edit = ChCreateMenubutton(mainMenubar, "Help");
	ChMenubarAddButton(mainMenubar, edit);

	ChPopupMenu* pm = ChCreatePopupMenu(mainWin, NULL);
	ChMenuItem* browse = ChCreateMenuItem("Select Song...", pm);
	browse->wid.ChActionHandler = BrowseMenuActionHandler;
	ChMenuItem* item = ChCreateMenuItem("Exit", pm);
	item->wid.ChActionHandler = ExitItemClicked;
	ChMenuButtonAddMenu(file, pm);

	ChPopupMenu* help = ChCreatePopupMenu(mainWin, NULL);
	ChMenuItem* about = ChCreateMenuItem("About", help);
	about->wid.ChActionHandler = AboutClicked;
	ChMenuButtonAddMenu(edit, help);

	ChWindowAddWidget(mainWin, (ChWidget*)mainMenubar);

	ChButton* prevbutt = ChCreateButton(10, mainWin->info->height - 100, 100, 65, "Previous");
	ChWindowAddWidget(mainWin, (ChWidget*)prevbutt);

	ChButton* playbutt = ChCreateButton(
		mainWin->info->width / 2 - 140 / 2, mainWin->info->height - 100, 140, 65, "Play/Pause");
	playbutt->base.ChActionHandler = PlayPauseButtonAction;
	ChWindowAddWidget(mainWin, (ChWidget*)playbutt);

	ChButton* nextbutt = ChCreateButton(
		(playbutt->base.x + playbutt->base.w) + 18, mainWin->info->height - 100, 100, 65, "Next");
	ChWindowAddWidget(mainWin, (ChWidget*)nextbutt);

	ChWindowPaint(mainWin);

	/* wallpaper area: below menubar (y=64) and above transport buttons (height-100) */
	int artX = 10;
	int artY = 64;
	int artW = mainWin->info->width - 20;		   /* 10px margin each side */
	int artH = mainWin->info->height - 100 - artY; /* leave room for buttons */
	DrawMusicBackground(mainWin->canv, "/music/charlie.mp3", artX, artY, artW, artH);
	ChWindowUpdate(mainWin, 0, 0, mainWin->info->width, mainWin->info->height, 1, 0);

	mainCanvas = mainWin->canv;

	soundbuff = malloc(4096);
	memset(soundbuff, 0, 4096);

	/* prepare the browser UI*/
	PrepareBrowserUI();

	ChWindowBroadcastIcon(app, "/icons/media.bmp");

	play_mp3("/music/charlie.mp3");

	//int threadIdx = _KeCreateThread(PlayerThread, "accent");

	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));

	setjmp(mainWin->jump);
	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		WindowHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
	}
}