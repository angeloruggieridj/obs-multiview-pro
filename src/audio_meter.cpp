// SPDX-License-Identifier: GPL-2.0-or-later
#include "audio_meter.hpp"

#include <obs-audio-controls.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxSceneDepth = 8; /* nested scenes; OBS forbids cycles, this is a belt */

struct Collector {
	std::vector<obs_source_t *> *out;
	int depth;
};

bool enum_item(obs_scene_t *, obs_sceneitem_t *item, void *param);

void collect_from_scene(obs_scene_t *scene, Collector &c)
{
	obs_scene_enum_items(scene, enum_item, &c);
}

bool enum_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	Collector &c = *static_cast<Collector *>(param);
	if (!obs_sceneitem_visible(item))
		return true;

	if (obs_sceneitem_is_group(item)) {
		/* Group children are their own items (the group's scene is not an
		 * obs_scene_t of the scene list). */
		obs_sceneitem_group_enum_items(item, enum_item, &c);
		return true;
	}

	obs_source_t *source = obs_sceneitem_get_source(item); /* not counted */
	if (!source)
		return true;

	if (obs_scene_t *nested = obs_scene_from_source(source)) {
		if (c.depth < kMaxSceneDepth) {
			Collector inner{c.out, c.depth + 1};
			collect_from_scene(nested, inner);
		}
		return true;
	}

	if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0 || !obs_source_audio_active(source))
		return true;
	if (std::find(c.out->begin(), c.out->end(), source) != c.out->end())
		return true;
	if (obs_source_t *ref = obs_source_get_ref(source))
		c.out->push_back(ref);
	return true;
}

/* Highest |sample| of `n` floats (an audio thread helper: no allocation). */
inline float peak_of(const float *p, uint32_t n)
{
	float m = 0.f;
	for (uint32_t i = 0; i < n; ++i) {
		const float a = std::fabs(p[i]);
		if (a > m) /* false for NaN: a NaN sample is ignored */
			m = a;
	}
	return m;
}

inline float amplitude_to_db(float a)
{
	return a > 0.f ? 20.f * std::log10(a) : kMeterFloorDb;
}

} // namespace

std::vector<obs_source_t *> collect_scene_audio_sources(obs_source_t *scene_source)
{
	std::vector<obs_source_t *> out;
	obs_scene_t *scene = scene_source ? obs_scene_from_source(scene_source) : nullptr;
	if (!scene)
		return out;
	Collector c{&out, 0};
	collect_from_scene(scene, c);
	return out;
}

static_assert(std::atomic<float>::is_always_lock_free, "audio callbacks must stay lock-free");

AudioMeter::AudioMeter()
{
	for (int i = 0; i < 2; ++i) {
		m_latest[i].store(kMeterFloorDb, std::memory_order_relaxed);
		m_peak[i].store(kMeterFloorDb, std::memory_order_relaxed);
	}
}

AudioMeter::~AudioMeter()
{
	detach();
}

/* Audio / source thread. Atomics only. */
void AudioMeter::publish(float left_db, float right_db)
{
	m_latest[0].store(left_db, std::memory_order_relaxed);
	m_latest[1].store(right_db, std::memory_order_relaxed);
	atomic_store_max(m_peak[0], left_db);
	atomic_store_max(m_peak[1], right_db);
}

/* Source thread (obs_volmeter). Atomics only. */
void AudioMeter::volmeter_cb(void *param, const float[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
			     const float[MAX_AUDIO_CHANNELS])
{
	const Feed *feed = static_cast<const Feed *>(param);
	if (feed->muted.load(std::memory_order_relaxed)) {
		feed->owner->publish(kMeterFloorDb, kMeterFloorDb);
		return;
	}
	const float l = sanitize_db(peak[0]);
	const float r = feed->mono.load(std::memory_order_relaxed) ? l : sanitize_db(peak[1]);
	feed->owner->publish(l, r);
}

/* Audio thread, once per 1024-frame packet, under libobs's audio input mutex.
 * The conversion requested below makes the data float planar stereo. */
void AudioMeter::raw_audio_cb(void *param, size_t, struct audio_data *data)
{
	AudioMeter *self = static_cast<AudioMeter *>(param);
	if (!data || data->frames == 0 || !data->data[0])
		return;
	const float *left = reinterpret_cast<const float *>(data->data[0]);
	const float l = amplitude_to_db(peak_of(left, data->frames));
	float r = l;
	if (data->data[1])
		r = amplitude_to_db(peak_of(reinterpret_cast<const float *>(data->data[1]), data->frames));
	self->publish(sanitize_db(l), sanitize_db(r));
}

void AudioMeter::add_source(obs_source_t *source)
{
	if (!source)
		return;
	for (const auto &e : m_entries)
		if (e->source == source) {
			obs_source_release(source);
			return;
		}
	obs_volmeter_t *vm = obs_volmeter_create(OBS_FADER_LOG);
	if (!vm) {
		obs_source_release(source);
		return;
	}
	auto feed = std::make_unique<Feed>();
	feed->owner = this;
	feed->volmeter = vm;
	feed->source = source;
	obs_volmeter_add_callback(vm, &AudioMeter::volmeter_cb, feed.get());
	if (!obs_volmeter_attach_source(vm, source)) {
		obs_volmeter_remove_callback(vm, &AudioMeter::volmeter_cb, feed.get());
		obs_volmeter_destroy(vm);
		obs_source_release(source);
		return;
	}
	feed->mono.store(obs_volmeter_get_nr_channels(vm) == 1, std::memory_order_relaxed);
	feed->muted.store(obs_source_muted(source), std::memory_order_relaxed);
	m_entries.push_back(std::move(feed));
}

void AudioMeter::clear_feeds()
{
	for (auto &e : m_entries) {
		/* Returns only when no callback of this volmeter is running, and none
		 * can start afterwards; destroy then detaches from the source. */
		obs_volmeter_remove_callback(e->volmeter, &AudioMeter::volmeter_cb, e.get());
		obs_volmeter_destroy(e->volmeter);
		obs_source_release(e->source);
	}
	m_entries.clear();
}

void AudioMeter::detach()
{
	if (m_program) {
		/* Waits for a running callback (libobs input mutex). */
		obs_remove_raw_audio_callback(0, &AudioMeter::raw_audio_cb, this);
		m_program = false;
	}
	clear_feeds();
}

void AudioMeter::attach(obs_source_t *source)
{
	detach();
	if (source)
		add_source(obs_source_get_ref(source));
}

void AudioMeter::attach_sources(std::vector<obs_source_t *> sources)
{
	detach();
	for (obs_source_t *s : sources)
		add_source(s);
}

void AudioMeter::attach_program_mix()
{
	detach();
	/* Float planar stereo, unclamped (levels above 0 dB are visible as
	 * clipping), at the output sample rate. The same callback / param pair is
	 * only ever registered once (detach() above). */
	audio_convert_info conv = {};
	conv.samples_per_sec = 0; /* output rate */
	conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
	conv.speakers = SPEAKERS_STEREO;
	conv.allow_clipping = true;
	obs_add_raw_audio_callback(0, &conv, &AudioMeter::raw_audio_cb, this);
	m_program = true;
}

bool AudioMeter::has_sources(const std::vector<obs_source_t *> &sources) const
{
	if (sources.size() != m_entries.size())
		return false;
	for (const obs_source_t *s : sources) {
		bool found = false;
		for (const auto &e : m_entries)
			if (e->source == s) {
				found = true;
				break;
			}
		if (!found)
			return false;
	}
	return true;
}

void AudioMeter::update_source_state()
{
	for (auto &e : m_entries) {
		e->mono.store(obs_volmeter_get_nr_channels(e->volmeter) == 1, std::memory_order_relaxed);
		e->muted.store(obs_source_muted(e->source), std::memory_order_relaxed);
	}
}

std::array<float, 2> AudioMeter::levels() const
{
	return {m_latest[0].load(std::memory_order_relaxed), m_latest[1].load(std::memory_order_relaxed)};
}

std::array<float, 2> AudioMeter::take_peaks()
{
	return {m_peak[0].exchange(kMeterFloorDb, std::memory_order_relaxed),
		m_peak[1].exchange(kMeterFloorDb, std::memory_order_relaxed)};
}
