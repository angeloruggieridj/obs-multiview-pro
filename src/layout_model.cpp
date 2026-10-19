// SPDX-License-Identifier: GPL-2.0-or-later
#include "layout_model.hpp"

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
