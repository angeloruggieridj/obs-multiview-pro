// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Appearance settings shared by every multiview window (pure, no Qt / OBS).
//
//  - border_rgb: the frame colour drawn under and around the tiles (the grey
//    that fills the gaps between tiles and frames empty slots; the native
//    multiview's "outerColor"). The red / green program and preview outlines
//    are not affected.
//  - label_opacity: 0..100 %, the opacity of the tile labels. 50 is the native
//    look (white text at alpha 127 over a 0x33 black box).
//  - meter_opacity: 0..100 %, a multiplier for everything the audio meters
//    draw. 100 is the native look.

#include <cstdint>
#include <string>

constexpr uint32_t kDefaultBorderRgb = 0x999999; /* native outerColor 0xFF999999 */
constexpr int kDefaultLabelOpacity = 50;
constexpr int kDefaultMeterOpacity = 100;

struct Appearance {
	uint32_t border_rgb = kDefaultBorderRgb; /* 0xRRGGBB */
	int label_opacity = kDefaultLabelOpacity;
	int meter_opacity = kDefaultMeterOpacity;

	bool operator==(const Appearance &o) const
	{
		return border_rgb == o.border_rgb && label_opacity == o.label_opacity &&
		       meter_opacity == o.meter_opacity;
	}
	bool operator!=(const Appearance &o) const { return !(*this == o); }
};

/* Into [0, 100]. */
int clamp_percent(long long value);

/* Fields clamped / masked to their valid ranges. */
Appearance normalized(const Appearance &a);

/* "#RRGGBB" or "RRGGBB", any case, nothing else (no spaces, no short form).
 * On failure `rgb` is left untouched. */
bool parse_color(const std::string &text, uint32_t &rgb);
/* "#RRGGBB" (upper case); bits above 24 are ignored. */
std::string format_color(uint32_t rgb);

/* Opaque ARGB of the border colour. */
uint32_t border_argb(const Appearance &a);

/* `argb` with its alpha multiplied by percent / 100 (percent clamped). */
uint32_t scale_alpha(uint32_t argb, int percent);

/* Label text alpha 0..255: percent * 255 / 100 (50 -> 127, the native value). */
int label_text_alpha(int percent);
/* Label box colour (ARGB, black): alpha scales with the text opacity,
 * alpha = round(0x33 * percent / 50), so 50 -> 0x33 (native) and 100 -> 0x66;
 * 0 -> fully transparent. */
uint32_t label_box_argb(int percent);
