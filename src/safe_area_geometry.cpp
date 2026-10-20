// SPDX-License-Identifier: GPL-2.0-or-later
#include "safe_area_geometry.hpp"

namespace {
/* Rec. ITU-R BT.1848-1 / EBU R 95, as OBS 32.1 display-helpers.hpp. */
constexpr float kActionSafe = 0.035f;       /* 3.5% */
constexpr float kGraphicsSafe = 0.05f;      /* 5.0% */
constexpr float kFourByThreeSafe = 0.1625f; /* 16.25% */
constexpr float kLineLength = 0.1f;
} // namespace

SafeLines safe_area_lines()
{
	SafeLines l;
	/* All essential action should be placed inside this area. */
	l[0] = {{kActionSafe, kActionSafe},
		{kActionSafe, 1 - kActionSafe},
		{1 - kActionSafe, 1 - kActionSafe},
		{1 - kActionSafe, kActionSafe},
		{kActionSafe, kActionSafe}};
	/* All graphics should be placed inside this area. */
	l[1] = {{kGraphicsSafe, kGraphicsSafe},
		{kGraphicsSafe, 1 - kGraphicsSafe},
		{1 - kGraphicsSafe, 1 - kGraphicsSafe},
		{1 - kGraphicsSafe, kGraphicsSafe},
		{kGraphicsSafe, kGraphicsSafe}};
	/* 4:3 safe area for widescreen. */
	l[2] = {{kFourByThreeSafe, kGraphicsSafe},
		{1 - kFourByThreeSafe, kGraphicsSafe},
		{1 - kFourByThreeSafe, 1 - kGraphicsSafe},
		{kFourByThreeSafe, 1 - kGraphicsSafe},
		{kFourByThreeSafe, kGraphicsSafe}};
	l[3] = {{0.0f, 0.5f}, {kLineLength, 0.5f}};
	l[4] = {{0.5f, 0.0f}, {0.5f, kLineLength}};
	l[5] = {{1.0f, 0.5f}, {1 - kLineLength, 0.5f}};
	return l;
}
