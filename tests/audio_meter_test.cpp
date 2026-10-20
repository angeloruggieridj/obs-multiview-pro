// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "audio_meter_math.hpp"

#include <atomic>
#include <cmath>
#include <limits>

namespace {
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
} // namespace

TEST_CASE("audio_meter_db_to_fraction")
{
	CHECK(db_to_fraction(0.f) == 1.f);
	CHECK(db_to_fraction(-60.f) == 0.f);
	CHECK(db_to_fraction(-90.f) == 0.f);
	CHECK(db_to_fraction(-30.f) == doctest::Approx(0.5f));
	CHECK(db_to_fraction(-20.f) == doctest::Approx(2.f / 3.f));
	CHECK(db_to_fraction(-9.f) == doctest::Approx(0.85f));
	/* clamp */
	CHECK(db_to_fraction(6.f) == 1.f);
	CHECK(db_to_fraction(-kInf) == 0.f);
	CHECK(db_to_fraction(kInf) == 1.f);
	CHECK(db_to_fraction(kNan) == 0.f);
	/* other range */
	CHECK(db_to_fraction(-30.f, -40.f) == doctest::Approx(0.25f));
}

TEST_CASE("audio_meter_sanitize_db")
{
	CHECK(sanitize_db(-12.5f) == -12.5f);
	CHECK(sanitize_db(0.f) == 0.f);
	CHECK(sanitize_db(3.f) == 0.f);
	CHECK(sanitize_db(-kInf) == kMeterFloorDb);
	CHECK(sanitize_db(kInf) == 0.f);
	CHECK(sanitize_db(kNan) == kMeterFloorDb);
	CHECK(sanitize_db(-500.f) == kMeterFloorDb);
}

TEST_CASE("audio_meter_ballistics_attack_is_immediate")
{
	ChannelBallistics b;
	ballistics_step(b, -6.f, 0.016f);
	CHECK(b.peak_db == -6.f);
	CHECK(b.hold_db == -6.f);
	ballistics_step(b, -3.f, 0.016f);
	CHECK(b.peak_db == -3.f);
	CHECK(b.hold_db == -3.f);
}

TEST_CASE("audio_meter_ballistics_decay_rate")
{
	/* Native default 11.76 dB/s: one second of silence from -6 dB gives -17.76. */
	ChannelBallistics b;
	ballistics_step(b, -6.f, 0.016f);
	ballistics_step(b, kMeterFloorDb, 1.f);
	CHECK(b.peak_db == doctest::Approx(-6.f - 11.76f));
	/* Two half seconds decay the same amount. */
	ChannelBallistics c;
	ballistics_step(c, -6.f, 0.016f);
	ballistics_step(c, kMeterFloorDb, 0.5f);
	ballistics_step(c, kMeterFloorDb, 0.5f);
	CHECK(c.peak_db == doctest::Approx(b.peak_db));
	/* Custom rate. */
	ChannelBallistics d;
	ballistics_step(d, -10.f, 0.016f);
	ballistics_step(d, kMeterFloorDb, 0.5f, 20.f);
	CHECK(d.peak_db == doctest::Approx(-20.f));
}

TEST_CASE("audio_meter_ballistics_never_below_input_or_floor")
{
	ChannelBallistics b;
	ballistics_step(b, -6.f, 0.016f);
	/* A level below the decaying peak is followed, never undershot. */
	ballistics_step(b, -7.f, 1.f);
	CHECK(b.peak_db == doctest::Approx(-7.f));
	/* Long silence ends on the floor, not on -inf. */
	for (int i = 0; i < 100; ++i)
		ballistics_step(b, -kInf, 1.f);
	CHECK(b.peak_db == kMeterFloorDb);
	CHECK(std::isfinite(b.hold_db));
}

TEST_CASE("audio_meter_ballistics_peak_hold")
{
	auto silence = [](ChannelBallistics &b, int seconds) {
		for (int i = 0; i < seconds; ++i)
			ballistics_step(b, kMeterFloorDb, 1.f);
	};
	ChannelBallistics b;
	ballistics_step(b, -6.f, 0.016f);
	/* Held for the hold time (native default 20 s)... */
	silence(b, 19);
	CHECK(b.hold_db == -6.f);
	/* ...then it falls back to the current level. */
	silence(b, 2);
	CHECK(b.hold_db == kMeterFloorDb);
	/* A new higher peak restarts the timer. */
	ballistics_step(b, -20.f, 0.016f);
	silence(b, 19);
	CHECK(b.hold_db == -20.f);
	/* A short hold time given explicitly. */
	ChannelBallistics c;
	ballistics_step(c, -6.f, 0.016f);
	ballistics_step(c, kMeterFloorDb, 0.5f, kMeterDecayDbPerSec, 1.f);
	CHECK(c.hold_db == -6.f);
	ballistics_step(c, kMeterFloorDb, 0.6f, kMeterDecayDbPerSec, 1.f);
	CHECK(c.hold_db == kMeterFloorDb);
}

TEST_CASE("audio_meter_ballistics_bad_inputs")
{
	ChannelBallistics b;
	ballistics_step(b, kNan, 0.016f);
	CHECK(b.peak_db == kMeterFloorDb);
	ballistics_step(b, -6.f, kNan); /* NaN dt counts as 0 */
	CHECK(b.peak_db == -6.f);
	ballistics_step(b, kMeterFloorDb, -5.f); /* negative dt: no decay */
	CHECK(b.peak_db == -6.f);
	ballistics_step(b, kMeterFloorDb, 1000.f); /* long stall: dt capped at 1 s */
	CHECK(b.peak_db == doctest::Approx(-6.f - 11.76f));
	ballistics_step(b, kInf, 0.016f);
	CHECK(b.peak_db == 0.f);
}

TEST_CASE("audio_meter_zone_segments")
{
	MeterSegment s[3];
	/* Empty bar. */
	CHECK(meter_segments(0.f, s) == 0);
	/* Only green: -30 dB. */
	CHECK(meter_segments(db_to_fraction(-30.f), s) == 1);
	CHECK(s[0].zone == MeterZone::Nominal);
	CHECK(s[0].f0 == 0.f);
	CHECK(s[0].f1 == doctest::Approx(0.5f));
	/* Into yellow: -15 dB. */
	CHECK(meter_segments(db_to_fraction(-15.f), s) == 2);
	CHECK(s[0].zone == MeterZone::Nominal);
	CHECK(s[0].f1 == doctest::Approx(2.f / 3.f));
	CHECK(s[1].zone == MeterZone::Warning);
	CHECK(s[1].f0 == doctest::Approx(2.f / 3.f));
	CHECK(s[1].f1 == doctest::Approx(0.75f));
	/* Full scale: three zones, contiguous. */
	CHECK(meter_segments(1.f, s) == 3);
	CHECK(s[1].zone == MeterZone::Warning);
	CHECK(s[1].f1 == doctest::Approx(0.85f));
	CHECK(s[2].zone == MeterZone::Error);
	CHECK(s[2].f0 == doctest::Approx(0.85f));
	CHECK(s[2].f1 == 1.f);
	/* Out of range inputs clamp. */
	CHECK(meter_segments(2.f, s) == 3);
	CHECK(s[2].f1 == 1.f);
	CHECK(meter_segments(-1.f, s) == 0);
	CHECK(meter_segments(kNan, s) == 0);
}

TEST_CASE("audio_meter_colors_are_the_native_defaults")
{
	/* OBS 32.1 VolumeMeter constructor defaults (ARGB). */
	CHECK(meter_color(MeterZone::Nominal, true) == 0xFF4CFF4Cu);
	CHECK(meter_color(MeterZone::Warning, true) == 0xFFFFFF4Cu);
	CHECK(meter_color(MeterZone::Error, true) == 0xFFFF4C4Cu);
	CHECK(meter_color(MeterZone::Nominal, false) == 0xFF267F26u);
	CHECK(meter_color(MeterZone::Warning, false) == 0xFF7F7F26u);
	CHECK(meter_color(MeterZone::Error, false) == 0xFF7F2626u);
	/* Peak-hold tick colour follows the level (native getPeakColor). */
	CHECK(peak_hold_color(-55.f) == meter_color(MeterZone::Nominal, false));
	CHECK(peak_hold_color(-30.f) == meter_color(MeterZone::Nominal, true));
	CHECK(peak_hold_color(-15.f) == meter_color(MeterZone::Warning, true));
	CHECK(peak_hold_color(-3.f) == meter_color(MeterZone::Error, true));
	CHECK(peak_hold_color(0.f) == 0xFFFFFFFFu);
}

TEST_CASE("audio_meter_scale_values")
{
	CHECK(kMeterScaleDb.size() == 9);
	CHECK(kMeterScaleDb.front() == 0);
	CHECK(kMeterScaleDb.back() == -60);
	const int expected[] = {0, -5, -10, -15, -20, -30, -40, -50, -60};
	for (size_t i = 0; i < kMeterScaleDb.size(); ++i)
		CHECK(kMeterScaleDb[i] == expected[i]);
}

TEST_CASE("audio_meter_geometry_big")
{
	const RectF tile = {100.f, 50.f, 900.f, 500.f};
	const MeterGeometry g = meter_geometry(tile, true);
	/* Everything inside the tile. */
	auto inside = [&](const RectF &r) {
		return r.x >= tile.x && r.y >= tile.y && r.x + r.w <= tile.x + tile.w + 0.001f &&
		       r.y + r.h <= tile.y + tile.h + 0.001f;
	};
	CHECK(inside(g.column));
	CHECK(inside(g.bar[0]));
	CHECK(inside(g.bar[1]));
	CHECK(inside(g.scale));
	CHECK(inside(g.captions));
	/* On the left edge, tall. */
	CHECK(g.column.x - tile.x < tile.w * 0.1f);
	CHECK(g.bar[0].h > tile.h * 0.7f);
	/* Two equal bars side by side, L left of R, same top. */
	CHECK(g.bar[0].w == doctest::Approx(g.bar[1].w));
	CHECK(g.bar[1].x > g.bar[0].x + g.bar[0].w - 0.001f);
	CHECK(g.bar[0].y == g.bar[1].y);
	CHECK(g.bar[0].w > 2.f);
	/* The scale sits right of the bars; captions are under them. */
	CHECK(g.scale.x >= g.bar[1].x + g.bar[1].w);
	CHECK(g.scale.w > 0.f);
	CHECK(g.captions.y >= g.bar[0].y + g.bar[0].h - 0.001f);
	CHECK(g.captions.h > 0.f);
	/* The column backs all of it. */
	CHECK(g.column.x <= g.bar[0].x);
	CHECK(g.column.x + g.column.w >= g.scale.x + g.scale.w - 0.001f);
	/* dB -> y: 0 dB at the top, -60 dB at the bottom, -30 in the middle. */
	CHECK(meter_y_for_db(g, 0.f) == doctest::Approx(g.bar[0].y));
	CHECK(meter_y_for_db(g, -60.f) == doctest::Approx(g.bar[0].y + g.bar[0].h));
	CHECK(meter_y_for_db(g, -30.f) == doctest::Approx(g.bar[0].y + g.bar[0].h / 2.f));
}

TEST_CASE("audio_meter_geometry_small_has_no_scale")
{
	const RectF tile = {0.f, 0.f, 900.f, 500.f};
	const MeterGeometry g = meter_geometry(tile, false);
	CHECK(g.scale.w == 0.f);
	CHECK(g.captions.h == 0.f);
	CHECK(g.bar[0].h > tile.h * 0.8f);
	/* Narrower than the big meter. */
	const MeterGeometry big = meter_geometry(tile, true);
	CHECK(g.column.w < big.column.w);
	CHECK(g.bar[0].w <= big.bar[0].w);
	CHECK(g.bar[0].w >= 2.f);
}

TEST_CASE("audio_meter_geometry_tiny_tile_stays_sane")
{
	const MeterGeometry g = meter_geometry({0.f, 0.f, 40.f, 30.f}, true);
	CHECK(g.bar[0].w >= 1.f);
	CHECK(g.bar[0].h >= 0.f);
	CHECK(std::isfinite(g.column.w));
	const MeterGeometry z = meter_geometry({0.f, 0.f, 0.f, 0.f}, true);
	CHECK(z.bar[0].h >= 0.f);
	CHECK(z.column.w >= 0.f);
}

TEST_CASE("audio_meter_atomic_max")
{
	std::atomic<float> a{-90.f};
	atomic_store_max(a, -30.f);
	CHECK(a.load() == -30.f);
	atomic_store_max(a, -40.f); /* lower: ignored */
	CHECK(a.load() == -30.f);
	atomic_store_max(a, -10.f);
	CHECK(a.load() == -10.f);
	atomic_store_max(a, -10.f);
	CHECK(a.load() == -10.f);
}

TEST_CASE("audio_meter_frame_step_resets_after_a_long_gap")
{
	/* a state that held -3 dB with a running hold age */
	ChannelBallistics b;
	ballistics_step(b, -3.f, 0.016f);
	ballistics_step(b, kMeterFloorDb, 0.5f);
	REQUIRE(b.hold_db == -3.f);
	REQUIRE(b.hold_age > 0.f);

	/* a normal frame behaves like ballistics_step */
	ChannelBallistics n = b;
	meter_frame_step(n, kMeterFloorDb, 0.016f);
	ChannelBallistics ref = b;
	ballistics_step(ref, kMeterFloorDb, 0.016f);
	CHECK(n.peak_db == ref.peak_db);
	CHECK(n.hold_db == ref.hold_db);
	CHECK(n.hold_age == ref.hold_age);

	/* exactly the cap is still a normal frame */
	ChannelBallistics e = b;
	meter_frame_step(e, kMeterFloorDb, kMeterMaxDt);
	CHECK(e.hold_db == -3.f);

	/* a longer gap: the old state and the peaks of the gap are both gone */
	ChannelBallistics g = b;
	meter_frame_step(g, -1.f, kMeterMaxDt + 0.01f);
	CHECK(g.peak_db == kMeterFloorDb);
	CHECK(g.hold_db == kMeterFloorDb);
	CHECK(g.hold_age == 0.f);
	ChannelBallistics h = b;
	meter_frame_step(h, -1.f, 300.f);
	CHECK(h.peak_db == kMeterFloorDb);
	CHECK(h.hold_db == kMeterFloorDb);
	/* the next normal frame works from the reset state */
	meter_frame_step(h, -20.f, 0.016f);
	CHECK(h.peak_db == -20.f);
	CHECK(h.hold_db == -20.f);
}
