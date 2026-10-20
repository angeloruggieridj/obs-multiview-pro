// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Pure safe-area geometry (no OBS / Qt dependency).

#include <array>
#include <vector>

struct SafePoint {
	float x;
	float y;
};

/* The six polylines of the native multiview safe areas (OBS 32.1
 * frontend/utility/display-helpers.hpp InitSafeAreas), in normalized 0..1
 * coordinates of the preview cell, each drawn as a line strip:
 *   0 action safe, 1 graphics (title) safe, 2 4:3 safe, 3 left, 4 top, 5 right
 * centre marks. */
constexpr int kSafeLineCount = 6;
using SafeLines = std::array<std::vector<SafePoint>, kSafeLineCount>;

SafeLines safe_area_lines();
