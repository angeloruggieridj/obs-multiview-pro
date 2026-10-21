// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "tile_meters.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

using Groups = std::vector<std::vector<MeterSourceId>>;

/* The ids a group of the plan points at, in order. */
std::vector<MeterSourceId> ids_of(const MeterPlan &p, size_t group)
{
	std::vector<MeterSourceId> out;
	for (size_t i : p.groups.at(group))
		out.push_back(p.sources.at(i));
	return out;
}

bool inside(const RectF &in, const RectF &outer)
{
	return in.x >= outer.x - 1e-3f && in.y >= outer.y - 1e-3f && in.x + in.w <= outer.x + outer.w + 1e-3f &&
	       in.y + in.h <= outer.y + outer.h + 1e-3f;
}

} // namespace

/* ---- Sources: one meter per distinct source, groups in priority order ---- */

TEST_CASE("tile_meters_plan_empty")
{
	const MeterPlan p = plan_meter_sources({});
	CHECK(p.sources.empty());
	CHECK(p.groups.empty());
	CHECK(p.dropped == 0);

	/* tiles without a scene, or with a scene without audio: empty groups */
	const MeterPlan q = plan_meter_sources(Groups{{}, {}, {}});
	CHECK(q.sources.empty());
	REQUIRE(q.groups.size() == 3);
	for (const auto &g : q.groups)
		CHECK(g.empty());
	CHECK(q.dropped == 0);
}

TEST_CASE("tile_meters_plan_dedup_across_and_within_groups")
{
	/* source 7 in three tiles (one scene nested in others), 5 twice in one */
	const MeterPlan p = plan_meter_sources(Groups{{7, 5, 5}, {9, 7}, {}, {7}});
	CHECK(p.sources == std::vector<MeterSourceId>{7, 5, 9});
	REQUIRE(p.groups.size() == 4);
	CHECK(ids_of(p, 0) == std::vector<MeterSourceId>{7, 5});
	CHECK(ids_of(p, 1) == std::vector<MeterSourceId>{9, 7});
	CHECK(ids_of(p, 2).empty());
	CHECK(ids_of(p, 3) == std::vector<MeterSourceId>{7});
	CHECK(p.dropped == 0);
}

TEST_CASE("tile_meters_plan_ignores_null_ids")
{
	const MeterPlan p = plan_meter_sources(Groups{{0, 3, 0}, {0}});
	CHECK(p.sources == std::vector<MeterSourceId>{3});
	CHECK(ids_of(p, 0) == std::vector<MeterSourceId>{3});
	CHECK(ids_of(p, 1).empty());
}

TEST_CASE("tile_meters_plan_cap_keeps_the_first_groups")
{
	/* cap 3: groups earlier in the list win, later distinct sources are dropped */
	const MeterPlan p = plan_meter_sources(Groups{{1, 2}, {3, 4}, {5}, {2, 1}}, 3);
	CHECK(p.sources == std::vector<MeterSourceId>{1, 2, 3});
	CHECK(ids_of(p, 0) == std::vector<MeterSourceId>{1, 2});
	CHECK(ids_of(p, 1) == std::vector<MeterSourceId>{3}); /* 4 dropped */
	CHECK(ids_of(p, 2).empty());                            /* 5 dropped */
	CHECK(ids_of(p, 3) == std::vector<MeterSourceId>{2, 1}); /* already metered: free */
	CHECK(p.dropped == 2);
}

TEST_CASE("tile_meters_plan_dropped_counts_distinct_sources")
{
	const MeterPlan p = plan_meter_sources(Groups{{1}, {2, 2}, {2, 3}, {3}}, 1);
	CHECK(p.sources == std::vector<MeterSourceId>{1});
	CHECK(p.dropped == 2); /* 2 and 3, each once */
}

TEST_CASE("tile_meters_plan_cap_zero_and_exactly_full")
{
	const MeterPlan none = plan_meter_sources(Groups{{1, 2}}, 0);
	CHECK(none.sources.empty());
	CHECK(ids_of(none, 0).empty());
	CHECK(none.dropped == 2);

	const MeterPlan full = plan_meter_sources(Groups{{1, 2}, {3}}, 3);
	CHECK(full.sources.size() == 3);
	CHECK(full.dropped == 0);
}

TEST_CASE("tile_meters_plan_default_cap_is_the_plugin_cap")
{
	Groups g(40);
	MeterSourceId next = 1;
	for (auto &tile : g)
		for (int i = 0; i < 4; ++i)
			tile.push_back(next++); /* 160 distinct sources */
	const MeterPlan p = plan_meter_sources(g);
	CHECK(p.sources.size() == kMaxMeterSources);
	CHECK(p.dropped == 160 - kMaxMeterSources);
	/* deterministic: the first 128 in group order */
	for (size_t i = 0; i < p.sources.size(); ++i)
		CHECK(p.sources[i] == MeterSourceId(i + 1));
	CHECK(ids_of(p, 31).size() == 4);
	CHECK(ids_of(p, 32).empty());
	CHECK(plan_meter_sources(g) == p); /* same input, same plan */
}

/* ---- Geometry: two thin bars on the left edge of the tile ---- */

TEST_CASE("tile_meters_geometry_normal_tile")
{
	/* a scene tile of the 8-scene layout, canvas 1920x1080 shown at 1600x900 */
	const RectF inner = {486.f, 546.f, 468.f, 258.f};
	const TileMeterGeometry g = tile_meter_geometry(inner, 1600.f / 1920.f);
	REQUIRE(g.shown);
	CHECK(inside(g.column, inner));
	CHECK(inside(g.bar[0], g.column));
	CHECK(inside(g.bar[1], g.column));
	/* left edge, L then R, same size */
	CHECK(g.column.x - inner.x < inner.w * 0.05f);
	CHECK(g.bar[0].x < g.bar[1].x);
	CHECK(g.bar[0].x + g.bar[0].w <= g.bar[1].x);
	CHECK(g.bar[0].w == doctest::Approx(g.bar[1].w));
	CHECK(g.bar[0].h == doctest::Approx(g.bar[1].h));
	CHECK(g.bar[0].y == doctest::Approx(g.bar[1].y));
	/* thin: the backing takes a small part of the tile width, the bars most of its height */
	CHECK(g.column.w < inner.w * 0.08f);
	CHECK(g.bar[0].h > inner.h * 0.8f);
}

TEST_CASE("tile_meters_geometry_scales_with_the_tile")
{
	/* large enough that no minimum (bar width, gap) applies */
	const RectF small = {100.f, 50.f, 1000.f, 600.f};
	const RectF big = {100.f, 50.f, 2000.f, 1200.f};
	const TileMeterGeometry a = tile_meter_geometry(small, 1.f);
	const TileMeterGeometry b = tile_meter_geometry(big, 1.f);
	REQUIRE(a.shown);
	REQUIRE(b.shown);
	CHECK(b.bar[0].h == doctest::Approx(a.bar[0].h * 2.f));
	CHECK(b.bar[0].w == doctest::Approx(a.bar[0].w * 2.f));
	CHECK(b.column.w == doctest::Approx(a.column.w * 2.f));
	CHECK(b.bar[0].x - big.x == doctest::Approx((a.bar[0].x - small.x) * 2.f));
}

TEST_CASE("tile_meters_geometry_minimum_bar_width_in_device_pixels")
{
	/* a 32-scene tile drawn very small: each bar stays at least 2 device pixels wide */
	const RectF inner = {0.f, 0.f, 228.f, 128.f};
	const float px = 0.5f; /* 1 canvas unit = 0.5 device pixel */
	const TileMeterGeometry g = tile_meter_geometry(inner, px);
	REQUIRE(g.shown);
	CHECK(g.bar[0].w * px >= 2.f - 1e-4f);
	CHECK(g.bar[1].w * px >= 2.f - 1e-4f);
	/* a gap between L and R that survives the rounding to pixels */
	CHECK((g.bar[1].x - (g.bar[0].x + g.bar[0].w)) * px >= kTileMeterMinGapPx - 1e-4f);
	CHECK(inside(g.column, inner));
}

TEST_CASE("tile_meters_geometry_skips_tiny_tiles")
{
	/* below kTileMeterMinPx device pixels of height: no bars */
	const float h = kTileMeterMinPx - 1.f;
	CHECK_FALSE(tile_meter_geometry({0.f, 0.f, h * 16.f / 9.f, h}, 1.f).shown);
	CHECK(tile_meter_geometry({0.f, 0.f, kTileMeterMinPx * 16.f / 9.f, kTileMeterMinPx}, 1.f).shown);
	/* the same canvas tile, shown at a quarter of its size */
	CHECK_FALSE(tile_meter_geometry({0.f, 0.f, 228.f, 128.f}, 0.25f).shown);
	/* very narrow tiles too */
	CHECK_FALSE(tile_meter_geometry({0.f, 0.f, 20.f, 400.f}, 1.f).shown);
}

/* ---- Quads: everything one tile meter draws, for a single batched draw ---- */

namespace {
TileMeterGeometry sample_geometry()
{
	return tile_meter_geometry({486.f, 546.f, 468.f, 258.f}, 1600.f / 1920.f);
}
} // namespace

TEST_CASE("tile_meters_quads_silent")
{
	const ChannelBallistics st[2] = {};
	MeterQuad q[kTileMeterMaxQuads];
	/* backing + 3 dark bands per bar, nothing bright, no hold tick */
	REQUIRE(tile_meter_quads(sample_geometry(), st, 100, q) == 7);
	CHECK(q[0].argb == 0x99000000u);
	for (size_t i = 1; i < 7; ++i) {
		CHECK(q[i].argb != meter_color(MeterZone::Nominal, true));
		CHECK(q[i].x1 > q[i].x0);
		CHECK(q[i].y1 > q[i].y0);
	}
	CHECK(q[1].argb == meter_color(MeterZone::Nominal, false));
}

TEST_CASE("tile_meters_quads_loud")
{
	ChannelBallistics st[2];
	ballistics_step(st[0], -6.f, 0.016f);
	ballistics_step(st[1], -30.f, 0.016f);
	MeterQuad q[kTileMeterMaxQuads];
	const TileMeterGeometry g = sample_geometry();
	/* backing; L: 3 dark + 3 bright + tick; R: 3 dark + 1 bright + tick */
	REQUIRE(tile_meter_quads(g, st, 100, q) == 13);
	/* L: bright green / yellow / red after the dark ones, then the red tick */
	CHECK(q[4].argb == meter_color(MeterZone::Nominal, true));
	CHECK(q[5].argb == meter_color(MeterZone::Warning, true));
	CHECK(q[6].argb == meter_color(MeterZone::Error, true));
	CHECK(q[7].argb == peak_hold_color(-6.f));
	/* the bright red run of L ends at -6 dB (edges on whole units) */
	const float y_top = g.bar[0].y + g.bar[0].h * (1.f - db_to_fraction(-6.f));
	CHECK(std::fabs(q[6].y0 - y_top) <= 0.5f);
	CHECK(q[6].y0 == std::round(q[6].y0));
	/* R sits right of L */
	CHECK(q[11].x0 > q[4].x0);
	CHECK(q[11].argb == meter_color(MeterZone::Nominal, true));
	CHECK(q[12].argb == peak_hold_color(-30.f));
}

TEST_CASE("tile_meters_quads_opacity_and_hidden")
{
	ChannelBallistics st[2];
	ballistics_step(st[0], -6.f, 0.016f);
	ballistics_step(st[1], -6.f, 0.016f);
	MeterQuad q[kTileMeterMaxQuads];
	REQUIRE(tile_meter_quads(sample_geometry(), st, 50, q) == 15);
	CHECK(q[0].argb == scale_alpha(0x99000000u, 50));
	CHECK(q[4].argb == scale_alpha(meter_color(MeterZone::Nominal, true), 50));
	CHECK(tile_meter_quads(sample_geometry(), st, 0, q) == 0);
	CHECK(tile_meter_quads(TileMeterGeometry(), st, 100, q) == 0);
}

TEST_CASE("tile_meters_quads_gap_survives_rounding_on_a_large_display")
{
	/* display larger than the canvas (2 device px per canvas unit): the
	 * L and R bars must stay at least one canvas unit apart after rounding,
	 * wherever the tile sits */
	ChannelBallistics st[2];
	ballistics_step(st[0], -30.f, 0.016f);
	ballistics_step(st[1], -30.f, 0.016f);
	MeterQuad q[kTileMeterMaxQuads];
	for (int k = 0; k <= 20; ++k) {
		const float x = float(k) * 0.05f;
		INFO("tile x: " << x);
		const TileMeterGeometry g = tile_meter_geometry({x, 0.f, 228.f, 128.f}, 2.f);
		REQUIRE(g.shown);
		/* backing; L: 3 dark + 1 bright + tick; R: 3 dark + 1 bright + tick */
		REQUIRE(tile_meter_quads(g, st, 100, q) == 11);
		const MeterQuad &l = q[4], &r = q[9];
		CHECK(l.argb == meter_color(MeterZone::Nominal, true));
		CHECK(r.argb == meter_color(MeterZone::Nominal, true));
		CHECK(r.x0 - l.x1 >= 1.f);
	}
}

TEST_CASE("tile_meters_vertex_color")
{
	/* libobs vertex colours are R8G8B8A8 in memory: 0xAABBGGRR */
	CHECK(vertex_color(0xFF112233u) == 0xFF332211u);
	CHECK(vertex_color(0x80FF0000u) == 0x800000FFu);
	CHECK(vertex_color(0x00000000u) == 0u);
}

TEST_CASE("tile_meters_geometry_degenerate_input")
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	CHECK_FALSE(tile_meter_geometry({0.f, 0.f, 0.f, 0.f}, 1.f).shown);
	CHECK_FALSE(tile_meter_geometry({0.f, 0.f, -400.f, 225.f}, 1.f).shown);
	CHECK_FALSE(tile_meter_geometry({0.f, 0.f, 400.f, 225.f}, 0.f).shown);
	CHECK_FALSE(tile_meter_geometry({0.f, 0.f, 400.f, 225.f}, -1.f).shown);
	CHECK_FALSE(tile_meter_geometry({0.f, 0.f, 400.f, 225.f}, nan).shown);
	CHECK_FALSE(tile_meter_geometry({0.f, 0.f, nan, 225.f}, 1.f).shown);
	CHECK_FALSE(tile_meter_geometry({0.f, 0.f, 400.f, inf}, 1.f).shown);
	CHECK_FALSE(tile_meter_geometry({nan, 0.f, 400.f, 225.f}, 1.f).shown);
}
