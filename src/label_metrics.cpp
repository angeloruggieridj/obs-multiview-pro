// SPDX-License-Identifier: GPL-2.0-or-later
#include "label_metrics.hpp"

#include <algorithm>
#include <cmath>

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
