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
