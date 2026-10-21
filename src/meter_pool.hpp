// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "audio_meter_math.hpp"

#include <obs.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

/* The levels of one audio source: one obs_volmeter on it (post-fader peak, as
 * the OBS mixer shows it), written into atomics by the volmeter callback.
 *
 * Threading. Only MeterPool (UI thread) attaches and detaches it. The
 * callback runs on an OBS audio / source thread and ONLY writes atomics: no
 * allocation, no lock of ours, no Qt, no logging. levels() and take_peaks()
 * may be called from any thread (the renderer reads them on the graphics
 * thread). A detached meter keeps its last values and is never fed again, so
 * the graphics thread may still read it from an older snapshot.
 *
 * Levels are sanitized like AudioMeter's: silence, -inf and NaN read
 * kMeterFloorDb, clipping reads 0. A muted source reads silence (the volmeter
 * itself still reports a muted source's level, like the greyed native meter);
 * a mono source mirrors L into R. */
class SourceMeter {
public:
	SourceMeter();
	SourceMeter(const SourceMeter &) = delete;
	SourceMeter &operator=(const SourceMeter &) = delete;

	/* Latest callback values, dB per channel [L, R]. */
	std::array<float, 2> levels() const;
	/* Highest value per channel since the previous call, then resets to the
	 * floor. Single consumer (the graphics thread of the renderer that owns
	 * the pool). */
	std::array<float, 2> take_peaks();
	bool muted() const { return m_muted.load(std::memory_order_relaxed); }

private:
	friend class MeterPool;

	static void volmeter_cb(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
				const float peak[MAX_AUDIO_CHANNELS], const float input_peak[MAX_AUDIO_CHANNELS]);

	obs_volmeter_t *m_volmeter = nullptr; /* UI thread (MeterPool) */
	obs_source_t *m_source = nullptr;     /* counted reference, UI thread (MeterPool) */
	std::atomic<bool> m_mono{false};
	std::atomic<bool> m_muted{false};
	std::atomic<float> m_latest[2];
	std::atomic<float> m_peak[2];
};

/* One SourceMeter per distinct audio source, shared by everything a renderer
 * meters: the mixer tile's columns, the Preview meter and the scene tiles. A
 * source shown in several places (in several scenes, in a scene and in the
 * mixer...) has exactly one volmeter.
 *
 * sync() is the register / unregister step: the renderer passes the complete
 * list of sources its consumers want (at most kMaxMeterSources, see
 * plan_meter_sources); a source new to the pool gets a volmeter and a counted
 * reference of its own, a source no consumer wants any more loses both at that
 * call. Pointer identity is safe: every source in the pool is held by a
 * counted reference, so its address cannot be reused while it is here.
 *
 * Lifetime. obs_volmeter_remove_callback takes the volmeter's callback mutex,
 * which is held while it calls back: when it returns no callback is running
 * or can start, then the volmeter is destroyed (detaching it from the source)
 * and the reference released. The SourceMeter memory itself is shared
 * (shared_ptr) with the snapshots the graphics thread draws from; it only
 * holds atomics once detached. clear() and the destructor detach everything.
 *
 * UI thread only. */
class MeterPool {
public:
	MeterPool() = default;
	~MeterPool();
	MeterPool(const MeterPool &) = delete;
	MeterPool &operator=(const MeterPool &) = delete;

	/* `wanted`: distinct sources, alive for the call (the caller holds
	 * references). Returns, aligned with `wanted`, the meter of each (null
	 * when no volmeter could be made; retried at the next call). Sources in
	 * the pool that are not in `wanted` are detached and dropped. */
	std::vector<std::shared_ptr<SourceMeter>> sync(const std::vector<obs_source_t *> &wanted);
	/* Re-reads whether each source is mono and whether it is muted. */
	void update_state();
	/* Detaches every meter. Idempotent. */
	void clear();

	size_t size() const { return m_entries.size(); }

private:
	static std::shared_ptr<SourceMeter> attach(obs_source_t *source);
	static void detach(SourceMeter &meter);

	std::vector<std::shared_ptr<SourceMeter>> m_entries;
};
