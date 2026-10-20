// SPDX-License-Identifier: GPL-2.0-or-later
#include "status_monitor.hpp"

#include <obs.h>

#include <QTime>

StatusMonitor::StatusMonitor(ObsState &state, QObject *parent) : QObject(parent), m_state(state), m_timer(this)
{
	m_timer.setInterval(1000);
	m_timer.setTimerType(Qt::CoarseTimer);
	connect(&m_timer, &QTimer::timeout, this, [this] { sample(); });

	connect(&m_state, &ObsState::streaming_started, this, [this] {
		m_stream.start(now_ms());
		reset_bitrate();
		m_service_known = false;
		if (m_enabled)
			sample();
	});
	connect(&m_state, &ObsState::streaming_stopped, this, [this] {
		m_stream.stop();
		reset_bitrate();
		if (m_enabled)
			sample();
	});
	connect(&m_state, &ObsState::recording_started, this, [this] {
		m_record.start(now_ms());
		m_record_file.clear(); /* read again below: a new file */
		if (m_enabled)
			sample();
	});
	connect(&m_state, &ObsState::recording_stopped, this, [this] {
		m_record.stop();
		m_record_file.clear();
		if (m_enabled)
			sample();
	});
	connect(&m_state, &ObsState::recording_paused_changed, this, [this](bool paused) {
		if (paused)
			m_record.pause(now_ms());
		else
			m_record.resume(now_ms());
		if (m_enabled)
			sample();
	});
}

int64_t StatusMonitor::now_ms() const
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void StatusMonitor::set_enabled(bool enabled)
{
	if (enabled == m_enabled)
		return;
	m_enabled = enabled;
	if (enabled) {
		reset_bitrate(); /* the previous sample is stale */
		m_have_last = false; /* always emit: the renderer may hold an old one */
		m_timer.start();
		sample();
	} else {
		m_timer.stop();
	}
}

void StatusMonitor::refresh()
{
	m_have_last = false;
	sample();
}

void StatusMonitor::reset_bitrate()
{
	m_have_prev = false;
}

/* Starts a clock for an output that was already live when we first looked
 * (window opened mid-stream), or catches up with a missed event. */
void StatusMonitor::sync_clocks(int64_t now)
{
	const bool streaming = m_state.streaming_active();
	const bool recording = m_state.recording_active();
	const bool paused = m_state.recording_paused();

	/* An output that is already running when first seen (window opened
	 * mid-live): seed the clock from its frame counter / video fps. */
	uint32_t fps_num = 0, fps_den = 0;
	if ((streaming && !m_stream.active()) || (recording && !m_record.active())) {
		obs_video_info ovi;
		if (obs_get_video_info(&ovi)) {
			fps_num = ovi.fps_num;
			fps_den = ovi.fps_den;
		}
	}

	if (streaming && !m_stream.active())
		m_stream.start_at_elapsed(now, seconds_from_frames(m_state.stream_stats().total_frames, fps_num, fps_den));
	else if (!streaming && m_stream.active())
		m_stream.stop();

	if (recording && !m_record.active())
		m_record.start_at_elapsed(now, seconds_from_frames(m_state.record_stats().total_frames, fps_num, fps_den));
	else if (!recording && m_record.active())
		m_record.stop();

	if (recording && paused)
		m_record.pause(now);
	else if (recording && !paused)
		m_record.resume(now);
}

void StatusMonitor::sample()
{
	const int64_t now = now_ms();
	sync_clocks(now);

	RawStatus raw;
	raw.clock_seconds = QTime::currentTime().msecsSinceStartOfDay() / 1000;
	raw.streaming = m_stream.active();
	raw.recording = m_record.active();
	raw.recording_paused = m_record.paused();
	raw.stream_seconds = m_stream.seconds(now);
	raw.record_seconds = m_record.seconds(now);

	if (raw.streaming) {
		const ObsState::StreamStats st = m_state.stream_stats();
		raw.have_stream_output = st.have;
		raw.dropped_frames = st.dropped_frames;
		raw.total_frames = st.total_frames;
		raw.congestion = st.congestion;
		/* Destination: read once per stream (it cannot change while live). */
		if (!m_service_known) {
			const ObsState::ServiceInfo svc = m_state.streaming_service();
			m_service = svc.name;
			m_server_url = svc.server_url;
			m_service_known = true;
		}
		raw.service = m_service;
		raw.server_url = m_server_url;
		if (st.have) {
			if (m_have_prev)
				raw.bitrate_kbps = bitrate_kbps(m_prev_bytes, st.total_bytes,
								double(now - m_prev_ms) / 1000.0);
			m_have_prev = true;
			m_prev_bytes = st.total_bytes;
			m_prev_ms = now;
		} else {
			reset_bitrate(); /* output went away: next one starts fresh */
		}
	} else {
		reset_bitrate();
		m_service_known = false;
	}

	if (raw.recording) {
		/* The file name is read once per recording (no polling of OBS's
		 * lastRecordingPath); the size is the output's byte counter. No
		 * file system access here: a stalled network share cannot block. */
		if (m_record_file.empty())
			m_record_file = m_state.last_recording_path();
		raw.record_path = m_record_file;
		const ObsState::RecordStats rs = m_state.record_stats();
		if (rs.have)
			raw.record_bytes = static_cast<int64_t>(rs.total_bytes);
	}

	const StatusSnapshot snap = make_snapshot(raw);
	if (m_have_last && snap == m_last)
		return;
	m_last = snap;
	m_have_last = true;
	emit snapshot_changed(snap);
}
