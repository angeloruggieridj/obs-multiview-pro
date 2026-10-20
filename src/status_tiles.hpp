// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "layout_model.hpp"
#include "status_format.hpp"

#include <cstdint>
#include <memory>
#include <vector>

/* One rasterized status tile (ON AIR, REC, Clock, Mixer), immutable once built:
 * plain memory, uploaded by the renderer on the graphics thread only when a new
 * atlas is built. */
struct StatusTileImage {
	int w = 0; /* texels (device pixels) */
	int h = 0;
	std::vector<uint8_t> pixels; /* w * h * 4: B, G, R, 0xFF (opaque) */
};

/* Draws the status tiles with Qt (QPainter) in the look of the ATEM multiview
 * reference: black tile, big centered title (red when the output is active,
 * dark grey when not), big white values, small grey captions.
 *
 * UI thread only. Not a widget: the multiview window is itself the native
 * display surface, so the tiles are drawn inside the obs_display from the
 * images this produces. nullptr for a non-status kind or an empty size. */
std::shared_ptr<const StatusTileImage> render_status_tile(TileKind kind, const StatusSnapshot &snapshot, int w, int h);
