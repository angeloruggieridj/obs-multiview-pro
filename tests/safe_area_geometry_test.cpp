// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "safe_area_geometry.hpp"

/* Values from OBS 32.1 display-helpers.hpp: ACTION_SAFE_PERCENT 0.035,
 * GRAPHICS_SAFE_PERCENT 0.05, FOURBYTHREE_SAFE_PERCENT 0.1625, LINE_LENGTH 0.1. */

namespace {
void check_line(const std::vector<SafePoint> &line, const std::vector<SafePoint> &expected)
{
	REQUIRE(line.size() == expected.size());
	for (size_t i = 0; i < line.size(); ++i) {
		CHECK(line[i].x == doctest::Approx(expected[i].x));
		CHECK(line[i].y == doctest::Approx(expected[i].y));
	}
}
} // namespace

TEST_CASE("safe_area_six_lines_like_native")
{
	const SafeLines l = safe_area_lines();
	check_line(l[0], {{0.035f, 0.035f}, {0.035f, 0.965f}, {0.965f, 0.965f}, {0.965f, 0.035f}, {0.035f, 0.035f}});
	check_line(l[1], {{0.05f, 0.05f}, {0.05f, 0.95f}, {0.95f, 0.95f}, {0.95f, 0.05f}, {0.05f, 0.05f}});
	check_line(l[2], {{0.1625f, 0.05f}, {0.8375f, 0.05f}, {0.8375f, 0.95f}, {0.1625f, 0.95f}, {0.1625f, 0.05f}});
	check_line(l[3], {{0.0f, 0.5f}, {0.1f, 0.5f}});
	check_line(l[4], {{0.5f, 0.0f}, {0.5f, 0.1f}});
	check_line(l[5], {{1.0f, 0.5f}, {0.9f, 0.5f}});
}

TEST_CASE("safe_area_rectangles_are_closed_and_inside_the_cell")
{
	const SafeLines l = safe_area_lines();
	for (const auto &line : l)
		for (const SafePoint &p : line) {
			CHECK(p.x >= 0.0f);
			CHECK(p.x <= 1.0f);
			CHECK(p.y >= 0.0f);
			CHECK(p.y <= 1.0f);
		}
	for (int i = 0; i < 3; ++i) {
		REQUIRE(l[i].size() == 5);
		CHECK(l[i].front().x == l[i].back().x);
		CHECK(l[i].front().y == l[i].back().y);
	}
}
