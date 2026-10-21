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
	CHECK_FALSE(a.always_on_top);
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

TEST_CASE("appearance_always_on_top")
{
	Appearance a;
	a.always_on_top = true;
	CHECK(a != Appearance()); /* part of equality: a toggle is a change to save */
	CHECK(normalized(a).always_on_top); /* kept as it is */
	a.always_on_top = false;
	CHECK(a == Appearance());
}

TEST_CASE("appearance_follow_saved_toggles")
{
	/* The settings dialog holds an unsaved preview; a window menu then saves a
	 * toggle. The dialog must show (and preview to every window) the saved
	 * toggles with the rest of its preview. */
	Appearance preview;
	preview.border_rgb = 0x123456;
	preview.label_opacity = 80;

	/* saved off, preview on: the menu unticks -> off everywhere */
	preview.always_on_top = true;
	Appearance saved;
	saved.always_on_top = false;
	Appearance shown = follow_saved_toggles(preview, saved);
	CHECK_FALSE(shown.always_on_top);
	CHECK(shown.border_rgb == 0x123456u); /* the other preview values stay */
	CHECK(shown.label_opacity == 80);

	/* saved on, preview off: the menu ticks -> on everywhere */
	preview.always_on_top = false;
	saved.always_on_top = true;
	shown = follow_saved_toggles(preview, saved);
	CHECK(shown.always_on_top);
	CHECK(shown.border_rgb == 0x123456u);

	/* the tile meters toggle follows the same rule, both directions */
	preview.tile_meters = true;
	saved.tile_meters = false;
	CHECK_FALSE(follow_saved_toggles(preview, saved).tile_meters);
	preview.tile_meters = false;
	saved.tile_meters = true;
	CHECK(follow_saved_toggles(preview, saved).tile_meters);

	/* the saved colours / opacities never replace the preview's */
	saved.border_rgb = 0xABCDEF;
	saved.meter_opacity = 10;
	CHECK(follow_saved_toggles(preview, saved).border_rgb == 0x123456u);
	CHECK(follow_saved_toggles(preview, saved).meter_opacity == preview.meter_opacity);
	/* the result is normalized */
	preview.label_opacity = 400;
	CHECK(follow_saved_toggles(preview, saved).label_opacity == 100);
}

TEST_CASE("appearance_topmost_check_action")
{
	/* while on top: always re-assert (idempotent, and it also restores the
	 * order inside the topmost band when a window was raised above) */
	CHECK(topmost_check_action(true, true) == TopmostAction::Assert);
	CHECK(topmost_check_action(true, false) == TopmostAction::Assert);
	/* while off: release only a window that is still topmost */
	CHECK(topmost_check_action(false, true) == TopmostAction::Release);
	CHECK(topmost_check_action(false, false) == TopmostAction::None);
}

TEST_CASE("appearance_tile_meters")
{
	Appearance a;
	CHECK(a.tile_meters == kDefaultTileMeters);
	a.tile_meters = !kDefaultTileMeters;
	CHECK(a != Appearance()); /* part of equality: a toggle is a change to save */
	CHECK(normalized(a).tile_meters == !kDefaultTileMeters); /* kept as it is */
	CHECK(normalized(a) == a);
	a.tile_meters = kDefaultTileMeters;
	CHECK(a == Appearance());
}
