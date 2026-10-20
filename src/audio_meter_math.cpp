// SPDX-License-Identifier: GPL-2.0-or-later
#include "audio_meter_math.hpp"

#include <algorithm>
#include <cmath>

float db_to_fraction(float db, float min_db)
{
	if (!(db == db) || !(min_db < 0.f)) /* NaN, or a degenerate range */
		return 0.f;
	return std::min(1.f, std::max(0.f, (db - min_db) / (0.f - min_db)));
}

float sanitize_db(float db)
{
	if (!(db == db))
		return kMeterFloorDb;
	return std::min(0.f, std::max(kMeterFloorDb, db));
}

void ballistics_step(ChannelBallistics &b, float input_db, float dt, float decay, float hold_s)
{
	const float in = sanitize_db(input_db);
	const float step = (dt == dt) ? std::min(kMeterMaxDt, std::max(0.f, dt)) : 0.f;

	if (in >= b.peak_db) {
		b.peak_db = in; /* attack is immediate */
	} else {
		b.peak_db = std::min(0.f, std::max(b.peak_db - decay * step, in));
	}

	if (in >= b.hold_db) {
		b.hold_db = in;
		b.hold_age = 0.f;
	} else {
		b.hold_age += step;
		if (b.hold_age > hold_s) {
			b.hold_db = in;
			b.hold_age = 0.f;
		}
	}
}

void meter_frame_step(ChannelBallistics &b, float input_db, float dt, float decay, float hold_s)
{
	if (dt == dt && dt > kMeterMaxDt) {
		b = ChannelBallistics();
		input_db = kMeterFloorDb;
	}
	ballistics_step(b, input_db, dt, decay, hold_s);
}

int meter_segments(float fraction, MeterSegment out[3])
{
	if (!(fraction == fraction) || fraction <= 0.f)
		return 0;
	const float top = std::min(1.f, fraction);
	const float bounds[3] = {0.f, db_to_fraction(kMeterWarningDb), db_to_fraction(kMeterErrorDb)};
	const float tops[3] = {bounds[1], bounds[2], 1.f};
	const MeterZone zones[3] = {MeterZone::Nominal, MeterZone::Warning, MeterZone::Error};
	int n = 0;
	for (int i = 0; i < 3; ++i) {
		if (top <= bounds[i])
			break;
		out[n++] = {bounds[i], std::min(top, tops[i]), zones[i]};
	}
	return n;
}

uint32_t meter_color(MeterZone zone, bool foreground)
{
	switch (zone) {
	case MeterZone::Nominal:
		return foreground ? 0xFF4CFF4Cu : 0xFF267F26u;
	case MeterZone::Warning:
		return foreground ? 0xFFFFFF4Cu : 0xFF7F7F26u;
	case MeterZone::Error:
		return foreground ? 0xFFFF4C4Cu : 0xFF7F2626u;
	}
	return 0xFF000000u;
}

uint32_t peak_hold_color(float db)
{
	if (db < kMeterMinInputDb)
		return meter_color(MeterZone::Nominal, false);
	if (db < kMeterWarningDb)
		return meter_color(MeterZone::Nominal, true);
	if (db < kMeterErrorDb)
		return meter_color(MeterZone::Warning, true);
	if (db < kMeterClipDb)
		return meter_color(MeterZone::Error, true);
	return 0xFFFFFFFFu;
}

MeterGeometry meter_geometry(const RectF &t, bool with_scale)
{
	MeterGeometry g = {};
	const float h = std::max(0.f, t.h);
	const float margin = h * 0.04f;
	const float bar_w = std::max(with_scale ? 4.f : 3.f, h * (with_scale ? 0.032f : 0.022f));
	const float gap = std::max(1.f, bar_w * 0.15f);
	const float pad = margin * 0.5f; /* inside the backing */

	const float cap_h = with_scale ? h * 0.05f : 0.f;
	const float col_x = t.x + margin;
	const float col_y = t.y + margin;
	const float col_h = std::max(0.f, h - 2.f * margin);

	const float bars_x = col_x + pad;
	const float bars_y = col_y + pad + (with_scale ? h * 0.012f : 0.f); /* room for the "0" */
	const float bars_h = std::max(0.f, col_h - 2.f * pad - cap_h - (with_scale ? h * 0.012f : 0.f));

	g.bar[0] = {bars_x, bars_y, bar_w, bars_h};
	g.bar[1] = {bars_x + bar_w + gap, bars_y, bar_w, bars_h};
	const float bars_right = g.bar[1].x + bar_w;

	float col_right = bars_right + pad;
	if (with_scale) {
		const float num_w = h * 0.085f;
		g.scale = {bars_right + pad, bars_y - h * 0.012f, num_w, bars_h + h * 0.024f};
		g.captions = {bars_x, bars_y + bars_h, bars_right - bars_x, cap_h};
		col_right = g.scale.x + g.scale.w + pad * 0.5f;
	}
	g.column = {col_x, col_y, col_right - col_x, col_h};
	return g;
}

float meter_y_for_db(const MeterGeometry &g, float db)
{
	return g.bar[0].y + g.bar[0].h * (1.f - db_to_fraction(db));
}
