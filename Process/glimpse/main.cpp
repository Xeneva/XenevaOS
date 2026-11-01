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
#include <widgets/icon.h>
#include <widgets/gridview.h>
#include <widgets/sidebar.h>
#include <widgets/toolbar.h>
#include <bmp.h>
#include <string.h>
#include <stdlib.h>

#include "glimpse.h"
#include "glimpse_image.h"
#include "glimpse_fs.h"
#include "glimpse_viewer.h"

ChitralekhaApp* app = NULL;
ChWindow* mainWin = NULL;
static ChGridView* gv = NULL;
static GlimpseViewer* viewer = NULL;
static ChSidebar* sb = NULL;
static ChToolbar* tb = NULL;

static void OnThumbnailSelected(ChGridView* grid, ChWindow* win, int index, ChGridCell* cell);
static void OnBackClicked(void* user_data);

/*
 * WindowHandleMessage -- handles incoming deodhai messages
 * @param e -- PostBox event message structure
 */
void WindowHandleMessage(PostEvent* e) {
	switch (e->type) {
	/* handle mouse event from deodhai */
	case DEODHAI_REPLY_MOUSE_EVENT: {
		ChWindowHandleMouse(mainWin, e->dword, e->dword2, e->dword3);
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	/* handle key events from deodhai */
	case DEODHAI_REPLY_KEY_EVENT: {
		memset(e, 0, sizeof(PostEvent));
		break;
	}
	}
}

static void OnThumbnailSelected(ChGridView* grid, ChWindow* win, int index, ChGridCell* cell) {
	(void)grid;
	(void)index;
	if (!cell || !cell->data) return;

	const char* full_image_path = (const char*)cell->data;

	if (GlimpseViewerLoad(viewer, full_image_path)) {
		gv->wid.visible = false;
		if (sb) sb->base.visible = false;
		if (tb) tb->base.visible = false;
		viewer->wid.visible = true;
		GlimpseWindowPaint(win);
	}
}

static void OnBackClicked(void* user_data) {
	(void)user_data;
	viewer->wid.visible = false;
	GlimpseViewerUnload(viewer);

	if (sb) sb->base.visible = true;
	if (tb) tb->base.visible = true;
	gv->wid.visible = true;
	gv->force_full_redraw = true;
	GlimpseWindowPaint(mainWin);
}

typedef struct {
	ChGridView* gv;
	int count;
} ScanContext;

static ChIcon* CreateIconFromRaw(const GlimpseRawImage* img) {
	if (!img || !img->pixels || img->width <= 0 || img->height <= 0) return NULL;
	int w = img->width;
	int h = img->height;
	uint32_t pixel_bytes = (uint32_t)(w * h * 4);
	uint32_t total_size = sizeof(BMP) + sizeof(BMPInfo) + pixel_bytes;

	uint8_t* out_buf = (uint8_t*)malloc(total_size);
	if (!out_buf) return NULL;

	BMP* bmp = (BMP*)out_buf;
	bmp->type = 0x4D42;
	bmp->size = total_size;
	bmp->resv1 = 0;
	bmp->resv2 = 0;
	bmp->off_bits = sizeof(BMP) + sizeof(BMPInfo);

	BMPInfo* info = (BMPInfo*)(out_buf + sizeof(BMP));
	memset(info, 0, sizeof(BMPInfo));
	info->biSize = sizeof(BMPInfo);
	info->biWidth = w;
	info->biHeight = h;
	info->biPlanes = 1;
	info->biBitCount = 32;
	info->biCompression = 0;
	info->biSizeImage = pixel_bytes;
	info->biXPelsPerMeter = 3780;
	info->biYPelsPerMeter = 3780;

	uint8_t* dst_pixels = out_buf + bmp->off_bits;
	for (int y = 0; y < h; y++) {
		int src_y = h - 1 - y;
		const uint8_t* src_row = img->pixels + (src_y * w * 4);
		uint8_t* dst_row = dst_pixels + (y * w * 4);

		for (int x = 0; x < w; x++) {
			dst_row[x * 4 + 0] = src_row[x * 4 + 2]; // B
			dst_row[x * 4 + 1] = src_row[x * 4 + 1]; // G
			dst_row[x * 4 + 2] = src_row[x * 4 + 0]; // R
			dst_row[x * 4 + 3] = src_row[x * 4 + 3]; // A
		}
	}

	ChIcon* ico = ChCreateIcon();
	if (!ico) {
		free(out_buf);
		return NULL;
	}
	ico->format = CHITRALEKHA_ICON_BMP;
	ico->image.image = out_buf;
	ico->image.fd = 0;
	ico->image.filesz = total_size;
	ico->image.data = dst_pixels;
	ico->image.width = w;
	ico->image.height = h;
	return ico;
}

static void ImageDiscoveredCallback(const char* full_path, const char* filename, void* user_data) {
	ScanContext* ctx = (ScanContext*)user_data;
	if (!ctx || !ctx->gv || !full_path) return;

	// Check for duplicates
	for (int i = 0; i < ctx->gv->cell_count; i++) {
		if (ctx->gv->cells[i].data && strcmp((const char*)ctx->gv->cells[i].data, full_path) == 0) {
			return;
		}
	}

	char thumb_path[256];
	GlimpseGetThumbnailPath(full_path, thumb_path, sizeof(thumb_path));

	GlimpseRawImage thumb_img;
	memset(&thumb_img, 0, sizeof(thumb_img));
	bool has_thumb_img = false;

	// Cache check: If thumbnail does not exist on disk, generate and cache it
	if (!GlimpseFileExists(thumb_path)) {
		GlimpseRawImage src_img;
		if (GlimpseLoadImage(full_path, &src_img)) {
			int tw = 0, th = 0;
			GlimpseFitDimensions(src_img.width, src_img.height, 100, 100, &tw, &th);

			if (GlimpseResizeImage(&src_img, tw, th, &thumb_img)) {
				GlimpseSaveBmp(thumb_path, &thumb_img);
				has_thumb_img = true;
			}
			GlimpseFreeImage(&src_img);
		}
	}

	ChIcon* ico = NULL;
	if (GlimpseFileExists(thumb_path)) {
		ico = ChCreateIcon();
		ChIconOpen(ico, (char*)thumb_path);
		ChIconRead(ico);
	} else if (has_thumb_img && thumb_img.pixels) {
		ico = CreateIconFromRaw(&thumb_img);
	} else if (strstr(full_path, ".bmp") || strstr(full_path, ".BMP")) {
		ico = ChCreateIcon();
		ChIconOpen(ico, (char*)full_path);
		ChIconRead(ico);
	} else {
		ico = ChCreateIcon();
		ChIconOpen(ico, (char*)"/icons/img.bmp");
		ChIconRead(ico);
	}

	if (has_thumb_img) {
		GlimpseFreeImage(&thumb_img);
	}

	if (ico) {
		char* stored_path = strdup(full_path);
		ChGridAddThumbnail(ctx->gv, ico, filename, (void*)stored_path);
		ctx->count++;
	}
}

/*
* main -- main entry
*/
int main(int argc, char* argv[]) {
	app = ChitralekhaStartApp(argc, argv);
	mainWin = ChCreateWindow(app, WINDOW_FLAG_MOVABLE, (char*)"Glimpse", 100, 100, 780, 500);

	mainWin->color = 0xFFB5B5B5;
	mainWin->info->alpha = false;
	mainWin->ChWinPaint = GlimpseWindowPaint;

	for (uint32_t i = 0; i < mainWin->GlobalControls->pointer; i++) {
		ChWinGlobalControl* glbl = (ChWinGlobalControl*)list_get_at(mainWin->GlobalControls, i);
		glbl->outlineColor = BLACK;
	}

	// 1. Ensure /appdata/glimpse/icons/ exists
	GlimpseEnsureCacheDir();

	// 2. Load sidebar icons
	ChIcon* homeDir = ChCreateIcon();
	ChIconOpen(homeDir, (char*)"/appdata/glimpse/icons/home.bmp");
	ChIconRead(homeDir);

	ChIcon* photoDir = ChCreateIcon();
	ChIconOpen(photoDir, (char*)"/appdata/glimpse/icons/photo.bmp");
	ChIconRead(photoDir);

	ChIcon* download = ChCreateIcon();
	ChIconOpen(download, (char*)"/appdata/glimpse/icons/dwnld.bmp");
	ChIconRead(download);

	ChIcon* trsh = ChCreateIcon();
	ChIconOpen(trsh, (char*)"/appdata/glimpse/icons/trsh_t.bmp");
	ChIconRead(trsh);

	ChIcon* gallery = ChCreateIcon();
	ChIconOpen(gallery, (char*)"/appdata/glimpse/icons/gllry.bmp");
	ChIconRead(gallery);

	// 3. Create GridView for image thumbnails
	gv = ChGridViewCreate(170, 80, 600, 500 - 80, 4, 120, 140, 8);
	gv->wid.visible = true;
	gv->color_bg = mainWin->color;
	gv->color_label_text = LIGHTBLACK;
	gv->color_hover_bg = LIGHTSILVER;
	gv->vscroll.color_track = mainWin->color;
	gv->vscroll.color_arrow_fg = gv->vscroll.color_thumb;
	gv->vscroll.color_arrow_bg = mainWin->color;
	gv->OnSelect = OnThumbnailSelected;

	// 4. Create Full-Image Viewer widget (occupying full content area, hidden initially)
	viewer = GlimpseViewerCreate(0, 26, mainWin->info->width, mainWin->info->height - 26, OnBackClicked, NULL);
	viewer->wid.visible = false;

	// 5. Scan directories and populate grid view dynamically
	ScanContext scan_ctx;
	scan_ctx.gv = gv;
	scan_ctx.count = 0;
	GlimpseScanImages(ImageDiscoveredCallback, &scan_ctx);

	// If no images discovered, add a sample icon so the grid is not empty
	if (scan_ctx.count == 0) {
		ChIcon* def_ico = ChCreateIcon();
		ChIconOpen(def_ico, (char*)"/icons/img.bmp");
		ChIconRead(def_ico);
		ChGridAddThumbnail(gv, def_ico, "No images found", NULL);
	}

	gv->force_full_redraw = 1;

	// 6. Create Sidebar
	sb = ChSidebarCreate(0, 80, 170, 500 - 80);
	sb->base.visible = true;
	sb->bgColor = mainWin->color;
	ChSidebarSection* sec1 = ChSidebarAddSection(sb, (char*)"Favourites");
	ChSidebarAddItem(sec1, (char*)"Home", homeDir, 0, 0, NULL);
	ChSidebarAddItem(sec1, (char*)"Photos", photoDir, 0, 0, NULL);
	ChSidebarAddItem(sec1, (char*)"Downloads", download, 0, 0, NULL);

	ChSidebarSection* sec2 = ChSidebarAddSection(sb, (char*)"Xeneva");
	ChSidebarAddItem(sec2, (char*)"Trash", trsh, 0, 0, NULL);
	ChSidebarAddItem(sec2, (char*)"Gallery", gallery, 0, 0, NULL);

	// 7. Create Toolbar
	tb = ChToolbarCreate(0, 26, mainWin->info->width, 42, TOOLBAR_HORIZONTAL);
	tb->base.visible = true;
	tb->color_bg = 0xFF949494;

	ChIcon* doc = ChCreateIcon();
	ChIconOpen(doc, (char*)"/appdata/glimpse/icons/fav_t.bmp");
	ChIconRead(doc);

	ChIcon* dll = ChCreateIcon();
	ChIconOpen(dll, (char*)"/appdata/glimpse/icons/albm_t.bmp");
	ChIconRead(dll);

	ChIcon* exe = ChCreateIcon();
	ChIconOpen(exe, (char*)"/appdata/glimpse/icons/view_t.bmp");
	ChIconRead(exe);

	ChIcon* info = ChCreateIcon();
	ChIconOpen(info, (char*)"/appdata/glimpse/icons/info_t.bmp");
	ChIconRead(info);

	ChIcon* arcv = ChCreateIcon();
	ChIconOpen(arcv, (char*)"/appdata/glimpse/icons/arcv_t.bmp");
	ChIconRead(arcv);

	ChToolbarAddButton(tb, doc, doc->image.width, doc->image.height, (char*)"doc", NULL);
	ChToolbarAddButton(tb, dll, dll->image.width, dll->image.height, (char*)"dll", NULL);
	ChToolbarAddButton(tb, exe, exe->image.width, exe->image.height, (char*)"exe", NULL);
	ChToolbarAddSeparator(tb);
	ChToolbarAddButton(tb, trsh, trsh->image.width, trsh->image.height, (char*)"trash", NULL);
	ChToolbarAddButton(tb, arcv, arcv->image.width, arcv->image.height, (char*)"import", NULL);
	ChToolbarAddSeparator(tb);
	ChToolbarAddButton(tb, info, info->image.width, info->image.height, (char*)"info", NULL);

	// 8. Register widgets with window
	ChWindowAddWidget(mainWin, (ChWidget*)gv);
	ChWindowAddWidget(mainWin, (ChWidget*)viewer);
	ChWindowAddWidget(mainWin, (ChWidget*)sb);
	ChWindowAddWidget(mainWin, (ChWidget*)tb);

	// 9. Initial Paint
	ChWindowPaint(mainWin);
	ChWindowBroadcastIcon(app, (char*)"/icons/glim.bmp");

	// 10. Message Event Loop
	PostEvent e;
	memset(&e, 0, sizeof(PostEvent));

	while (1) {
		int err = _KeFileIoControl(app->postboxfd, POSTBOX_GET_EVENT, &e);
		WindowHandleMessage(&e);
		if (err == POSTBOX_NO_EVENT)
			_KePauseThread();
	}
}

GlimpBox* _Glimpse_get_main_glimp() {
	return NULL;
}