#ifndef __GLIMPSE_VIEWER_H__
#define __GLIMPSE_VIEWER_H__

#include <chitralekha.h>
#include <widgets/base.h>
#include <widgets/window.h>
#include "glimpse_image.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*GlimpseBackCallback)(void* user_data);

typedef struct {
	ChWidget wid;

	char current_path[256];
	char current_filename[64];
	GlimpseRawImage current_img;
	GlimpseRawImage scaled_img;
	int scaled_x;
	int scaled_y;
	bool has_image;

	// Zoom & Pan state
	int base_fit_w;
	int base_fit_h;
	int zoom_percent;
	int pan_x;
	int pan_y;

	// Dragging state
	bool is_dragging;
	int drag_last_x;
	int drag_last_y;

	// Back button geometry (relative to window)
	int btn_x;
	int btn_y;
	int btn_w;
	int btn_h;
	bool btn_hover;
	bool btn_clicked;

	// Zoom Out button [-]
	int btn_zoom_out_x;
	int btn_zoom_out_y;
	int btn_zoom_out_w;
	int btn_zoom_out_h;
	bool btn_zoom_out_hover;
	bool btn_zoom_out_clicked;

	// Zoom In button [+]
	int btn_zoom_in_x;
	int btn_zoom_in_y;
	int btn_zoom_in_w;
	int btn_zoom_in_h;
	bool btn_zoom_in_hover;
	bool btn_zoom_in_clicked;

	// Fit / Reset button [Fit]
	int btn_fit_x;
	int btn_fit_y;
	int btn_fit_w;
	int btn_fit_h;
	bool btn_fit_hover;
	bool btn_fit_clicked;

	int lbl_zoom_x;

	GlimpseBackCallback on_back;
	void* user_data;
} GlimpseViewer;

GlimpseViewer* GlimpseViewerCreate(int x, int y, int w, int h, GlimpseBackCallback on_back, void* user_data);

bool GlimpseViewerLoad(GlimpseViewer* viewer, const char* path);

void GlimpseViewerUnload(GlimpseViewer* viewer);

void GlimpseViewerResetFit(GlimpseViewer* viewer);

void GlimpseViewerZoomIn(GlimpseViewer* viewer);

void GlimpseViewerZoomOut(GlimpseViewer* viewer);

void GlimpseViewerPaint(ChWidget* wid, ChWindow* win);

void GlimpseViewerMouse(ChWidget* wid, ChWindow* win, int x, int y, int button);

#ifdef __cplusplus
}
#endif

#endif // __GLIMPSE_VIEWER_H__
