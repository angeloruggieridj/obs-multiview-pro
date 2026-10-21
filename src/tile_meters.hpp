// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Pure logic of the audio meters on the scene tiles (no OBS / Qt dependency):
// which sources get a meter, and where the bars go inside a tile.

#include "layout_model.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

/* Identifies an audio source (the plugin uses the source's address, kept
 * unique by the reference its meter holds); 0 is no source. */
using MeterSourceId = uintptr_t;

/* At most this many distinct sources are metered by one multiview window
 * (mixer columns, preview and scene tiles together): one obs_volmeter each. */
constexpr size_t kMaxMeterSources = 128;
/* Scene slots that can have tile meters (the largest layout has 32). */
constexpr size_t kMaxTileMeterSlots = 64;

struct MeterPlan {
	/* The distinct sources that get a meter, in priority order (first seen). */
	std::vector<MeterSourceId> sources;
	/* Per input group, in the group's order and without repeats: the indices
	 * into `sources` of its sources that got a meter. */
	std::vector<std::vector<size_t>> groups;
	/* Distinct sources left out by the cap. */
	size_t dropped = 0;

	bool operator==(const MeterPlan &o) const
	{
		return sources == o.sources && groups == o.groups && dropped == o.dropped;
	}
};

/* One meter per distinct source, whatever the number of groups it is in. The
 * groups are in priority order (the plugin passes the mixer columns, then the
 * preview scene, then the scene tiles in slot order): while fewer than `cap`
 * sources are taken, a group's new sources are taken in its own order; once
 * the cap is reached, sources not taken yet are dropped (counted once each),
 * while those already taken still count for every group they are in. Id 0 is
 * ignored. Deterministic: the same input gives the same plan. */
MeterPlan plan_meter_sources(const std::vector<std::vector<MeterSourceId>> &groups, size_t cap = kMaxMeterSources);

/* Below this height in device pixels a tile gets no bars (they would cover
 * too much of a tiny picture and could not be read anyway). */
constexpr float kTileMeterMinPx = 48.f;
/* ...and below this width. */
constexpr float kTileMeterMinWidthPx = 64.f;
/* Narrowest bar, in device pixels. */
constexpr float kTileMeterMinBarPx = 2.f;
/* Narrowest gap between L and R, in device pixels (one pixel can round away). */
constexpr float kTileMeterMinGapPx = 1.5f;

struct TileMeterGeometry {
	bool shown = false;
	RectF column; /* the semi-transparent backing */
	RectF bar[2]; /* L, R: top = 0 dB, bottom = -60 dB */
};

/* Two thin bars on the left edge of a scene tile, in the same units as
 * `inner` (the tile's picture area); `px_per_unit` is the number of device
 * pixels per unit (the display scale). Proportional to the tile height like
 * the Preview meter (each bar 2.2 % of it, margins 4 %), each bar at least
 * kTileMeterMinBarPx device pixels wide and kTileMeterMinGapPx apart. Not shown for a tile smaller than
 * kTileMeterMinPx x kTileMeterMinWidthPx device pixels or degenerate input
 * (non-positive, NaN or infinite). */
TileMeterGeometry tile_meter_geometry(const RectF &inner, float px_per_unit);
