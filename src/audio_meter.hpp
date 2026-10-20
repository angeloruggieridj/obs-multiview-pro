// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "audio_meter_math.hpp"

#include <obs.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

/* Collects the audio sources a scene would show in the OBS 32.1 mixer while it
 * is the studio-mode preview (AudioMixer::updatePreviewSources): the visible
 * items with OBS_SOURCE_AUDIO that are obs_source_audio_active(), followed
 * recursively into groups and nested scenes. One counted reference per source
 * (obs_source_get_ref), each exactly once, in enumeration order. The caller
 * releases them (see AudioMeter::attach_sources, which takes ownership).
 * UI thread. `scene_source` may be null (nothing collected). */
std::vector<obs_source_t *> collect_scene_audio_sources(obs_source_t *scene_source);

/* Feeds two peak levels (L, R) from OBS audio into lock-free atomics.
 *
 * Three ways to feed it (one at a time; each call replaces the previous one):
 *  - attach(source): one obs_volmeter on `source`;
 *  - attach_sources(list): one obs_volmeter per source, the level of a channel
 *    is the maximum over the sources (the preview meter);
 *  - attach_program_mix(): the program mix, from obs_add_raw_audio_callback on
 *    mix 0 (track 1), downmixed to stereo by libobs.
 *
 * Threading. attach*, detach, update_source_state and the destructor run on
 * one thread (the Qt UI thread). The OBS callbacks run on audio / source
 * threads and ONLY write std::atomic<float>: no allocation, no lock of ours, no
 * Qt, no logging. levels() and take_peaks() may be called from any thread
 * (the renderer reads them on the graphics thread).
 *
 * Lifetime. Every callback is removed before the data it points to is freed:
 * obs_volmeter_remove_callback takes the volmeter's callback mutex, which is
 * held while it calls back, and obs_remove_raw_audio_callback takes the audio
 * output's input mutex, which is held around every raw callback; so when they
 * return no callback is running or can start. The destructor calls detach().
 * Sources handed to the volmeters are held with a counted reference until the
 * volmeter is destroyed, so a source being deleted elsewhere cannot dangle.
 *
 * Levels are peak dB (the volmeter's post-fader `peak`; the raw program mix
 * sample peak), sanitized: silence, -inf and NaN read kMeterFloorDb, clipping
 * reads 0. */
class AudioMeter {
public:
	AudioMeter();
	~AudioMeter();
	AudioMeter(const AudioMeter &) = delete;
	AudioMeter &operator=(const AudioMeter &) = delete;

	void attach(obs_source_t *source);
	/* Takes ownership of the counted references in `sources` (as returned by
	 * collect_scene_audio_sources); duplicates and nulls are released. */
	void attach_sources(std::vector<obs_source_t *> sources);
	void attach_program_mix();
	void detach();

	bool attached() const { return m_program || !m_entries.empty(); }
	size_t source_count() const { return m_entries.size(); }
	/* True when the attached sources are exactly `sources` (pointer identity;
	 * the references held here keep those addresses unique). */
	bool has_sources(const std::vector<obs_source_t *> &sources) const;
	/* True when `source` is the only attached source (pointer identity). */
	bool has_source(const obs_source_t *source) const { return m_entries.size() == 1 && m_entries[0]->source == source; }

	/* Re-reads, for each attached source, whether it is mono (then the R bar
	 * mirrors L) and whether it is muted (then it reads silence: the volmeter
	 * itself still reports a muted source's level, like the greyed native
	 * meter). Cheap; call now and then (the renderer does it on its poll). */
	void update_source_state();

	/* Latest callback values, dB per channel [L, R]. */
	std::array<float, 2> levels() const;
	/* Highest value per channel since the previous call, then resets to the
	 * floor. Single consumer (the graphics thread). */
	std::array<float, 2> take_peaks();

private:
	struct Feed {
		AudioMeter *owner = nullptr;
		obs_volmeter_t *volmeter = nullptr;
		obs_source_t *source = nullptr; /* counted reference */
		std::atomic<bool> mono{false};
		std::atomic<bool> muted{false};
	};

	static void volmeter_cb(void *param, const float magnitude[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
				const float input_peak[MAX_AUDIO_CHANNELS]);
	static void raw_audio_cb(void *param, size_t mix_idx, struct audio_data *data);

	void publish(float left_db, float right_db);
	void add_source(obs_source_t *source); /* takes the reference */
	void clear_feeds();

	std::vector<std::unique_ptr<Feed>> m_entries; /* UI thread only */
	bool m_program = false;                       /* UI thread only */

	std::atomic<float> m_latest[2];
	std::atomic<float> m_peak[2];
};
