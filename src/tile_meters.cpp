// SPDX-License-Identifier: GPL-2.0-or-later
#include "tile_meters.hpp"

#include <algorithm>
#include <cmath>

namespace {

size_t index_of(const std::vector<MeterSourceId> &v, MeterSourceId id)
{
	return size_t(std::find(v.begin(), v.end(), id) - v.begin());
}

bool finite(float v)
{
	return std::isfinite(v);
}

} // namespace

MeterPlan plan_meter_sources(const std::vector<std::vector<MeterSourceId>> &groups, size_t cap)
{
	MeterPlan plan;
	plan.groups.resize(groups.size());
	std::vector<MeterSourceId> dropped;
	for (size_t g = 0; g < groups.size(); ++g) {
		std::vector<size_t> &out = plan.groups[g];
		for (MeterSourceId id : groups[g]) {
			if (id == 0)
				continue;
			size_t i = index_of(plan.sources, id);
			if (i == plan.sources.size()) {
				if (plan.sources.size() >= cap) {
					if (index_of(dropped, id) == dropped.size())
						dropped.push_back(id);
					continue;
				}
				plan.sources.push_back(id);
			}
			if (std::find(out.begin(), out.end(), i) == out.end())
				out.push_back(i);
		}
	}
	plan.dropped = dropped.size();
	return plan;
}

TileMeterGeometry tile_meter_geometry(const RectF &t, float px)
{
	TileMeterGeometry g;
	if (!finite(px) || !(px > 0.f) || !finite(t.x) || !finite(t.y) || !finite(t.w) || !finite(t.h) || !(t.w > 0.f) ||
	    !(t.h > 0.f))
		return g;
	if (t.h * px < kTileMeterMinPx || t.w * px < kTileMeterMinWidthPx)
		return g;

	/* The Preview meter's proportions (meter_geometry, no scale). */
	const float h = t.h;
	const float margin = h * 0.04f;
	const float bar_w = std::max(kTileMeterMinBarPx / px, h * 0.022f);
	const float gap = std::max(kTileMeterMinGapPx / px, bar_w * 0.15f);
	const float pad = margin * 0.5f;

	const float col_x = t.x + margin;
	const float col_y = t.y + margin;
	const float col_h = h - 2.f * margin;
	const float bars_x = col_x + pad;
	const float bars_y = col_y + pad;
	const float bars_h = col_h - 2.f * pad;

	g.bar[0] = {bars_x, bars_y, bar_w, bars_h};
	g.bar[1] = {bars_x + bar_w + gap, bars_y, bar_w, bars_h};
	const float col_right = g.bar[1].x + bar_w + pad;
	g.column = {col_x, col_y, col_right - col_x, col_h};
	if (col_right > t.x + t.w)
		return TileMeterGeometry(); /* cannot happen above the minimum width; belt */
	g.shown = true;
	return g;
}

size_t tile_meter_quads(const TileMeterGeometry &g, const ChannelBallistics (&st)[2], int opacity,
			MeterQuad (&out)[kTileMeterMaxQuads])
{
	if (!g.shown || opacity <= 0)
		return 0;
	size_t n = 0;
	auto add = [&](float x0, float y0, float x1, float y1, uint32_t argb) {
		const float rx0 = std::round(x0), ry0 = std::round(y0), rx1 = std::round(x1), ry1 = std::round(y1);
		if (!(rx1 > rx0) || !(ry1 > ry0) || n >= kTileMeterMaxQuads)
			return;
		out[n++] = {rx0, ry0, rx1, ry1, opacity < 100 ? scale_alpha(argb, opacity) : argb};
	};
	auto run = [&](const RectF &bar, const MeterSegment &s, bool fg) {
		add(bar.x, bar.y + bar.h * (1.f - s.f1), bar.x + bar.w, bar.y + bar.h * (1.f - s.f0), meter_color(s.zone, fg));
	};

	add(g.column.x, g.column.y, g.column.x + g.column.w, g.column.y + g.column.h, 0x99000000u);
	for (int c = 0; c < 2; ++c) {
		const RectF &bar = g.bar[c];
		MeterSegment seg[3];
		int k = meter_segments(1.f, seg);
		for (int i = 0; i < k; ++i)
			run(bar, seg[i], false);
		k = meter_segments(db_to_fraction(st[c].peak_db), seg);
		for (int i = 0; i < k; ++i)
			run(bar, seg[i], true);
		if (st[c].hold_db > kMeterMinDb) {
			const float th = std::max(1.f, bar.h * 0.005f);
			const float y = std::min(bar.y + bar.h * (1.f - db_to_fraction(st[c].hold_db)), bar.y + bar.h - th);
			add(bar.x, y, bar.x + bar.w, y + th, peak_hold_color(st[c].hold_db));
		}
	}
	return n;
}
