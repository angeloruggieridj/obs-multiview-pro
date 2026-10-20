// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "appearance.hpp"

#include <string>

TEST_CASE("appearance_defaults_match_the_native_look")
{
	const Appearance a;
	CHECK(a.border_rgb == 0x999999u);
	CHECK(a.label_opacity == 50);
	CHECK(a.meter_opacity == 100);
	CHECK(a == Appearance());
	CHECK(border_argb(a) == 0xFF999999u);
	/* Native label: text alpha 50 * 255 / 100 = 127, box 0x33000000. */
	CHECK(label_text_alpha(50) == 127);
	CHECK(label_box_argb(50) == 0x33000000u);
	CHECK(scale_alpha(0x99000000u, 100) == 0x99000000u);
}

TEST_CASE("appearance_label_formula")
{
	CHECK(label_text_alpha(0) == 0);
	CHECK(label_text_alpha(100) == 255);
	CHECK(label_box_argb(0) == 0u);
	CHECK(label_box_argb(100) == 0x66000000u);
	CHECK(label_text_alpha(-5) == 0);
	CHECK(label_text_alpha(500) == 255);
	CHECK(label_box_argb(500) == 0x66000000u);
	CHECK(label_box_argb(25) == 0x1A000000u);
	/* monotonic */
	for (int i = 1; i <= 100; ++i) {
		CHECK(label_text_alpha(i) >= label_text_alpha(i - 1));
		CHECK(label_box_argb(i) >= label_box_argb(i - 1));
	}
}

TEST_CASE("appearance_scale_alpha")
{
	CHECK(scale_alpha(0xFF112233u, 0) == 0x00112233u);
	CHECK(scale_alpha(0xFF112233u, 50) == 0x80112233u);
	CHECK(scale_alpha(0xFF112233u, 100) == 0xFF112233u);
	CHECK(scale_alpha(0xFF112233u, 400) == 0xFF112233u);
	CHECK(scale_alpha(0xFF112233u, -4) == 0x00112233u);
	CHECK(scale_alpha(0x80112233u, 50) == 0x40112233u);
}

TEST_CASE("appearance_clamp_percent")
{
	CHECK(clamp_percent(-3) == 0);
	CHECK(clamp_percent(0) == 0);
	CHECK(clamp_percent(42) == 42);
	CHECK(clamp_percent(100) == 100);
	CHECK(clamp_percent(500) == 100);
	CHECK(clamp_percent(-9999999999LL) == 0);
	CHECK(clamp_percent(9999999999LL) == 100);
}

TEST_CASE("appearance_color_parse")
{
	uint32_t c = 0;
	CHECK(parse_color("#336699", c));
	CHECK(c == 0x336699u);
	CHECK(parse_color("336699", c));
	CHECK(c == 0x336699u);
	CHECK(parse_color("#aBcDeF", c));
	CHECK(c == 0xABCDEFu);
	CHECK(parse_color("000000", c));
	CHECK(c == 0u);
	c = 7;
	for (const char *bad : {"", "#", "#GGGGGG", "12345", "#1234567", "#12345", "0x336699", " 336699", "336699 ",
				"#33 699", "##336699", "\xC3\xA8\xC3\xA8\xC3\xA8", "-12345"}) {
		CHECK_FALSE(parse_color(bad, c));
		CHECK(c == 7u); /* untouched */
	}
	CHECK_FALSE(parse_color(std::string("33669\0", 6), c));
	CHECK_FALSE(parse_color(std::string(100000, 'a'), c));
}

TEST_CASE("appearance_color_format")
{
	CHECK(format_color(0x336699u) == "#336699");
	CHECK(format_color(0u) == "#000000");
	CHECK(format_color(0xABCDEFu) == "#ABCDEF");
	CHECK(format_color(0xFF123456u) == "#123456"); /* alpha dropped */
	uint32_t c = 0;
	CHECK(parse_color(format_color(0x0A0B0Cu), c));
	CHECK(c == 0x0A0B0Cu);
}

TEST_CASE("appearance_normalized")
{
	Appearance a;
	a.border_rgb = 0xFF336699u;
	a.label_opacity = 900;
	a.meter_opacity = -2;
	const Appearance n = normalized(a);
	CHECK(n.border_rgb == 0x336699u);
	CHECK(n.label_opacity == 100);
	CHECK(n.meter_opacity == 0);
	CHECK(n != a);
}
