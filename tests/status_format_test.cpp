// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "status_format.hpp"

#include <cmath>
#include <limits>

TEST_CASE("status_format_duration")
{
	CHECK(format_duration(0) == "00:00:00");
	CHECK(format_duration(59) == "00:00:59");
	CHECK(format_duration(3661) == "01:01:01");
	CHECK(format_duration(86399) == "23:59:59");
	CHECK(format_duration(86400) == "24:00:00");
	CHECK(format_duration(90061) == "25:01:01");
	CHECK(format_duration(-1) == "00:00:00");
	CHECK(format_duration(INT64_MIN) == "00:00:00");
}

TEST_CASE("status_format_bitrate_mbps")
{
	CHECK(format_bitrate_mbps(4620.0) == "4.62");
	CHECK(format_bitrate_mbps(4500.4) == "4.50");
	CHECK(format_bitrate_mbps(0) == "0.00");
	CHECK(format_bitrate_mbps(12345.0) == "12.35");
	CHECK(format_bitrate_mbps(-1) == "--");
	CHECK(format_bitrate_mbps(std::numeric_limits<double>::quiet_NaN()) == "--");
	CHECK(format_bitrate_mbps(std::numeric_limits<double>::infinity()) == "--");
}

TEST_CASE("status_format_filesize")
{
	CHECK(format_filesize(0) == "0 B");
	CHECK(format_filesize(1023) == "1023 B");
	CHECK(format_filesize(1024) == "1.0 KB");
	CHECK(format_filesize(1536) == "1.5 KB");
	CHECK(format_filesize(1024 * 1024) == "1.0 MB");
	CHECK(format_filesize(int64_t(5.5 * 1024 * 1024)) == "5.5 MB");
	CHECK(format_filesize(int64_t(1024) * 1024 * 1024) == "1.00 GB");
	CHECK(format_filesize(int64_t(1024) * 1024 * 1024 * 3 / 2) == "1.50 GB");
	CHECK(format_filesize(-1) == "--");
}

TEST_CASE("status_format_congestion")
{
	CHECK(congestion_level(0.0) == Congestion::Ok);
	CHECK(congestion_level(0.2) == Congestion::Ok);
	CHECK(congestion_level(0.4999) == Congestion::Ok);
	CHECK(congestion_level(0.5) == Congestion::High);
	CHECK(congestion_level(1.0) == Congestion::High);
	CHECK(congestion_level(-1.0) == Congestion::Unknown);
	CHECK(congestion_level(std::numeric_limits<double>::quiet_NaN()) == Congestion::Unknown);
}

TEST_CASE("status_friendly_service_name")
{
	/* Service name from the settings wins, trimmed at " - " (protocol suffix). */
	CHECK(friendly_service_name("YouTube - RTMPS", "rtmps://a.rtmps.youtube.com/live2") == "YouTube");
	CHECK(friendly_service_name("Twitch", "rtmp://live.twitch.tv/app") == "Twitch");
	CHECK(friendly_service_name("YouTube - HLS", "") == "YouTube");
	/* Custom server: the host of the URL. */
	CHECK(friendly_service_name("", "rtmp://live.example.com:1935/app") == "live.example.com");
	CHECK(friendly_service_name("", "rtmps://user@host.tld/x/y") == "host.tld");
	CHECK(friendly_service_name("", "srt://10.0.0.5:9000?mode=caller") == "10.0.0.5");
	CHECK(friendly_service_name("", "justahost") == "justahost");
	CHECK(friendly_service_name("", "") == "--");
	CHECK(friendly_service_name("  ", "  ") == "--");
}

TEST_CASE("status_file_name_of")
{
	CHECK(file_name_of("C:\\Videos\\2026-10-20 10-00-00.mkv") == "2026-10-20 10-00-00.mkv");
	CHECK(file_name_of("/home/u/rec.mp4") == "rec.mp4");
	CHECK(file_name_of("rec.mkv") == "rec.mkv");
	CHECK(file_name_of("/dir/") == "--");
	CHECK(file_name_of("") == "--");
}

TEST_CASE("status_format_dropped")
{
	CHECK(format_dropped(12, 4000) == "12 (0.3%)");
	CHECK(format_dropped(0, 4000) == "0 (0.0%)");
	CHECK(format_dropped(0, 0) == "0 (0.0%)");
	CHECK(format_dropped(5, 0) == "5 (0.0%)");
	CHECK(format_dropped(50, 100) == "50 (50.0%)");
	CHECK(format_dropped(-1, 100) == "--");
	CHECK(format_dropped(1, -5) == "--");
}

TEST_CASE("status_snapshot_inactive_shows_dashes")
{
	RawStatus raw;
	raw.clock_seconds = 3661;
	const StatusSnapshot s = make_snapshot(raw);
	CHECK(s.clock == "01:01:01");
	CHECK(s.stream_duration == "--");
	CHECK(s.record_duration == "--");
	CHECK(s.bitrate == "--");
	CHECK(s.dropped_frames == "--");
	CHECK_FALSE(s.streaming);
	CHECK_FALSE(s.recording);
	CHECK_FALSE(s.recording_paused);
}

TEST_CASE("status_snapshot_ignores_stale_values_when_inactive")
{
	/* Leftover numbers must not leak into an inactive output's fields. */
	RawStatus raw;
	raw.stream_seconds = 99;
	raw.record_seconds = 99;
	raw.bitrate_kbps = 4000;
	raw.have_stream_output = true;
	raw.dropped_frames = 7;
	raw.total_frames = 100;
	const StatusSnapshot s = make_snapshot(raw);
	CHECK(s.stream_duration == "--");
	CHECK(s.record_duration == "--");
	CHECK(s.bitrate == "--");
	CHECK(s.dropped_frames == "--");
}

TEST_CASE("status_snapshot_streaming")
{
	RawStatus raw;
	raw.clock_seconds = 86400 + 5; /* wraps to the next day's seconds */
	raw.streaming = true;
	raw.stream_seconds = 3725;
	raw.bitrate_kbps = 4500.4;
	raw.have_stream_output = true;
	raw.dropped_frames = 12;
	raw.total_frames = 4000;
	const StatusSnapshot s = make_snapshot(raw);
	CHECK(s.clock == "00:00:05");
	CHECK(s.streaming);
	CHECK(s.stream_duration == "01:02:05");
	CHECK(s.bitrate == "4.50");
	CHECK(s.dropped_frames == "12 (0.3%)");
	CHECK(s.record_duration == "--");
	CHECK_FALSE(s.recording);
}

TEST_CASE("status_snapshot_streaming_first_sample_and_missing_output")
{
	RawStatus raw;
	raw.streaming = true;
	raw.stream_seconds = 1;
	raw.bitrate_kbps = -1; /* first sample */
	raw.have_stream_output = false;
	const StatusSnapshot s = make_snapshot(raw);
	CHECK(s.stream_duration == "00:00:01");
	CHECK(s.bitrate == "--");
	CHECK(s.dropped_frames == "--");
	CHECK(s.streaming);
}

TEST_CASE("status_snapshot_recording_has_no_bitrate")
{
	/* Spec 8bis: bitrate and dropped frames belong to the stream output. */
	RawStatus raw;
	raw.recording = true;
	raw.record_seconds = 61;
	raw.bitrate_kbps = 9000; /* would be a stale stream value */
	const StatusSnapshot s = make_snapshot(raw);
	CHECK(s.recording);
	CHECK(s.record_duration == "00:01:01");
	CHECK(s.bitrate == "--");
	CHECK(s.dropped_frames == "--");
	CHECK_FALSE(s.streaming);
}

TEST_CASE("status_snapshot_recording_paused")
{
	RawStatus raw;
	raw.recording = true;
	raw.recording_paused = true;
	raw.record_seconds = 61;
	StatusSnapshot s = make_snapshot(raw);
	CHECK(s.recording);
	CHECK(s.recording_paused);
	/* The duration stays (frozen by the clock); the tile shows the paused word. */
	CHECK(s.record_duration == "00:01:01");

	raw.recording = false; /* paused without recording is meaningless */
	s = make_snapshot(raw);
	CHECK_FALSE(s.recording_paused);
	CHECK(s.record_duration == "--");
}

TEST_CASE("status_bitrate_from_byte_counters")
{
	CHECK(bitrate_kbps(0, 562500, 1.0) == doctest::Approx(4500.0));
	CHECK(bitrate_kbps(1000, 1000, 1.0) == doctest::Approx(0.0));
	CHECK(bitrate_kbps(0, 1125000, 2.0) == doctest::Approx(4500.0));
	CHECK(bitrate_kbps(2000, 1000, 1.0) < 0.0); /* counter reset */
	CHECK(bitrate_kbps(0, 1000, 0.0) < 0.0);    /* no time elapsed */
	CHECK(bitrate_kbps(0, 1000, -1.0) < 0.0);
}

TEST_CASE("status_output_clock")
{
	OutputClock c;
	CHECK_FALSE(c.active());
	CHECK(c.seconds(5000) == 0);

	c.start(1000);
	CHECK(c.active());
	CHECK(c.seconds(1000) == 0);
	CHECK(c.seconds(1999) == 0);
	CHECK(c.seconds(4000) == 3);
	CHECK(c.seconds(500) == 0); /* never negative */

	c.pause(4000);
	CHECK(c.paused());
	CHECK(c.seconds(9000) == 3); /* frozen while paused */
	c.resume(9000);
	CHECK_FALSE(c.paused());
	CHECK(c.seconds(11000) == 5); /* 10 s since start minus 5 s paused */

	c.stop();
	CHECK_FALSE(c.active());
	CHECK(c.seconds(20000) == 0);

	c.start(30000); /* restart forgets the old run */
	CHECK(c.seconds(31000) == 1);
	c.resume(31500); /* resume without pause is a no-op */
	CHECK(c.seconds(32000) == 2);
	c.pause(32000);
	c.pause(33000); /* second pause keeps the first timestamp */
	c.resume(34000);
	CHECK(c.seconds(35000) == 3);
}

TEST_CASE("status_snapshot_inactive_new_fields_are_dashes")
{
	RawStatus raw;
	raw.service = "YouTube - RTMPS"; /* stale */
	raw.record_path = "C:/x/rec.mkv";
	raw.record_bytes = 5000;
	raw.congestion = 0.1;
	const StatusSnapshot s = make_snapshot(raw);
	CHECK(s.service == "--");
	CHECK(s.cache == Congestion::Unknown);
	CHECK(s.record_file == "--");
	CHECK(s.record_size == "--");
}

TEST_CASE("status_snapshot_streaming_service_and_cache")
{
	RawStatus raw;
	raw.streaming = true;
	raw.have_stream_output = true;
	raw.bitrate_kbps = 4620.0;
	raw.congestion = 0.0;
	raw.service = "YouTube - RTMPS";
	const StatusSnapshot s = make_snapshot(raw);
	CHECK(s.bitrate == "4.62");
	CHECK(s.cache == Congestion::Ok);
	CHECK(s.service == "YouTube");

	raw.congestion = 0.9;
	CHECK(make_snapshot(raw).cache == Congestion::High);
	raw.have_stream_output = false; /* no output object at this instant */
	CHECK(make_snapshot(raw).cache == Congestion::Unknown);
}

TEST_CASE("status_snapshot_recording_file_and_size")
{
	RawStatus raw;
	raw.recording = true;
	raw.record_seconds = 2047;
	raw.record_path = "C:\\Rec\\take1.mkv";
	raw.record_bytes = 3 * 1024 * 1024;
	StatusSnapshot s = make_snapshot(raw);
	CHECK(s.record_duration == "00:34:07");
	CHECK(s.record_file == "take1.mkv");
	CHECK(s.record_size == "3.0 MB");

	raw.record_bytes = -1; /* file not readable yet */
	raw.record_path.clear();
	s = make_snapshot(raw);
	CHECK(s.record_file == "--");
	CHECK(s.record_size == "--");
}

TEST_CASE("status_tile_content_equal_looks_only_at_the_fields_a_tile_shows")
{
	RawStatus raw;
	raw.streaming = true;
	raw.recording = true;
	raw.have_stream_output = true;
	raw.bitrate_kbps = 4000;
	raw.record_bytes = 100;
	raw.record_path = "/x/a.mkv";
	const StatusSnapshot a = make_snapshot(raw);

	RawStatus later = raw;
	later.clock_seconds += 1;
	const StatusSnapshot clock_moved = make_snapshot(later);
	CHECK_FALSE(status_tile_content_equal(TileKind::Clock, a, clock_moved));
	CHECK(status_tile_content_equal(TileKind::OnAir, a, clock_moved));
	CHECK(status_tile_content_equal(TileKind::Rec, a, clock_moved));
	CHECK(status_tile_content_equal(TileKind::Mixer, a, clock_moved));

	later = raw;
	later.bitrate_kbps = 5000;
	const StatusSnapshot rate = make_snapshot(later);
	CHECK_FALSE(status_tile_content_equal(TileKind::OnAir, a, rate));
	CHECK(status_tile_content_equal(TileKind::Rec, a, rate));
	CHECK(status_tile_content_equal(TileKind::Clock, a, rate));

	later = raw;
	later.record_bytes = 200;
	const StatusSnapshot size = make_snapshot(later);
	CHECK_FALSE(status_tile_content_equal(TileKind::Rec, a, size));
	CHECK(status_tile_content_equal(TileKind::OnAir, a, size));

	CHECK(status_tile_content_equal(TileKind::Scene, a, size)); /* not a status tile */
}

TEST_CASE("status_seconds_from_frames")
{
	CHECK(seconds_from_frames(0, 60, 1) == 0);
	CHECK(seconds_from_frames(3600, 60, 1) == 60);
	CHECK(seconds_from_frames(119, 60, 1) == 1); /* floor */
	CHECK(seconds_from_frames(30000, 30000, 1001) == 1001); /* 29.97 fps */
	CHECK(seconds_from_frames(100, 60, 0) == -1);  /* fps_den == 0 */
	CHECK(seconds_from_frames(100, 0, 1) == -1);   /* fps_num == 0 */
	CHECK(seconds_from_frames(-5, 60, 1) == -1);   /* negative frames */
}

TEST_CASE("status_output_clock_seeded_from_elapsed")
{
	OutputClock c;
	c.start_at_elapsed(10000, 90); /* already running for 90 s when first seen */
	CHECK(c.active());
	CHECK(c.seconds(10000) == 90);
	CHECK(c.seconds(13000) == 93);
	c.pause(13000);
	CHECK(c.seconds(20000) == 93);
	c.resume(20000);
	CHECK(c.seconds(21000) == 94);

	OutputClock d;
	d.start_at_elapsed(5000, -3); /* unknown / negative: starts at 0 */
	CHECK(d.seconds(5000) == 0);
	d.start_at_elapsed(5000, 0);
	CHECK(d.seconds(7000) == 2);
}
