// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Pure audio meter maths and geometry (no OBS / Qt dependency).
//
// Look and ballistics are those of the OBS 32.1 frontend VolumeMeter
// (frontend/components/VolumeMeter.cpp, constructor defaults and
// calculateBallisticsForChannel): -60 dB at the bottom, green below -20 dB,
// yellow up to -9 dB, red up to 0 dB, peak decay 11.76 dB/s (the default
// MeterDecayRate, "20 dB / 1.7 s"), peak-hold tick for 20 s.

#include "layout_model.hpp"

#include <array>
#include <atomic>
#include <cstdint>

constexpr float kMeterMinDb = -60.f;       /* bottom of the bar */
constexpr float kMeterFloorDb = -90.f;     /* "silence": what -inf, NaN and tiny values become */
constexpr float kMeterWarningDb = -20.f;   /* native warningLevel: green -> yellow */
constexpr float kMeterErrorDb = -9.f;      /* native errorLevel: yellow -> red */
constexpr float kMeterClipDb = 0.f;        /* native clipLevel: peak-hold tick turns white */
constexpr float kMeterMinInputDb = -50.f;  /* native minimumInputLevel: tick hidden below */
constexpr float kMeterDecayDbPerSec = 11.76f;
constexpr float kMeterPeakHoldSec = 20.f;
constexpr float kMeterMaxDt = 1.f;         /* longest frame gap accounted for (s) */

/* The scale printed next to the big meter. */
constexpr std::array<int, 9> kMeterScaleDb = {0, -5, -10, -15, -20, -30, -40, -50, -60};

/* Position of `db` on the bar: 0 dB -> 1, min_db and below -> 0, linear in dB,
 * clamped to 0..1. NaN -> 0. */
float db_to_fraction(float db, float min_db = kMeterMinDb);

/* A level safe to draw and compare: NaN, -inf and anything below the floor
 * become kMeterFloorDb, anything above 0 dB (clipping) becomes 0. */
float sanitize_db(float db);

/* Raises `target` to `value` if it is larger (lock-free, never allocates).
 * Safe to call from an audio thread. `value` must not be NaN. */
inline void atomic_store_max(std::atomic<float> &target, float value)
{
	float cur = target.load(std::memory_order_relaxed);
	while (value > cur && !target.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {
	}
}

/* Display state of one channel (graphics thread only). */
struct ChannelBallistics {
	float peak_db = kMeterFloorDb; /* bar top: attack immediate, then falls */
	float hold_db = kMeterFloorDb; /* peak-hold tick */
	float hold_age = 0.f;          /* seconds since hold_db was last raised */
};

/* One frame: `input_db` is the highest level seen since the previous frame, dt
 * the time since the previous frame in seconds. As the native meter: the bar
 * rises at once and falls `decay` dB per second (never below the input); the
 * tick rises at once, stays `hold_s` seconds, then falls back to the input.
 * Any NaN / inf input is sanitized; dt is clamped to 0..kMeterMaxDt. */
void ballistics_step(ChannelBallistics &b, float input_db, float dt, float decay = kMeterDecayDbPerSec,
		     float hold_s = kMeterPeakHoldSec);

enum class MeterZone { Nominal, Warning, Error };

/* A run of the bar, in fractions from the bottom (0) to the top (1). */
struct MeterSegment {
	float f0;
	float f1;
	MeterZone zone;
};

/* The coloured runs that make up a bar filled to `fraction` (0..1, clamped;
 * NaN counts as empty): green up to -20 dB, yellow up to -9 dB, red above.
 * Returns how many of out[0..2] are set (0 for an empty bar). */
int meter_segments(float fraction, MeterSegment out[3]);

/* ARGB, native defaults. foreground = bright (the level), else dark (the
 * empty part of the bar). */
uint32_t meter_color(MeterZone zone, bool foreground);

/* Colour of the peak-hold tick (native getPeakColor): dark green below
 * -50 dB, then by zone, white at 0 dB. */
uint32_t peak_hold_color(float db);

/* Where a meter goes inside a tile, in the same units as the tile (canvas
 * pixels). The big meter (Program) has a dB scale on the right of the bars and
 * L / R captions under them; the small one (Preview) only the bars. */
struct MeterGeometry {
	RectF column;   /* whole meter, drawn as a dark backing */
	RectF bar[2];   /* L, R: top = 0 dB, bottom = -60 dB */
	RectF scale;    /* numbers (w = 0: none) */
	RectF captions; /* "L" "R" row under the bars (h = 0: none) */
};

MeterGeometry meter_geometry(const RectF &tile_inner, bool with_scale);

/* y of `db` on the bars of `g` (0 dB at the top, kMeterMinDb at the bottom). */
float meter_y_for_db(const MeterGeometry &g, float db);
