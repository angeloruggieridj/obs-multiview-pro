// SPDX-License-Identifier: GPL-2.0-or-later
#include "mixer_columns.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

MixerSelection select_mixer_columns(const MixerSourceLists &in, size_t max_columns)
{
	/* Distinct sources in priority order (a few dozen at most: linear search). */
	std::vector<MixerSourceId> order;
	auto add = [&order](const std::vector<MixerSourceId> &ids) {
		for (MixerSourceId id : ids)
			if (id != kMixerPgm && std::find(order.begin(), order.end(), id) == order.end())
				order.push_back(id);
	};
	add(in.devices);
	add(in.program);
	if (in.studio_mode)
		add(in.preview);

	MixerSelection out;
	const size_t room = max_columns > 0 ? max_columns - 1 : 0; /* beside PGM */
	const size_t kept = std::min(order.size(), room);
	out.columns.assign(order.begin(), order.begin() + std::ptrdiff_t(kept));
	out.dropped = order.size() - kept;
	if (max_columns > 0)
		out.columns.push_back(kMixerPgm); /* last: rightmost */
	return out;
}

namespace {

/* Bytes of the UTF-8 sequence that starts with `lead` (1 for a stray byte). */
size_t sequence_length(unsigned char lead)
{
	if (lead >= 0xF0)
		return 4;
	if (lead >= 0xE0)
		return 3;
	if (lead >= 0xC0)
		return 2;
	return 1;
}

/* Byte offset after the first `count` code points of `s` (clamped to the end). */
size_t offset_after(const std::string &s, size_t count)
{
	size_t pos = 0;
	for (size_t i = 0; i < count && pos < s.size(); ++i)
		pos += sequence_length(static_cast<unsigned char>(s[pos]));
	return std::min(pos, s.size());
}

size_t code_point_count(const std::string &s, size_t stop_after)
{
	size_t pos = 0, n = 0;
	while (pos < s.size() && n < stop_after) {
		pos += sequence_length(static_cast<unsigned char>(s[pos]));
		++n;
	}
	return n;
}

} // namespace

std::string elide_name(const std::string &utf8, size_t max_chars)
{
	if (max_chars == 0)
		return std::string();
	/* Counting stops one past the limit: enough to know it does not fit. */
	if (code_point_count(utf8, max_chars + 1) <= max_chars)
		return utf8;
	std::string head = utf8.substr(0, offset_after(utf8, max_chars - 1));
	while (!head.empty() && head.back() == ' ')
		head.pop_back();
	return head + "\xE2\x80\xA6";
}

std::string elide_name_middle(const std::string &utf8, size_t max_chars)
{
	if (max_chars == 0)
		return std::string();
	if (code_point_count(utf8, max_chars + 1) <= max_chars)
		return utf8;
	const size_t keep = max_chars - 1; /* code points besides the ellipsis */
	const size_t head_n = keep - keep / 2;
	const size_t tail_n = keep / 2;

	std::string head = utf8.substr(0, offset_after(utf8, head_n));
	while (!head.empty() && head.back() == ' ')
		head.pop_back();

	/* Start of the last tail_n code points: walk back over lead bytes. */
	size_t tail_pos = utf8.size();
	for (size_t i = 0; i < tail_n && tail_pos > 0; ++i) {
		--tail_pos;
		while (tail_pos > 0 && (static_cast<unsigned char>(utf8[tail_pos]) & 0xC0) == 0x80)
			--tail_pos;
	}
	tail_pos = std::max(tail_pos, offset_after(utf8, head_n)); /* never overlap the head */
	std::string tail = utf8.substr(tail_pos);
	size_t lead = 0;
	while (lead < tail.size() && tail[lead] == ' ')
		++lead;
	tail.erase(0, lead);
	return head + "\xE2\x80\xA6" + tail;
}

size_t max_name_chars(float cell_w, float font_px)
{
	if (!(cell_w > 0.f) || !(font_px > 0.f))
		return 0;
	const float n = std::floor(cell_w / (0.55f * font_px));
	return n >= 1.f ? size_t(n) : 0;
}

std::string format_db_text(float db)
{
	if (!(db == db) || db <= kMeterFloorDb)
		return "--";
	double r = std::round(double(sanitize_db(db)) * 10.0) / 10.0;
	if (r == 0.0)
		r = 0.0; /* -0.0 -> 0.0 */
	char buf[32];
	std::snprintf(buf, sizeof buf, "%.1f", r);
	return buf;
}

MixerValue mixer_value(bool muted, float db)
{
	if (muted)
		return {std::string(), MixerValueStyle::Mute};
	MixerValue v = {format_db_text(db), MixerValueStyle::Dim};
	if (v.text == "--")
		return v;
	const float s = sanitize_db(db);
	v.style = s < kMeterWarningDb ? MixerValueStyle::Nominal
					: (s < kMeterErrorDb ? MixerValueStyle::Warning : MixerValueStyle::Error);
	return v;
}

bool db_text_update_due(const DbTextState &state, float new_db, int64_t now_ms)
{
	if (!state.shown)
		return true;
	if (now_ms - state.at_ms < kMixerTextMinIntervalMs)
		return false;
	const float shown = sanitize_db(state.db);
	const float now = sanitize_db(new_db);
	if ((shown <= kMeterFloorDb) != (now <= kMeterFloorDb))
		return true; /* "--" <-> a value */
	return std::fabs(now - shown) >= kMixerTextMinStepDb;
}

std::vector<MixerColumnGeometry> mixer_geometry(const RectF &inner, size_t n)
{
	std::vector<MixerColumnGeometry> out;
	if (n == 0 || !(inner.w > 0.f) || !(inner.h > 0.f))
		return out;
	n = std::min(n, kMixerMaxColumns);

	const float margin_x = inner.w * 0.02f;
	const float margin_y = inner.h * 0.03f;
	const float row_h = inner.h * 0.11f; /* name row, value row */
	const float gap_y = inner.h * 0.02f;

	const float cell_w = (inner.w - 2.f * margin_x) / float(n);
	const float name_y = inner.y + margin_y;
	const float bars_y = name_y + row_h + gap_y;
	const float value_y = inner.y + inner.h - margin_y - row_h;
	const float bars_h = value_y - gap_y - bars_y;
	const float bar_w = std::min(cell_w * 0.30f, inner.h * 0.09f);
	const float bar_gap = bar_w * 0.25f;

	out.reserve(n);
	for (size_t i = 0; i < n; ++i) {
		MixerColumnGeometry c = {};
		const float x = inner.x + margin_x + cell_w * float(i);
		c.cell = {x, name_y, cell_w, value_y + row_h - name_y};
		c.name = {x, name_y, cell_w, row_h};
		c.value = {x, value_y, cell_w, row_h};
		const float bars_x = x + (cell_w - (2.f * bar_w + bar_gap)) * 0.5f;
		c.bar[0] = {bars_x, bars_y, bar_w, bars_h};
		c.bar[1] = {bars_x + bar_w + bar_gap, bars_y, bar_w, bars_h};
		out.push_back(c);
	}
	return out;
}
