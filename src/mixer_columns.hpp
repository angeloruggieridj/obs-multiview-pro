// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Pure logic of the mixer tile (no OBS / Qt dependency): which columns it
// shows, how names and dB values are written, when a dB text is refreshed, and
// where everything goes inside the tile.

#include "audio_meter_math.hpp"
#include "layout_model.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

constexpr size_t kMixerMaxColumns = 8;  /* PGM + up to 7 devices */
constexpr int kMixerGlobalChannels = 6; /* OBS output channels 1..6 (Desktop Audio, Mic/Aux ...) */
constexpr int kMixerPgm = 0;            /* column id of the program mix */

/* Which columns the tile shows, as ids: 0 = PGM, n = OBS output channel n
 * (1-based). `present[i]` says whether output channel i + 1 holds a source.
 * PGM comes first, then the present channels in channel order, at most
 * `max_columns` in all (the first channels win). max_columns 0 -> empty. */
std::vector<int> select_mixer_columns(const std::vector<bool> &present, size_t max_columns = kMixerMaxColumns);

/* `utf8` shortened to `max_chars` code points: unchanged when it fits, else
 * its first max_chars - 1 code points (trailing spaces dropped) followed by
 * U+2026. max_chars 0 -> "". Never cuts inside a UTF-8 sequence. */
std::string elide_name(const std::string &utf8, size_t max_chars);

/* How many characters of a name fit a column `cell_w` wide at `font_px`
 * (average advance taken as 0.55 em). 0 for a non-positive / NaN input. */
size_t max_name_chars(float cell_w, float font_px);

/* One decimal: "-23.0", "-6.0", "0.0" (never "-0.0"; above 0 reads 0). Silence
 * (-inf, NaN, kMeterFloorDb and below) -> "--". */
std::string format_db_text(float db);

enum class MixerValueStyle { Dim, Nominal, Warning, Error, Mute };

struct MixerValue {
	std::string text;
	MixerValueStyle style;
};

/* The text under a column: "MUTE" when muted (whatever the level), else the dB
 * text, coloured by the zone of the level (green / yellow / red, grey for "--"). */
MixerValue mixer_value(bool muted, float db);

/* What a column's dB text shows now (UI thread bookkeeping). */
struct DbTextState {
	bool shown = false;           /* a text has been shown */
	float db = kMeterFloorDb;     /* the level it was made from */
	int64_t at_ms = 0;            /* when (any monotonic clock) */
};

constexpr int64_t kMixerTextMinIntervalMs = 500; /* at most 2 refreshes a second */
constexpr int kMixerTextMinStepDb = 1;           /* of the rounded value */

/* True when the dB text must be made again for `new_db` at `now_ms`: nothing
 * shown yet, or at least kMixerTextMinIntervalMs since the last one AND the
 * level, rounded to whole dB (silence = kMeterFloorDb), differs from the shown
 * one by at least kMixerTextMinStepDb. A clock that went backwards is never
 * due. */
bool db_text_update_due(const DbTextState &state, float new_db, int64_t now_ms);

/* Where the parts of one column go. Canvas units, same as the tile rect. */
struct MixerColumnGeometry {
	RectF cell;     /* the whole column */
	RectF name;     /* text row above the bars */
	RectF bar[2];   /* L, R: top = 0 dB, bottom = -60 dB */
	RectF value;    /* text row under the bars */
};

/* Lays out min(n, kMixerMaxColumns) equal columns left to right inside
 * `inner`: name row, two bars, value row. Every length is proportional to the
 * width or the height of `inner`, so the layout of the tile in device pixels
 * (text image) and in canvas pixels (bars) coincide. Empty for n == 0 or an
 * empty rect. */
std::vector<MixerColumnGeometry> mixer_geometry(const RectF &inner, size_t n);

/* What the text image of the tile shows for one column. */
struct MixerColumnView {
	std::string name;
	std::string value;
	MixerValueStyle style = MixerValueStyle::Dim;

	bool operator==(const MixerColumnView &o) const
	{
		return name == o.name && value == o.value && style == o.style;
	}
};
