// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "mixer_columns.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
const std::string kEllipsis = "\xE2\x80\xA6"; /* U+2026 */
} // namespace

TEST_CASE("mixer_select_columns_pgm_only_without_devices")
{
	CHECK(select_mixer_columns({false, false, false, false, false, false}) == std::vector<int>{0});
	CHECK(select_mixer_columns({}) == std::vector<int>{0});
}

TEST_CASE("mixer_select_columns_pgm_first_then_channel_order")
{
	/* Desktop Audio (1) and Mic/Aux (3), a gap at 2 */
	CHECK(select_mixer_columns({true, false, true, false, false, false}) == std::vector<int>{0, 1, 3});
	CHECK(select_mixer_columns({false, false, false, false, false, true}) == std::vector<int>{0, 6});
	CHECK(select_mixer_columns({true, true, true, true, true, true}) == std::vector<int>{0, 1, 2, 3, 4, 5, 6});
}

TEST_CASE("mixer_select_columns_capped")
{
	const std::vector<bool> ten(10, true);
	const std::vector<int> cols = select_mixer_columns(ten);
	REQUIRE(cols.size() == kMixerMaxColumns);
	CHECK(cols.front() == 0); /* PGM never dropped */
	CHECK(cols.back() == 7);  /* the first channels win */
	CHECK(select_mixer_columns(ten, 3) == std::vector<int>{0, 1, 2});
	CHECK(select_mixer_columns(ten, 1) == std::vector<int>{0});
	CHECK(select_mixer_columns(ten, 0).empty());
}

TEST_CASE("mixer_elide_name")
{
	CHECK(elide_name("Mic", 8) == "Mic");
	CHECK(elide_name("Mic/Aux", 7) == "Mic/Aux"); /* exactly fits */
	CHECK(elide_name("Desktop Audio", 8) == "Desktop" + kEllipsis);
	CHECK(elide_name("Desktop Audio", 9) == "Desktop" + kEllipsis); /* trailing space dropped */
	CHECK(elide_name("Mic/Aux 2", 5) == "Mic/" + kEllipsis);
	CHECK(elide_name("abc", 1) == kEllipsis);
	CHECK(elide_name("abc", 0).empty());
	CHECK(elide_name("", 4).empty());
}

TEST_CASE("mixer_elide_name_counts_code_points_not_bytes")
{
	/* "Micre" with e acute: 5 code points, 6 bytes */
	const std::string name = "Micr\xC3\xA9";
	CHECK(elide_name(name, 5) == name);
	CHECK(elide_name(name, 4) == "Mic" + kEllipsis);
	/* never cuts inside a sequence, even for a truncated tail */
	CHECK(elide_name("ab\xC3", 5) == "ab\xC3");
}

TEST_CASE("mixer_max_name_chars")
{
	CHECK(max_name_chars(55.f, 10.f) == 10); /* 55 / 5.5 */
	CHECK(max_name_chars(100.f, 10.f) > max_name_chars(50.f, 10.f));
	CHECK(max_name_chars(50.f, 20.f) < max_name_chars(50.f, 10.f));
	CHECK(max_name_chars(0.f, 10.f) == 0);
	CHECK(max_name_chars(-5.f, 10.f) == 0);
	CHECK(max_name_chars(50.f, 0.f) == 0);
	CHECK(max_name_chars(kNan, 10.f) == 0);
}

TEST_CASE("mixer_format_db_text")
{
	CHECK(format_db_text(-23.04f) == "-23.0");
	CHECK(format_db_text(-23.06f) == "-23.1");
	CHECK(format_db_text(-6.f) == "-6.0");
	CHECK(format_db_text(-59.94f) == "-59.9");
	CHECK(format_db_text(0.f) == "0.0");
	CHECK(format_db_text(-0.02f) == "0.0"); /* never "-0.0" */
	CHECK(format_db_text(3.f) == "0.0");    /* clipping reads 0 */
	/* silence */
	CHECK(format_db_text(kMeterFloorDb) == "--");
	CHECK(format_db_text(-100.f) == "--");
	CHECK(format_db_text(-kInf) == "--");
	CHECK(format_db_text(kNan) == "--");
	CHECK(format_db_text(-89.f) == "-89.0");
}

TEST_CASE("mixer_value_text_and_style")
{
	CHECK(mixer_value(false, -30.f).style == MixerValueStyle::Nominal);
	CHECK(mixer_value(false, -20.f).style == MixerValueStyle::Warning);
	CHECK(mixer_value(false, -10.f).style == MixerValueStyle::Warning);
	CHECK(mixer_value(false, -9.f).style == MixerValueStyle::Error);
	CHECK(mixer_value(false, 0.f).style == MixerValueStyle::Error);
	CHECK(mixer_value(false, -kInf).style == MixerValueStyle::Dim);
	CHECK(mixer_value(false, -kInf).text == "--");
	CHECK(mixer_value(false, -12.34f).text == "-12.3");
	/* muted wins over any level */
	CHECK(mixer_value(true, -6.f).text == "MUTE");
	CHECK(mixer_value(true, -6.f).style == MixerValueStyle::Mute);
	CHECK(mixer_value(true, -kInf).text == "MUTE");
}

TEST_CASE("mixer_db_text_update_rule")
{
	DbTextState s;
	/* nothing shown yet: always due */
	CHECK(db_text_update_due(s, -20.f, 0));

	s = {true, -20.f, 1000};
	/* too soon, whatever the change */
	CHECK_FALSE(db_text_update_due(s, -6.f, 1000));
	CHECK_FALSE(db_text_update_due(s, -6.f, 1499));
	/* time is there, change is not */
	CHECK_FALSE(db_text_update_due(s, -20.f, 5000));
	CHECK_FALSE(db_text_update_due(s, -20.4f, 5000)); /* rounds to the same dB */
	CHECK_FALSE(db_text_update_due(s, -19.6f, 5000));
	/* both */
	CHECK(db_text_update_due(s, -19.f, 1500));
	CHECK(db_text_update_due(s, -21.f, 1500));
	CHECK(db_text_update_due(s, -6.f, 9999));
	/* >= 1 dB on the rounded values: -20 -> -19 */
	CHECK(db_text_update_due(s, -18.6f, 2000));
	/* a clock that went backwards never makes it due */
	CHECK_FALSE(db_text_update_due(s, -6.f, 400));
}

TEST_CASE("mixer_db_text_update_rule_silence")
{
	DbTextState s = {true, -20.f, 0};
	CHECK(db_text_update_due(s, -kInf, 600));
	CHECK(db_text_update_due(s, kNan, 600));
	s = {true, kMeterFloorDb, 0};
	CHECK(db_text_update_due(s, -20.f, 600));
	CHECK_FALSE(db_text_update_due(s, -kInf, 600));
	CHECK_FALSE(db_text_update_due(s, -90.4f, 600));
}

TEST_CASE("mixer_geometry_columns")
{
	const RectF inner = {100.f, 50.f, 480.f, 270.f};
	const std::vector<MixerColumnGeometry> g = mixer_geometry(inner, 8);
	REQUIRE(g.size() == 8);
	for (size_t i = 0; i < g.size(); ++i) {
		const MixerColumnGeometry &c = g[i];
		/* everything inside the tile */
		for (const RectF *r : {&c.cell, &c.name, &c.value, &c.bar[0], &c.bar[1]}) {
			CHECK(r->x >= inner.x);
			CHECK(r->y >= inner.y);
			CHECK(r->x + r->w <= inner.x + inner.w + 0.001f);
			CHECK(r->y + r->h <= inner.y + inner.h + 0.001f);
			CHECK(r->w > 0.f);
			CHECK(r->h > 0.f);
		}
		/* name above the bars above the value */
		CHECK(c.name.y + c.name.h <= c.bar[0].y);
		CHECK(c.bar[0].y + c.bar[0].h <= c.value.y);
		/* two equal bars side by side, inside the cell, L left of R */
		CHECK(c.bar[0].w == doctest::Approx(c.bar[1].w));
		CHECK(c.bar[0].h == doctest::Approx(c.bar[1].h));
		CHECK(c.bar[0].x + c.bar[0].w <= c.bar[1].x);
		CHECK(c.bar[0].x >= c.cell.x);
		CHECK(c.bar[1].x + c.bar[1].w <= c.cell.x + c.cell.w + 0.001f);
		if (i > 0) {
			CHECK(c.cell.w == doctest::Approx(g[0].cell.w));
			CHECK(c.cell.x >= g[i - 1].cell.x + g[i - 1].cell.w - 0.001f); /* no overlap */
			CHECK(c.bar[0].h == doctest::Approx(g[0].bar[0].h));
		}
	}
}

TEST_CASE("mixer_geometry_is_proportional")
{
	/* The text image is built for the tile in device pixels, the bars drawn in
	 * canvas pixels: the layout must scale exactly. */
	const auto a = mixer_geometry({0.f, 0.f, 480.f, 270.f}, 5);
	const auto b = mixer_geometry({0.f, 0.f, 960.f, 540.f}, 5);
	REQUIRE(a.size() == 5);
	REQUIRE(b.size() == 5);
	for (size_t i = 0; i < a.size(); ++i) {
		CHECK(b[i].bar[1].x == doctest::Approx(a[i].bar[1].x * 2.f));
		CHECK(b[i].bar[0].y == doctest::Approx(a[i].bar[0].y * 2.f));
		CHECK(b[i].bar[0].h == doctest::Approx(a[i].bar[0].h * 2.f));
		CHECK(b[i].value.y == doctest::Approx(a[i].value.y * 2.f));
		CHECK(b[i].name.w == doctest::Approx(a[i].name.w * 2.f));
	}
}

TEST_CASE("mixer_geometry_degenerate")
{
	CHECK(mixer_geometry({0.f, 0.f, 480.f, 270.f}, 0).empty());
	CHECK(mixer_geometry({0.f, 0.f, 0.f, 270.f}, 3).empty());
	CHECK(mixer_geometry({0.f, 0.f, 480.f, -1.f}, 3).empty());
	/* more columns than the cap are not laid out */
	CHECK(mixer_geometry({0.f, 0.f, 480.f, 270.f}, 20).size() == kMixerMaxColumns);
	/* one column: bars not wider than the cell */
	const auto one = mixer_geometry({0.f, 0.f, 480.f, 270.f}, 1);
	REQUIRE(one.size() == 1);
	CHECK(one[0].bar[1].x + one[0].bar[1].w <= one[0].cell.x + one[0].cell.w);
}
