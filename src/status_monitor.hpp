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
 * steady clock, started when the frontend STARTED event arrives (so they match
 * OBS's own timer to within the event latency, but are not read from it). When
 * the window opens while an output is already live, the clock starts at the
 * first sample that sees it: the duration then counts from the window's
 * opening, not from the real start. Stopped events reset them. A paused
 * recording does not count its paused time.
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

	bool m_service_known = false; /* destination read for the current stream */
	std::string m_service;
	std::string m_server_url;

	bool m_have_prev = false;
	uint64_t m_prev_bytes = 0;
	int64_t m_prev_ms = 0;

	StatusSnapshot m_last;
	bool m_have_last = false;
};
