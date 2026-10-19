// SPDX-License-Identifier: GPL-2.0-or-later
#include "layout_model.hpp"

#include <algorithm>
#include <cmath>

namespace {

/* All layouts: PVW and PGM side by side on top (square in normalized units,
 * i.e. same aspect as the canvas), a regular grid of equally sized scene
 * tiles below. */
struct GridSpec {
	int cols;
	int rows;
	float pvw_size; // PVW/PGM edge, normalized
	float pvw_x;    // PVW x; PGM follows immediately
	float cell;     // scene cell edge, normalized
	float grid_x;   // grid left
	float grid_y;   // grid top
};

GridSpec spec_for(Preset preset)
{
	switch (preset) {
	case Preset::Scenes4:
		return {4, 1, 1.f / 2, 0.f, 1.f / 4, 0.f, 1.f / 2};
	case Preset::Scenes16:
		return {4, 4, 1.f / 3, 1.f / 6, 1.f / 6, 1.f / 6, 1.f / 3};
	case Preset::Scenes24: // native HORIZONTAL_TOP_24_SCENES
		return {6, 4, 1.f / 3, 1.f / 6, 1.f / 6, 0.f, 1.f / 3};
	case Preset::Scenes32:
		return {8, 4, 1.f / 2, 0.f, 1.f / 8, 0.f, 1.f / 2};
	case Preset::Scenes8:
	default: // native HORIZONTAL_TOP_8_SCENES
		return {4, 2, 1.f / 2, 0.f, 1.f / 4, 0.f, 1.f / 2};
	}
}

} // namespace

std::vector<Tile> build_layout(Preset preset)
{
	const GridSpec s = spec_for(preset);
	std::vector<Tile> tiles;
	tiles.reserve(2 + static_cast<size_t>(s.cols) * static_cast<size_t>(s.rows));

	tiles.push_back({TileKind::Preview, -1, {s.pvw_x, 0.f, s.pvw_size, s.pvw_size}});
	tiles.push_back({TileKind::Program, -1, {s.pvw_x + s.pvw_size, 0.f, s.pvw_size, s.pvw_size}});

	int slot = 0;
	for (int r = 0; r < s.rows; ++r) {
		for (int c = 0; c < s.cols; ++c) {
			tiles.push_back({TileKind::Scene,
					 slot++,
					 {s.grid_x + static_cast<float>(c) * s.cell,
					  s.grid_y + static_cast<float>(r) * s.cell, s.cell, s.cell}});
		}
	}
	return tiles;
}

int hit_test(const std::vector<Tile> &tiles, float nx, float ny)
{
	for (size_t i = 0; i < tiles.size(); ++i) {
		const RectF &r = tiles[i].rect;
		if (nx >= r.x && nx < r.x + r.w && ny >= r.y && ny < r.y + r.h)
			return static_cast<int>(i);
	}
	return -1;
}

RectF fit_rect(float canvas_aspect, float cx, float cy)
{
	if (canvas_aspect <= 0.f || cx <= 0.f || cy <= 0.f)
		return {0.f, 0.f, 0.f, 0.f};

	float w = cx;
	float h = cx / canvas_aspect;
	if (h > cy) {
		h = cy;
		w = cy * canvas_aspect;
	}
	return {(cx - w) / 2.f, (cy - h) / 2.f, w, h};
}

std::vector<RectF> uncovered_areas(const std::vector<Tile> &tiles)
{
	constexpr float eps = 1e-4f;
	std::vector<RectF> out;

	float top_l = 1.f, top_r = 0.f, top_h = 0.f;
	float gx0 = 1.f, gy0 = 1.f, gx1 = 0.f, gy1 = 0.f;
	bool has_top = false, has_grid = false;

	for (const Tile &t : tiles) {
		const RectF &r = t.rect;
		if (t.kind == TileKind::Scene) {
			has_grid = true;
			gx0 = std::min(gx0, r.x);
			gy0 = std::min(gy0, r.y);
			gx1 = std::max(gx1, r.x + r.w);
			gy1 = std::max(gy1, r.y + r.h);
		} else {
			has_top = true;
			top_l = std::min(top_l, r.x);
			top_r = std::max(top_r, r.x + r.w);
			top_h = std::max(top_h, r.y + r.h);
		}
	}

	if (has_top) {
		if (top_l > eps)
			out.push_back({0.f, 0.f, top_l, top_h});
		if (top_r < 1.f - eps)
			out.push_back({top_r, 0.f, 1.f - top_r, top_h});
	}
	if (has_grid) {
		if (gy0 > top_h + eps)
			out.push_back({0.f, top_h, 1.f, gy0 - top_h});
		if (gx0 > eps)
			out.push_back({0.f, gy0, gx0, gy1 - gy0});
		if (gx1 < 1.f - eps)
			out.push_back({gx1, gy0, 1.f - gx1, gy1 - gy0});
		if (gy1 < 1.f - eps)
			out.push_back({0.f, gy1, 1.f, 1.f - gy1});
	}
	return out;
}

PixelRect to_pixel_rect(const RectF &r, int width, int height)
{
	auto edge = [](float n, int size) { return static_cast<int>(std::lround(static_cast<double>(n) * size)); };
	const int x0 = edge(r.x, width);
	const int y0 = edge(r.y, height);
	const int x1 = edge(r.x + r.w, width);
	const int y1 = edge(r.y + r.h, height);
	return {x0, y0, x1 - x0, y1 - y0};
}
