// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Pure layout geometry (no OBS / Qt dependency).

#include <vector>

struct RectF {
	float x;
	float y;
	float w;
	float h;
};

enum class TileKind { Preview, Program, Scene };

struct Tile {
	TileKind kind;
	int scene_slot; // -1 for Preview / Program
	RectF rect;     // normalized 0..1 coordinates
};

enum class Preset { Scenes4, Scenes8, Scenes16, Scenes24, Scenes32 };

/* Tiles in order: Preview, Program, then Scene slots 0..N-1. */
std::vector<Tile> build_layout(Preset preset);

/* Index of the tile containing (nx, ny), or -1. Rects are half-open
 * [x, x+w) x [y, y+h), so a shared edge belongs to exactly one tile. */
int hit_test(const std::vector<Tile> &tiles, float nx, float ny);

/* Areas of the canvas covered by no tile, in normalized coordinates: beside
 * the PVW/PGM pair, between that pair and the grid, beside and below the grid.
 * The renderer paints them like the native 24-scene layout paints the areas
 * beside PVW/PGM. Empty for layouts that fill the canvas. */
std::vector<RectF> uncovered_areas(const std::vector<Tile> &tiles);

/* Largest rect of aspect `canvas_aspect` centered in a cx x cy area
 * (letter/pillar box), like the native GetScaleAndCenterPos. */
RectF fit_rect(float canvas_aspect, float cx, float cy);

struct PixelRect {
	int x;
	int y;
	int w;
	int h;
};

/* Maps a normalized rect into a width x height pixel area. The edges are
 * rounded (not the sizes), so tiles that share an edge in normalized space
 * share it in pixels too: no gaps, no overlaps. */
PixelRect to_pixel_rect(const RectF &r, int width, int height);
