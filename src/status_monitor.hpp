// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "obs_state.hpp"
#include "status_format.hpp"

#include <QObject>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <string>

/* Samples OBS once per second (UI thread) and emits a StatusSnapshot when its
 * text changed. Enabled only while the layout shows status tiles.
 *
 * Durations: libobs has no output start time, so they are measured here with a
 * steady clock, started when the frontend STARTED event arrives. When the
 * window opens (or the status preset is chosen) while an output is already
 * running, the clock is seeded from the output's frame counter divided by the
 * video fps (obs_output_get_total_frames: elapsed time, without paused time
 * for a recording), so it matches OBS's own timer to about a second. Stopped
 * events reset them. A paused recording does not count its paused time.
 *
 * Recording: size is the recording output's total_bytes (no file system
 * access, nothing that can block the UI thread); the file name is read once
 * per recording (32.1 has no file-changed event, so a split file keeps the
 * first name).
 *
 * Bitrate: delta of the stream output's total_bytes between two samples; the
 * first sample of each stream shows "--".
 *
 * The timer runs only while enabled; events keep the clocks right either way.
 * Must live on the UI thread, destroyed before the ObsState it observes. */
class StatusMonitor : public QObject {
	Q_OBJECT

public:
	StatusMonitor(ObsState &state, QObject *parent = nullptr);

	void set_enabled(bool enabled);
	bool enabled() const { return m_enabled; }

	/* Samples now and emits (unconditionally) the current snapshot. */
	void refresh();
	StatusSnapshot snapshot() const { return m_last; }

signals:
	void snapshot_changed(const StatusSnapshot &snapshot);

private:
	int64_t now_ms() const;
	void sample();
	void sync_clocks(int64_t now);
	void reset_bitrate();

	ObsState &m_state;
	QTimer m_timer; /* child of this, which is a child of the window */
	bool m_enabled = false;

	OutputClock m_stream;
	OutputClock m_record;

	std::string m_record_file; /* name of the current recording, cached */
	bool m_service_known = false; /* destination read for the current stream */
	std::string m_service;
	std::string m_server_url;

	bool m_have_prev = false;
	uint64_t m_prev_bytes = 0;
	int64_t m_prev_ms = 0;

	StatusSnapshot m_last;
	bool m_have_last = false;
};
