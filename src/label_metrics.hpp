// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Tile label geometry (no OBS / Qt dependency), copied from the native
// multiview (OBS 32.1 frontend/components/Multiview.cpp: CreateLabel,
// labelOffset, Render) and the text source it uses for the labels
// (plugins/obs-text/gdiplus/obs-text.cpp: CalculateTextSizes).
//
// Units: "canvas" = base-canvas pixels (the multiview is drawn in canvas
// coordinates, then the viewport maps it to the window). "label px" = pixels of
// the native label text source, drawn scaled by LabelScale.

#include "layout_model.hpp"

#include <cstdint>

/* Native Multiview::thickness (outline width, canvas px). */
constexpr float kLabelThickness = 6.0f;

/* Font size the native gives the label text source: int(h / 9.81), with
 * h = canvas height / 2 for PVW/PGM and / 3 for scenes (integer division first,
 * as the native size_t arithmetic). The text source uses it as the font cell
 * height (LOGFONT lfHeight > 0) in label px. */
int label_face_size(uint32_t canvas_height, bool preview_or_program);

struct LabelSize {
	int cx; /* label px */
	int cy;
};

/* Texture size of the native text source (non-wrapped, no outline) for a
 * measured text box: cx = LONG(w + 1e-4), cy = max(face, LONG(h + 1e-4)), both
 * rounded up to even, at least 2. */
LabelSize label_text_size(float text_w, float text_h, int face_size);

struct LabelScale {
	float x; /* native ppiScaleX: (pvwprgCX - 2*thickness) / canvas width */
	float y; /* native ppiScaleY */
	int n;   /* native labelOffset divisor: twice the PVW scale factor */
};

/* For a layout whose PVW/PGM tiles are pvw_cx x pvw_cy canvas px. All labels
 * (scene tiles too) are scaled by the PVW/PGM scale, as natively. */
LabelScale label_scale(float canvas_w, float canvas_h, float pvw_cx, float pvw_cy);

/* Native labelOffset: (uint32(tile_cx) / 2) - uint32(label_cx / n), in canvas
 * px. The native wraps around (unsigned) when the label is too wide; this
 * returns 0 instead. Labels are elided to label_max_cx, so it does not happen. */
float label_offset(int label_cx, int n, float tile_cx);

struct LabelGeometry {
	RectF box;  /* canvas: background box, (cx, cy + 2*thickness) label px scaled */
	RectF text; /* canvas: where the text texture lands (box shifted by thickness label px) */
};

/* Placement inside `tile` (outer rect in canvas px, outline included), as the
 * native Render: x = tile.x + offset, y = tile bottom - cy*scale.y - 3*thickness. */
LabelGeometry label_geometry(const RectF &tile, LabelSize size, const LabelScale &s);

/* Widest label (LabelSize::cx, label px) that stays inside the tile outline:
 * left edge >= thickness and right edge <= tile width - thickness. */
int label_max_cx(float tile_cx, const LabelScale &s);

struct LabelRaster {
	int w; /* texture size in texels */
	int h;
};

/* Texture size for drawing a `size` label at `display_scale` (display px per
 * canvas px), so a texel is about one screen pixel. Between 2 and 4096. */
LabelRaster label_raster(LabelSize size, const LabelScale &s, float display_scale);

/* How the multiview canvas lands on the display: the viewport the renderer
 * sets (origin and size in device pixels, the canvas fitted and centered like
 * the native GetScaleAndCenterPos) and the resulting canvas -> device scale.
 * Text is rasterized at device pixels and must be drawn 1:1, so its quads are
 * placed with this map. No display known: the identity (1 px per canvas px). */
struct DeviceMap {
	int x;    /* viewport origin, device px */
	int y;
	float sx; /* device px per canvas px */
	float sy;
};

DeviceMap device_map(uint32_t base_cx, uint32_t base_cy, int display_cx, int display_cy);

/* A rectangle of whole device pixels. */
struct DeviceRect {
	int x;
	int y;
	int w;
	int h;
};

/* The device pixels a canvas rect covers: each edge rounded to the nearest
 * pixel boundary, which is the set of pixels whose centers lie inside. */
DeviceRect device_rect(const RectF &canvas, const DeviceMap &m);

/* Canvas rect whose edges fall exactly on `r`'s pixel boundaries. */
RectF device_to_canvas(const DeviceRect &r, const DeviceMap &m);

/* Font pixel size that gives a label with native face size `face_size`
 * (label px, the font cell height) a cell of about face_size * label_to_device
 * device px; `font_px_per_cell` is the font's pixel size over its cell height.
 * Whole pixels, at least 1. */
int label_font_px(int face_size, double font_px_per_cell, float label_to_device);
