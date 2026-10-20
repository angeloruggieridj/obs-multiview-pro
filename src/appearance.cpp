// SPDX-License-Identifier: GPL-2.0-or-later
#include "appearance.hpp"

int clamp_percent(long long value)
{
	return value < 0 ? 0 : (value > 100 ? 100 : static_cast<int>(value));
}

Appearance normalized(const Appearance &a)
{
	Appearance out;
	out.border_rgb = a.border_rgb & 0xFFFFFFu;
	out.label_opacity = clamp_percent(a.label_opacity);
	out.meter_opacity = clamp_percent(a.meter_opacity);
	return out;
}

bool parse_color(const std::string &text, uint32_t &rgb)
{
	size_t i = 0;
	if (!text.empty() && text[0] == '#')
		i = 1;
	if (text.size() - i != 6)
		return false;
	uint32_t v = 0;
	for (; i < text.size(); ++i) {
		const char c = text[i];
		v <<= 4;
		if (c >= '0' && c <= '9')
			v |= static_cast<uint32_t>(c - '0');
		else if (c >= 'a' && c <= 'f')
			v |= static_cast<uint32_t>(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			v |= static_cast<uint32_t>(c - 'A' + 10);
		else
			return false;
	}
	rgb = v;
	return true;
}

std::string format_color(uint32_t rgb)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out = "#";
	for (int shift = 20; shift >= 0; shift -= 4)
		out.push_back(hex[(rgb >> shift) & 0xF]);
	return out;
}

uint32_t border_argb(const Appearance &a)
{
	return 0xFF000000u | (a.border_rgb & 0xFFFFFFu);
}

uint32_t scale_alpha(uint32_t argb, int percent)
{
	const uint32_t p = static_cast<uint32_t>(clamp_percent(percent));
	const uint32_t alpha = argb >> 24;
	const uint32_t scaled = (alpha * p + 50) / 100;
	return (scaled << 24) | (argb & 0xFFFFFFu);
}

int label_text_alpha(int percent)
{
	return clamp_percent(percent) * 255 / 100;
}

uint32_t label_box_argb(int percent)
{
	const uint32_t alpha = (0x33u * static_cast<uint32_t>(clamp_percent(percent)) + 25) / 50;
	return alpha << 24;
}
