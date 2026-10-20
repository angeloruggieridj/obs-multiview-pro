// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "audio_meter_math.hpp"

#include <cstdint>
#include <memory>
#include <vector>

/* The text of the big (Program) meter, rasterized once: the dB scale numbers
 * (0, -5 ... -60) at the height of their level, and the "L" / "R" captions
 * under the bars. One transparent image that covers MeterGeometry::column, so
 * the renderer draws it as a single quad over the bars. Immutable plain
 * memory; uploaded by the graphics thread when a new one is published. */
struct MeterScaleImage {
	int w = 0; /* texels (device pixels) */
	int h = 0;
	std::vector<uint8_t> pixels; /* w * h * 4: B, G, R, A, straight alpha (GS_BGRA) */
};

/* UI thread only (QPainter / QFont). `g` is in canvas pixels; the image is
 * w x h texels for g.column (display pixels, so text is crisp). nullptr when
 * the size is empty or there is no scale in `g`. Text smaller than 6 px is
 * left out (unreadable). */
std::shared_ptr<const MeterScaleImage> render_meter_scale(const MeterGeometry &g, int w, int h);
