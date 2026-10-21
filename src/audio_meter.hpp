// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "audio_meter_math.hpp"

#include <obs.h>

#include <array>
#include <atomic>
#include <vector>

/* Collects the audio sources a scene would show in the OBS 32.1 mixer while it
 * is the studio-mode preview (AudioMixer::updatePreviewSources): the visible
 * items with OBS_SOURCE_AUDIO that are obs_source_audio_active(); unlike the
 * OBS mixer, groups and nested scenes are followed too (recursively). One
 * counted reference per source (obs_source_get_ref), each exactly once, in
 * enumeration order. The caller releases them. UI thread. `scene_source` may
 * be null (nothing collected). */
std::vector<obs_source_t *> collect_scene_audio_sources(obs_source_t *scene_source);

/* The program mix levels (L, R) in lock-free atomics, from
 * obs_add_raw_audio_callback on mix 0 (track 1), in the output's own channel
 * layout (L and R are its first two channels). The levels of single sources
 * come from MeterPool (meter_pool.hpp).
 *
 * Threading. attach_program_mix, detach and the destructor run on the Qt UI
 * thread. The OBS callback runs on the audio thread and ONLY writes
 * std::atomic<float>: no allocation, no lock of ours, no Qt, no logging.
 * levels() and take_peaks() may be called from any thread (the renderer reads
 * them on the graphics thread).
 *
 * Lifetime. obs_remove_raw_audio_callback takes the audio output's input
 * mutex, which is held around every raw callback; so when detach() returns no
 * callback is running or can start. The destructor calls detach().
 *
 * Levels are the raw program mix sample peak in dB, sanitized: silence, -inf
 * and NaN read kMeterFloorDb, clipping reads 0. */
class AudioMeter {
public:
	AudioMeter();
	~AudioMeter();
	AudioMeter(const AudioMeter &) = delete;
	AudioMeter &operator=(const AudioMeter &) = delete;

	void attach_program_mix();
	void detach();
	bool attached() const { return m_program; }

	/* Latest callback values, dB per channel [L, R]. */
	std::array<float, 2> levels() const;
	/* Highest value per channel since the previous call, then resets to the
	 * floor. Single consumer (the graphics thread). */
	std::array<float, 2> take_peaks();

private:
	static void raw_audio_cb(void *param, size_t mix_idx, struct audio_data *data);
	void publish(float left_db, float right_db);

	bool m_program = false; /* UI thread only */

	std::atomic<int> m_channels{2}; /* output channel count, set before the callback is added */
	std::atomic<float> m_latest[2];
	std::atomic<float> m_peak[2];
};
