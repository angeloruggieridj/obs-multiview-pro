// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Status tile text and bookkeeping (no OBS / Qt dependency).

#include "layout_model.hpp"

#include <cstdint>
#include <string>

/* "HH:MM:SS"; hours are not wrapped (90061 -> "25:01:01"); negative -> "00:00:00". */
std::string format_duration(int64_t seconds);

/* Megabits per second with two decimals, from kbps: 4620.0 -> "4.62".
 * Negative, NaN or infinite (unknown) -> "--". */
std::string format_bitrate_mbps(double kbps);

/* "12 (0.3%)": dropped frames and their share of total_frames, one decimal
 * (total 0 -> "0 (0.0%)"). Negative counts -> "--". */
std::string format_dropped(int64_t dropped, int64_t total_frames);

/* Binary units: "512 B", "1.5 KB", "5.5 MB", "1.50 GB". Negative -> "--". */
std::string format_filesize(int64_t bytes);

/* libobs congestion 0..1 as the "CACHE" word of the ATEM tile: "OK" below 0.5,
 * "HIGH" from 0.5 up; negative / NaN (unknown) -> "--". */
std::string format_congestion(double congestion);

/* Friendly stream destination: the OBS service name from the settings trimmed
 * at " - " ("YouTube - RTMPS" -> "YouTube"); if empty, the host of the server
 * URL ("rtmp://live.example.com:1935/app" -> "live.example.com"); else "--".
 * Never looks at a stream key. */
std::string friendly_service_name(const std::string &service, const std::string &server_url);

/* Last path component (either separator); empty / directory -> "--". */
std::string file_name_of(const std::string &path);

/* The text shown by the status tiles. Inactive outputs show "--". */
struct StatusSnapshot {
	std::string clock; /* local time, "HH:MM:SS" */
	bool streaming = false;
	std::string stream_duration; /* "HH:MM:SS" or "--" */
	std::string bitrate;         /* Mb/s, "4.62" or "--" */
	std::string dropped_frames;  /* "12 (0.3%)" or "--" */
	std::string cache;           /* "OK", "HIGH" or "--" */
	std::string service;         /* "YouTube" or "--" */
	bool recording = false;
	bool recording_paused = false; /* implies recording */
	std::string record_duration;   /* "HH:MM:SS", "PAUSED" or "--" */
	std::string record_file;       /* file name or "--" */
	std::string record_size;       /* "1.5 GB" or "--" */

	bool operator==(const StatusSnapshot &o) const
	{
		return clock == o.clock && streaming == o.streaming && stream_duration == o.stream_duration &&
		       bitrate == o.bitrate && dropped_frames == o.dropped_frames && cache == o.cache &&
		       service == o.service && recording == o.recording && recording_paused == o.recording_paused &&
		       record_duration == o.record_duration && record_file == o.record_file &&
		       record_size == o.record_size;
	}
	bool operator!=(const StatusSnapshot &o) const { return !(*this == o); }
};

/* True when the text of tile `kind` is identical in both snapshots, i.e. the
 * tile does not need to be drawn again. Pure. */
bool status_tile_content_equal(TileKind kind, const StatusSnapshot &a, const StatusSnapshot &b);

/* What one sample of OBS reports. */
struct RawStatus {
	int64_t clock_seconds = 0; /* seconds since local midnight */
	bool streaming = false;
	bool recording = false;
	bool recording_paused = false;
	int64_t stream_seconds = 0;
	int64_t record_seconds = 0;
	double bitrate_kbps = -1.0; /* < 0 or NaN: unknown (e.g. first sample) */
	bool have_stream_output = false;
	int64_t dropped_frames = 0;
	int64_t total_frames = 0;
	double congestion = -1.0;  /* < 0: unknown */
	std::string service;       /* service name from the settings, may be empty */
	std::string server_url;    /* fallback for friendly_service_name */
	std::string record_path;   /* current recording file, may be empty */
	int64_t record_bytes = -1; /* < 0: unknown */
};

StatusSnapshot make_snapshot(const RawStatus &raw);

/* kbps from two byte counters read `seconds` apart; -1 when it cannot be
 * computed (counter went backwards, no time elapsed). */
double bitrate_kbps(uint64_t previous_bytes, uint64_t current_bytes, double seconds);

/* Elapsed time of an output, from timestamps the caller supplies (ms, any
 * monotonic origin). Time spent paused is not counted. */
class OutputClock {
public:
	bool active() const { return m_active; }
	bool paused() const { return m_paused; }
	void start(int64_t now_ms);
	void stop();
	void pause(int64_t now_ms);
	void resume(int64_t now_ms);
	int64_t seconds(int64_t now_ms) const;

private:
	bool m_active = false;
	bool m_paused = false;
	int64_t m_start = 0;
	int64_t m_paused_total = 0;
	int64_t m_pause_start = 0;
};
