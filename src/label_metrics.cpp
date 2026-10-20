// SPDX-License-Identifier: GPL-2.0-or-later
#include "label_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

/* EPSILON of libobs graphics/math-defs.h, used by obs-text. */
constexpr float kEpsilon = 1e-4f;
constexpr int kMinTexSize = 2;
constexpr int kMaxTexSize = 4096;

int even_up(int v)
{
	return v + v % 2;
}

int clamp_tex(float v)
{
	if (!(v > 0.f)) /* also NaN */
		return kMinTexSize;
	if (v >= float(kMaxTexSize))
		return kMaxTexSize;
	return std::max(kMinTexSize, int(std::ceil(v)));
}

} // namespace

int label_face_size(uint32_t canvas_height, bool preview_or_program)
{
	/* CreateLabel(name, h / 2 or h / 3) with uint32 h, then int(h / 9.81). */
	const uint32_t h = preview_or_program ? canvas_height / 2 : canvas_height / 3;
	return int(double(h) / 9.81);
}

LabelSize label_text_size(float text_w, float text_h, int face_size)
{
	LabelSize s;
	s.cx = text_w > 0.f ? int(text_w + kEpsilon) : 0;
	s.cy = text_h < float(face_size) ? face_size : int(text_h + kEpsilon);
	s.cx = std::max(kMinTexSize, even_up(s.cx));
	s.cy = std::max(kMinTexSize, even_up(s.cy));
	return s;
}

LabelScale label_scale(float canvas_w, float canvas_h, float pvw_cx, float pvw_cy)
{
	if (canvas_w <= 0.f || canvas_h <= 0.f || pvw_cx <= 0.f || pvw_cy <= 0.f)
		return {0.f, 0.f, 0};
	LabelScale s;
	s.x = (pvw_cx - 2.f * kLabelThickness) / canvas_w;
	s.y = (pvw_cy - 2.f * kLabelThickness) / canvas_h;
	/* Native: 4 when PVW is 1/2 of the canvas, 6 when 1/3 (and 8, 10 for
	 * 1/4, 1/5 in the scenes-only layouts). */
	s.n = 2 * int(std::lround(canvas_w / pvw_cx));
	return s;
}

float label_offset(int label_cx, int n, float tile_cx)
{
	const uint32_t half = (tile_cx > 0.f ? uint32_t(tile_cx) : 0u) / 2u;
	if (n <= 0)
		return float(half);
	const uint32_t w = uint32_t(float(std::max(0, label_cx)) * (1.0f / float(n)));
	return w > half ? 0.f : float(half - w);
}

LabelGeometry label_geometry(const RectF &tile, LabelSize size, const LabelScale &s)
{
	const float x = tile.x + label_offset(size.cx, s.n, tile.w);
	const float y = tile.y + tile.h - float(size.cy) * s.y - kLabelThickness * 3.f;
	LabelGeometry g;
	g.box = {x, y, float(size.cx) * s.x, float(size.cy + 2 * int(kLabelThickness)) * s.y};
	g.text = {x, y + kLabelThickness * s.y, float(size.cx) * s.x, float(size.cy) * s.y};
	return g;
}

int label_max_cx(float tile_cx, const LabelScale &s)
{
	const float inner = tile_cx - 2.f * kLabelThickness;
	if (inner <= 0.f || s.x <= 0.f)
		return 0;
	/* Upper bound from the width alone, then step down until the native
	 * offset also keeps the left edge inside (a few steps at most). */
	int lw = int(std::floor(inner / s.x));
	const RectF tile = {0.f, 0.f, tile_cx, 0.f};
	while (lw > 0) {
		const LabelGeometry g = label_geometry(tile, {lw, 0}, s);
		if (g.box.x >= kLabelThickness && g.box.x + g.box.w <= tile_cx - kLabelThickness)
			break;
		--lw;
	}
	return lw;
}

LabelRaster label_raster(LabelSize size, const LabelScale &s, float display_scale)
{
	return {clamp_tex(float(size.cx) * s.x * display_scale), clamp_tex(float(size.cy) * s.y * display_scale)};
}

/* The renderer's viewport (MultiviewRenderer::render: scale_and_center, then a
 * viewport of int(canvas * scale) with the canvas mapped onto it). The float
 * arithmetic is the same so the numbers agree to the bit. */
DeviceMap device_map(uint32_t base_cx, uint32_t base_cy, int display_cx, int display_cy)
{
	const DeviceMap identity = {0, 0, 1.f, 1.f};
	if (base_cx == 0 || base_cy == 0 || display_cx <= 0 || display_cy <= 0)
		return identity;

	const double window_aspect = double(display_cx) / double(display_cy);
	const double base_aspect = double(base_cx) / double(base_cy);
	float scale;
	int new_cx, new_cy;
	if (window_aspect > base_aspect) {
		scale = float(display_cy) / float(base_cy);
		new_cx = int(double(display_cy) * base_aspect);
		new_cy = display_cy;
	} else {
		scale = float(display_cx) / float(base_cx);
		new_cx = display_cx;
		new_cy = int(float(display_cx) / base_aspect);
	}
	const int view_cx = int(float(base_cx) * scale);
	const int view_cy = int(float(base_cy) * scale);
	if (view_cx <= 0 || view_cy <= 0)
		return identity;
	return {display_cx / 2 - new_cx / 2, display_cy / 2 - new_cy / 2, float(view_cx) / float(base_cx),
		float(view_cy) / float(base_cy)};
}

DeviceRect device_rect(const RectF &canvas, const DeviceMap &m)
{
	auto edge = [](double v) { return int(std::lround(v)); };
	const int x0 = edge(double(m.x) + double(canvas.x) * double(m.sx));
	const int y0 = edge(double(m.y) + double(canvas.y) * double(m.sy));
	const int x1 = edge(double(m.x) + (double(canvas.x) + double(canvas.w)) * double(m.sx));
	const int y1 = edge(double(m.y) + (double(canvas.y) + double(canvas.h)) * double(m.sy));
	return {x0, y0, x1 - x0, y1 - y0};
}

RectF device_to_canvas(const DeviceRect &r, const DeviceMap &m)
{
	return {float((double(r.x) - double(m.x)) / double(m.sx)), float((double(r.y) - double(m.y)) / double(m.sy)),
		float(double(r.w) / double(m.sx)), float(double(r.h) / double(m.sy))};
}

int label_font_px(int face_size, double font_px_per_cell, float label_to_device)
{
	const double px = double(face_size) * font_px_per_cell * double(label_to_device);
	if (!(px >= 1.0)) /* also NaN */
		return 1;
	if (px >= double(std::numeric_limits<int>::max()))
		return std::numeric_limits<int>::max();
	return int(std::lround(px));
}
