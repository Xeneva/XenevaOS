#include "glimpse_viewer.h"
#include <draw.h>
#include <color.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

GlimpseViewer* GlimpseViewerCreate(int x, int y, int w, int h, GlimpseBackCallback on_back, void* user_data) {
	GlimpseViewer* v = (GlimpseViewer*)malloc(sizeof(GlimpseViewer));
	if (!v) return NULL;
	memset(v, 0, sizeof(GlimpseViewer));

	v->wid.x = x;
	v->wid.y = y;
	v->wid.w = w;
	v->wid.h = h;
	v->wid.visible = true;
	v->wid.ChPaintHandler = GlimpseViewerPaint;
	v->wid.ChMouseEvent = GlimpseViewerMouse;

	v->zoom_percent = 100;
	v->pan_x = 0;
	v->pan_y = 0;
	v->is_dragging = false;

	// Back button (left side)
	v->btn_w = 72;
	v->btn_h = 28;
	v->btn_x = x + 12;
	v->btn_y = y + h - 38;

	// Zoom controls (right side)
	int rx = x + w - 12;

	// [ Fit ] button
	v->btn_fit_w = 40;
	v->btn_fit_h = 28;
	v->btn_fit_y = y + h - 38;
	rx -= v->btn_fit_w;
	v->btn_fit_x = rx;

	// Spacing
	rx -= 6;

	// [ + ] button
	v->btn_zoom_in_w = 28;
	v->btn_zoom_in_h = 28;
	v->btn_zoom_in_y = y + h - 38;
	rx -= v->btn_zoom_in_w;
	v->btn_zoom_in_x = rx;

	// Zoom text label area
	rx -= 44;
	v->lbl_zoom_x = rx;

	// [ - ] button
	rx -= 6;
	v->btn_zoom_out_w = 28;
	v->btn_zoom_out_h = 28;
	v->btn_zoom_out_y = y + h - 38;
	rx -= v->btn_zoom_out_w;
	v->btn_zoom_out_x = rx;

	v->on_back = on_back;
	v->user_data = user_data;

	return v;
}

void GlimpseViewerUnload(GlimpseViewer* v) {
	if (!v) return;
	GlimpseFreeImage(&v->current_img);
	GlimpseFreeImage(&v->scaled_img);
	v->has_image = false;
	v->current_path[0] = '\0';
	v->current_filename[0] = '\0';
	v->base_fit_w = 0;
	v->base_fit_h = 0;
	v->zoom_percent = 100;
	v->pan_x = 0;
	v->pan_y = 0;
	v->is_dragging = false;
}

static void GlimpseViewerApplyZoom(GlimpseViewer* v, int new_zoom) {
	if (!v || !v->has_image || !v->current_img.pixels) return;
	if (new_zoom < 25) new_zoom = 25;
	if (new_zoom > 400) new_zoom = 400;

	int old_zoom = v->zoom_percent;
	if (old_zoom <= 0) old_zoom = 100;

	int new_w = (v->base_fit_w * new_zoom) / 100;
	int new_h = (v->base_fit_h * new_zoom) / 100;

	if (new_w < 16) new_w = 16;
	if (new_h < 16) new_h = 16;
	if (new_w > 2560) new_w = 2560;
	if (new_h > 2560) new_h = 2560;

	GlimpseRawImage new_scaled;
	memset(&new_scaled, 0, sizeof(new_scaled));

	if (!GlimpseResizeImage(&v->current_img, new_w, new_h, &new_scaled)) {
		return;
	}

	GlimpseFreeImage(&v->scaled_img);
	v->scaled_img = new_scaled;

	// Scale pan offset with zoom to keep focal point steady
	v->pan_x = (v->pan_x * new_zoom) / old_zoom;
	v->pan_y = (v->pan_y * new_zoom) / old_zoom;

	int max_h = v->wid.h - 50;
	v->scaled_x = v->wid.x + (v->wid.w - new_w) / 2;
	v->scaled_y = v->wid.y + (max_h - new_h) / 2 + 5;

	v->zoom_percent = new_zoom;
}

void GlimpseViewerResetFit(GlimpseViewer* v) {
	if (!v || !v->has_image) return;
	v->pan_x = 0;
	v->pan_y = 0;
	GlimpseViewerApplyZoom(v, 100);
}

void GlimpseViewerZoomIn(GlimpseViewer* v) {
	if (!v || !v->has_image) return;
	int z = v->zoom_percent;
	if (z < 50) z = 50;
	else if (z < 75) z = 75;
	else if (z < 100) z = 100;
	else if (z < 125) z = 125;
	else if (z < 150) z = 150;
	else if (z < 200) z = 200;
	else if (z < 300) z = 300;
	else z = 400;
	GlimpseViewerApplyZoom(v, z);
}

void GlimpseViewerZoomOut(GlimpseViewer* v) {
	if (!v || !v->has_image) return;
	int z = v->zoom_percent;
	if (z > 300) z = 300;
	else if (z > 200) z = 200;
	else if (z > 150) z = 150;
	else if (z > 125) z = 125;
	else if (z > 100) z = 100;
	else if (z > 75) z = 75;
	else if (z > 50) z = 50;
	else z = 25;
	GlimpseViewerApplyZoom(v, z);
}

bool GlimpseViewerLoad(GlimpseViewer* v, const char* path) {
	if (!v || !path) return false;

	GlimpseViewerUnload(v);

	strncpy(v->current_path, path, sizeof(v->current_path) - 1);

	// Extract filename for display
	const char* slash = strrchr(path, '/');
	if (slash) {
		strncpy(v->current_filename, slash + 1, sizeof(v->current_filename) - 1);
	} else {
		strncpy(v->current_filename, path, sizeof(v->current_filename) - 1);
	}

	if (!GlimpseLoadImage(path, &v->current_img)) {
		return false;
	}

	// Calculate fitted dimensions within viewer viewport (reserving bottom 45px for control bar)
	int max_w = v->wid.w - 20;
	int max_h = v->wid.h - 50;
	int fit_w = v->current_img.width;
	int fit_h = v->current_img.height;

	if (fit_w > max_w || fit_h > max_h) {
		GlimpseFitDimensions(v->current_img.width, v->current_img.height, max_w, max_h, &fit_w, &fit_h);
	} else if (fit_w <= 48 && fit_h <= 48) {
		fit_w *= 2;
		fit_h *= 2;
	}

	v->base_fit_w = fit_w;
	v->base_fit_h = fit_h;
	v->zoom_percent = 100;
	v->pan_x = 0;
	v->pan_y = 0;
	v->is_dragging = false;

	if (!GlimpseResizeImage(&v->current_img, fit_w, fit_h, &v->scaled_img)) {
		GlimpseViewerUnload(v);
		return false;
	}

	// Center the image inside the display box
	v->scaled_x = v->wid.x + (v->wid.w - fit_w) / 2;
	v->scaled_y = v->wid.y + (max_h - fit_h) / 2 + 5;
	v->has_image = true;

	return true;
}

void GlimpseViewerPaint(ChWidget* wid, ChWindow* win) {
	GlimpseViewer* v = (GlimpseViewer*)wid;
	if (!v || !win || !win->canv) return;

	int canv_w = win->canv->canvasWidth;
	int canv_h = win->canv->canvasHeight;

	int view_x = v->wid.x;
	int view_y = v->wid.y;
	int view_w = v->wid.w;
	int view_h = v->wid.h - 45;

	// 1. Dark canvas backdrop for the photo viewport
	ChDrawRect(win->canv, view_x, view_y, view_w, view_h, 0xFF181818);

	// 2. Render scaled & panned photo with strict clipping to viewport & canvas buffer
	if (v->has_image && v->scaled_img.pixels && v->scaled_img.width > 0 && v->scaled_img.height > 0) {
		int img_x0 = v->scaled_x + v->pan_x;
		int img_y0 = v->scaled_y + v->pan_y;
		int img_x1 = img_x0 + v->scaled_img.width;
		int img_y1 = img_y0 + v->scaled_img.height;

		int clip_x0 = img_x0 < view_x ? view_x : img_x0;
		int clip_y0 = img_y0 < view_y ? view_y : img_y0;
		int clip_x1 = img_x1 > (view_x + view_w) ? (view_x + view_w) : img_x1;
		int clip_y1 = img_y1 > (view_y + view_h) ? (view_y + view_h) : img_y1;

		if (clip_x0 < 0) clip_x0 = 0;
		if (clip_y0 < 0) clip_y0 = 0;
		if (clip_x1 > canv_w) clip_x1 = canv_w;
		if (clip_y1 > canv_h) clip_y1 = canv_h;

		if (clip_x0 < clip_x1 && clip_y0 < clip_y1) {
			int img_w = v->scaled_img.width;
			for (int cy = clip_y0; cy < clip_y1; cy++) {
				int sy = cy - img_y0;
				uint32_t* dst = win->canv->buffer + cy * canv_w + clip_x0;
				const uint8_t* src = v->scaled_img.pixels + (sy * img_w + (clip_x0 - img_x0)) * 4;
				for (int cx = clip_x0; cx < clip_x1; cx++) {
					uint32_t r = src[0];
					uint32_t g = src[1];
					uint32_t b = src[2];
					*dst++ = (0xFF << 24) | (r << 16) | (g << 8) | b;
					src += 4;
				}
			}
		}
	}

	// 3. Bottom bar area
	int bar_y = v->wid.y + v->wid.h - 45;
	ChDrawRect(win->canv, v->wid.x, bar_y, v->wid.w, 45, 0xFF242424);
	ChDrawRectUnfilled(win->canv, v->wid.x, bar_y, v->wid.w, 1, 0xFF383838);

	// 4. "< Back" button
	uint32_t btn_bg = v->btn_clicked ? 0xFF0463C0 : (v->btn_hover ? 0xFF3F8EDA : 0xFF333333);
	uint32_t btn_border = v->btn_hover ? 0xFF60A5FA : 0xFF4A4A4A;

	ChDrawRect(win->canv, v->btn_x, v->btn_y, v->btn_w, v->btn_h, btn_bg);
	ChDrawRectUnfilled(win->canv, v->btn_x, v->btn_y, v->btn_w, v->btn_h, btn_border);

	ChFont* font = win->app->baseFont;
	ChFontSetSize(font, 12);
	ChFontDrawText(win->canv, font, (char*)"< Back", v->btn_x + 12, v->btn_y + 19, 14, 0xFFFFFFFF);

	// 5. Image info text (Filename & Dimensions)
	if (v->has_image) {
		char info_str[128];
		snprintf(info_str, sizeof(info_str), "%s  (%dx%d)",
				 v->current_filename, v->current_img.width, v->current_img.height);
		ChFontSetSize(font, 11);
		ChFontDrawText(win->canv, font, info_str, v->btn_x + v->btn_w + 12, v->btn_y + 18, 14, 0xFFCCCCCC);
	}

	// 6. Zoom controls
	// [ - ] Zoom Out button
	uint32_t zo_bg = v->btn_zoom_out_clicked ? 0xFF0463C0 : (v->btn_zoom_out_hover ? 0xFF3F8EDA : 0xFF333333);
	uint32_t zo_bd = v->btn_zoom_out_hover ? 0xFF60A5FA : 0xFF4A4A4A;
	ChDrawRect(win->canv, v->btn_zoom_out_x, v->btn_zoom_out_y, v->btn_zoom_out_w, v->btn_zoom_out_h, zo_bg);
	ChDrawRectUnfilled(win->canv, v->btn_zoom_out_x, v->btn_zoom_out_y, v->btn_zoom_out_w, v->btn_zoom_out_h, zo_bd);
	ChFontSetSize(font, 14);
	ChFontDrawText(win->canv, font, (char*)"-", v->btn_zoom_out_x + 10, v->btn_zoom_out_y + 19, 14, 0xFFFFFFFF);

	// Zoom text label
	char zoom_str[16];
	snprintf(zoom_str, sizeof(zoom_str), "%d%%", v->zoom_percent);
	ChFontSetSize(font, 11);
	ChFontDrawText(win->canv, font, zoom_str, v->lbl_zoom_x + 6, v->btn_zoom_out_y + 18, 14, 0xFFDDDDDD);

	// [ + ] Zoom In button
	uint32_t zi_bg = v->btn_zoom_in_clicked ? 0xFF0463C0 : (v->btn_zoom_in_hover ? 0xFF3F8EDA : 0xFF333333);
	uint32_t zi_bd = v->btn_zoom_in_hover ? 0xFF60A5FA : 0xFF4A4A4A;
	ChDrawRect(win->canv, v->btn_zoom_in_x, v->btn_zoom_in_y, v->btn_zoom_in_w, v->btn_zoom_in_h, zi_bg);
	ChDrawRectUnfilled(win->canv, v->btn_zoom_in_x, v->btn_zoom_in_y, v->btn_zoom_in_w, v->btn_zoom_in_h, zi_bd);
	ChFontSetSize(font, 14);
	ChFontDrawText(win->canv, font, (char*)"+", v->btn_zoom_in_x + 8, v->btn_zoom_in_y + 19, 14, 0xFFFFFFFF);

	// [ Fit ] Reset button
	uint32_t fit_bg = v->btn_fit_clicked ? 0xFF0463C0 : (v->btn_fit_hover ? 0xFF3F8EDA : 0xFF333333);
	uint32_t fit_bd = v->btn_fit_hover ? 0xFF60A5FA : 0xFF4A4A4A;
	ChDrawRect(win->canv, v->btn_fit_x, v->btn_fit_y, v->btn_fit_w, v->btn_fit_h, fit_bg);
	ChDrawRectUnfilled(win->canv, v->btn_fit_x, v->btn_fit_y, v->btn_fit_w, v->btn_fit_h, fit_bd);
	ChFontSetSize(font, 11);
	ChFontDrawText(win->canv, font, (char*)"Fit", v->btn_fit_x + 10, v->btn_fit_y + 18, 14, 0xFFFFFFFF);
}

void GlimpseViewerMouse(ChWidget* wid, ChWindow* win, int mx, int my, int button) {
	GlimpseViewer* v = (GlimpseViewer*)wid;
	if (!v || !win) return;

	int lx = mx - win->info->x;
	int ly = my - win->info->y;
	int bar_y = v->wid.y + v->wid.h - 45;

	// Mouse scroll wheel support for zooming
	if (button == DEODHAI_MOUSE_MSG_SCROLL_UP) {
		GlimpseViewerZoomIn(v);
		GlimpseViewerPaint(wid, win);
		ChWindowUpdate(win, v->wid.x, v->wid.y, v->wid.w, v->wid.h, false, true);
		return;
	} else if (button == DEODHAI_MOUSE_MSG_SCROLL_DOWN) {
		GlimpseViewerZoomOut(v);
		GlimpseViewerPaint(wid, win);
		ChWindowUpdate(win, v->wid.x, v->wid.y, v->wid.w, v->wid.h, false, true);
		return;
	}

	bool in_back = (lx >= v->btn_x && lx < v->btn_x + v->btn_w &&
				   ly >= v->btn_y && ly < v->btn_y + v->btn_h);
	bool in_zo = (lx >= v->btn_zoom_out_x && lx < v->btn_zoom_out_x + v->btn_zoom_out_w &&
				  ly >= v->btn_zoom_out_y && ly < v->btn_zoom_out_y + v->btn_zoom_out_h);
	bool in_zi = (lx >= v->btn_zoom_in_x && lx < v->btn_zoom_in_x + v->btn_zoom_in_w &&
				  ly >= v->btn_zoom_in_y && ly < v->btn_zoom_in_y + v->btn_zoom_in_h);
	bool in_fit = (lx >= v->btn_fit_x && lx < v->btn_fit_x + v->btn_fit_w &&
				   ly >= v->btn_fit_y && ly < v->btn_fit_y + v->btn_fit_h);
	bool in_viewport = (lx >= v->wid.x && lx < v->wid.x + v->wid.w &&
						ly >= v->wid.y && ly < bar_y);

	bool redraw_toolbar = false;
	bool redraw_viewport = false;

	// Hover updates
	if (in_back != v->btn_hover) { v->btn_hover = in_back; redraw_toolbar = true; }
	if (in_zo != v->btn_zoom_out_hover) { v->btn_zoom_out_hover = in_zo; redraw_toolbar = true; }
	if (in_zi != v->btn_zoom_in_hover) { v->btn_zoom_in_hover = in_zi; redraw_toolbar = true; }
	if (in_fit != v->btn_fit_hover) { v->btn_fit_hover = in_fit; redraw_toolbar = true; }

	if (button != 0) {
		// Active mouse drag
		if (v->is_dragging) {
			int dx = lx - v->drag_last_x;
			int dy = ly - v->drag_last_y;
			if (dx != 0 || dy != 0) {
				v->pan_x += dx;
				v->pan_y += dy;
				v->drag_last_x = lx;
				v->drag_last_y = ly;
				redraw_viewport = true;
			}
		} else if (in_back) {
			if (!v->btn_clicked) { v->btn_clicked = true; redraw_toolbar = true; }
		} else if (in_zo) {
			if (!v->btn_zoom_out_clicked) { v->btn_zoom_out_clicked = true; redraw_toolbar = true; }
		} else if (in_zi) {
			if (!v->btn_zoom_in_clicked) { v->btn_zoom_in_clicked = true; redraw_toolbar = true; }
		} else if (in_fit) {
			if (!v->btn_fit_clicked) { v->btn_fit_clicked = true; redraw_toolbar = true; }
		} else if (in_viewport) {
			v->is_dragging = true;
			v->drag_last_x = lx;
			v->drag_last_y = ly;
			win->focusedWidget = (void*)wid;
		}
	} else {
		// Button released
		if (v->btn_clicked) {
			v->btn_clicked = false;
			redraw_toolbar = true;
			if (in_back && v->on_back) {
				v->on_back(v->user_data);
				return;
			}
		}
		if (v->btn_zoom_out_clicked) {
			v->btn_zoom_out_clicked = false;
			if (in_zo) {
				GlimpseViewerZoomOut(v);
				redraw_viewport = true;
			}
			redraw_toolbar = true;
		}
		if (v->btn_zoom_in_clicked) {
			v->btn_zoom_in_clicked = false;
			if (in_zi) {
				GlimpseViewerZoomIn(v);
				redraw_viewport = true;
			}
			redraw_toolbar = true;
		}
		if (v->btn_fit_clicked) {
			v->btn_fit_clicked = false;
			if (in_fit) {
				GlimpseViewerResetFit(v);
				redraw_viewport = true;
			}
			redraw_toolbar = true;
		}
		if (v->is_dragging) {
			v->is_dragging = false;
			win->focusedWidget = NULL;
		}
	}

	if (redraw_viewport && redraw_toolbar) {
		GlimpseViewerPaint(wid, win);
		ChWindowUpdate(win, v->wid.x, v->wid.y, v->wid.w, v->wid.h, false, true);
	} else if (redraw_viewport) {
		GlimpseViewerPaint(wid, win);
		ChWindowUpdate(win, v->wid.x, v->wid.y, v->wid.w, v->wid.h - 45, false, true);
	} else if (redraw_toolbar) {
		GlimpseViewerPaint(wid, win);
		ChWindowUpdate(win, v->wid.x, bar_y, v->wid.w, 45, false, true);
	}
}
