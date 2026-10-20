// SPDX-License-Identifier: GPL-2.0-or-later
#include "status_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr const char *kNone = "--";
constexpr int64_t kSecondsPerDay = 86400;

std::string trim(const std::string &s)
{
	const char *ws = " \t\r\n";
	const size_t a = s.find_first_not_of(ws);
	if (a == std::string::npos)
		return std::string();
	return s.substr(a, s.find_last_not_of(ws) - a + 1);
}
} // namespace

std::string format_duration(int64_t seconds)
{
	if (seconds < 0)
		seconds = 0;
	const int64_t h = seconds / 3600;
	const int m = int(seconds / 60 % 60);
	const int s = int(seconds % 60);
	char buf[40];
	std::snprintf(buf, sizeof(buf), "%02lld:%02d:%02d", static_cast<long long>(h), m, s);
	return buf;
}

std::string format_bitrate_mbps(double kbps)
{
	if (!std::isfinite(kbps) || kbps < 0.0)
		return kNone;
	char buf[40];
	std::snprintf(buf, sizeof(buf), "%.2f", kbps / 1000.0);
	return buf;
}

std::string format_dropped(int64_t dropped, int64_t total_frames)
{
	if (dropped < 0 || total_frames < 0)
		return kNone;
	const double pct = total_frames > 0 ? 100.0 * double(dropped) / double(total_frames) : 0.0;
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%lld (%.1f%%)", static_cast<long long>(dropped), pct);
	return buf;
}

std::string format_filesize(int64_t bytes)
{
	if (bytes < 0)
		return kNone;
	char buf[40];
	const double b = static_cast<double>(bytes);
	constexpr double kKB = 1024.0, kMB = kKB * 1024.0, kGB = kMB * 1024.0;
	if (bytes < 1024)
		std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
	else if (b < kMB)
		std::snprintf(buf, sizeof(buf), "%.1f KB", b / kKB);
	else if (b < kGB)
		std::snprintf(buf, sizeof(buf), "%.1f MB", b / kMB);
	else
		std::snprintf(buf, sizeof(buf), "%.2f GB", b / kGB);
	return buf;
}

std::string format_congestion(double congestion)
{
	if (!(congestion >= 0.0))
		return kNone; /* negative or NaN */
	return congestion >= 0.5 ? "HIGH" : "OK";
}

std::string friendly_service_name(const std::string &service, const std::string &server_url)
{
	std::string name = trim(service);
	const size_t dash = name.find(" - ");
	if (dash != std::string::npos)
		name = trim(name.substr(0, dash));
	if (!name.empty())
		return name;

	std::string host = trim(server_url);
	const size_t scheme = host.find("://");
	if (scheme != std::string::npos)
		host.erase(0, scheme + 3);
	host = host.substr(0, host.find_first_of("/?#"));
	const size_t at = host.rfind('@');
	if (at != std::string::npos)
		host.erase(0, at + 1);
	if (!host.empty() && host.front() != '[') { /* IPv6 literals stay whole */
		const size_t colon = host.find(':');
		if (colon != std::string::npos)
			host.erase(colon);
	}
	return host.empty() ? std::string(kNone) : host;
}

std::string file_name_of(const std::string &path)
{
	const size_t cut = path.find_last_of("/\\");
	const std::string name = cut == std::string::npos ? path : path.substr(cut + 1);
	return name.empty() ? std::string(kNone) : name;
}

bool status_tile_content_equal(TileKind kind, const StatusSnapshot &a, const StatusSnapshot &b)
{
	switch (kind) {
	case TileKind::OnAir:
		return a.streaming == b.streaming && a.stream_duration == b.stream_duration && a.bitrate == b.bitrate &&
		       a.dropped_frames == b.dropped_frames && a.cache == b.cache && a.service == b.service;
	case TileKind::Rec:
		return a.recording == b.recording && a.recording_paused == b.recording_paused &&
		       a.record_duration == b.record_duration && a.record_file == b.record_file &&
		       a.record_size == b.record_size;
	case TileKind::Clock:
		return a.clock == b.clock;
	case TileKind::Mixer: /* placeholder until the meters (Task 9b) */
	default:
		return true;
	}
}

StatusSnapshot make_snapshot(const RawStatus &raw)
{
	StatusSnapshot s;
	const int64_t day = raw.clock_seconds % kSecondsPerDay;
	s.clock = format_duration(day < 0 ? day + kSecondsPerDay : day);

	s.streaming = raw.streaming;
	s.recording = raw.recording;
	s.recording_paused = raw.recording && raw.recording_paused;

	s.stream_duration = kNone;
	s.bitrate = kNone;
	s.dropped_frames = kNone;
	s.cache = kNone;
	s.service = kNone;
	s.record_duration = kNone;
	s.record_file = kNone;
	s.record_size = kNone;

	if (raw.streaming) {
		s.stream_duration = format_duration(raw.stream_seconds);
		s.service = friendly_service_name(raw.service, raw.server_url);
		/* Bitrate, dropped frames and congestion are the stream output's. */
		s.bitrate = format_bitrate_mbps(raw.bitrate_kbps);
		if (raw.have_stream_output) {
			s.dropped_frames = format_dropped(raw.dropped_frames, raw.total_frames);
			s.cache = format_congestion(raw.congestion);
		}
	}
	if (raw.recording) {
		s.record_duration = s.recording_paused ? "PAUSED" : format_duration(raw.record_seconds);
		if (!raw.record_path.empty())
			s.record_file = file_name_of(raw.record_path);
		s.record_size = format_filesize(raw.record_bytes);
	}
	return s;
}

double bitrate_kbps(uint64_t previous_bytes, uint64_t current_bytes, double seconds)
{
	if (!(seconds > 0.0) || current_bytes < previous_bytes)
		return -1.0;
	return double(current_bytes - previous_bytes) * 8.0 / 1000.0 / seconds;
}

void OutputClock::start(int64_t now_ms)
{
	m_active = true;
	m_paused = false;
	m_start = now_ms;
	m_paused_total = 0;
	m_pause_start = 0;
}

void OutputClock::stop()
{
	m_active = false;
	m_paused = false;
	m_start = m_paused_total = m_pause_start = 0;
}

void OutputClock::pause(int64_t now_ms)
{
	if (!m_active || m_paused)
		return;
	m_paused = true;
	m_pause_start = now_ms;
}

void OutputClock::resume(int64_t now_ms)
{
	if (!m_active || !m_paused)
		return;
	m_paused = false;
	m_paused_total += std::max<int64_t>(0, now_ms - m_pause_start);
}

int64_t OutputClock::seconds(int64_t now_ms) const
{
	if (!m_active)
		return 0;
	const int64_t until = m_paused ? m_pause_start : now_ms;
	return std::max<int64_t>(0, until - m_start - m_paused_total) / 1000;
}
