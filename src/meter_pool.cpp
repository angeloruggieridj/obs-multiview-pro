// SPDX-License-Identifier: GPL-2.0-or-later
#include "meter_pool.hpp"

#include <obs-audio-controls.h>

#include <utility>

static_assert(std::atomic<float>::is_always_lock_free, "audio callbacks must stay lock-free");
static_assert(std::atomic<bool>::is_always_lock_free, "audio callbacks must stay lock-free");

SourceMeter::SourceMeter()
{
	for (int i = 0; i < 2; ++i) {
		m_latest[i].store(kMeterFloorDb, std::memory_order_relaxed);
		m_peak[i].store(kMeterFloorDb, std::memory_order_relaxed);
	}
}

/* Source thread (obs_volmeter). Atomics only. */
void SourceMeter::volmeter_cb(void *param, const float[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
			      const float[MAX_AUDIO_CHANNELS])
{
	SourceMeter *self = static_cast<SourceMeter *>(param);
	float l = kMeterFloorDb;
	float r = kMeterFloorDb;
	if (!self->m_muted.load(std::memory_order_relaxed)) {
		l = sanitize_db(peak[0]);
		r = self->m_mono.load(std::memory_order_relaxed) ? l : sanitize_db(peak[1]);
	}
	self->m_latest[0].store(l, std::memory_order_relaxed);
	self->m_latest[1].store(r, std::memory_order_relaxed);
	atomic_store_max(self->m_peak[0], l);
	atomic_store_max(self->m_peak[1], r);
}

std::array<float, 2> SourceMeter::levels() const
{
	return {m_latest[0].load(std::memory_order_relaxed), m_latest[1].load(std::memory_order_relaxed)};
}

std::array<float, 2> SourceMeter::take_peaks()
{
	return {m_peak[0].exchange(kMeterFloorDb, std::memory_order_relaxed),
		m_peak[1].exchange(kMeterFloorDb, std::memory_order_relaxed)};
}

MeterPool::~MeterPool()
{
	clear();
}

std::shared_ptr<SourceMeter> MeterPool::attach(obs_source_t *source)
{
	obs_source_t *ref = source ? obs_source_get_ref(source) : nullptr;
	if (!ref)
		return nullptr;
	obs_volmeter_t *vm = obs_volmeter_create(OBS_FADER_LOG);
	if (!vm) {
		obs_source_release(ref);
		return nullptr;
	}
	auto meter = std::make_shared<SourceMeter>();
	meter->m_volmeter = vm;
	meter->m_source = ref;
	/* Mute state first, so the first callback already honours it. */
	meter->m_muted.store(obs_source_muted(ref), std::memory_order_relaxed);
	obs_volmeter_add_callback(vm, &SourceMeter::volmeter_cb, meter.get());
	if (!obs_volmeter_attach_source(vm, ref)) {
		detach(*meter);
		return nullptr;
	}
	meter->m_mono.store(obs_volmeter_get_nr_channels(vm) == 1, std::memory_order_relaxed);
	return meter;
}

void MeterPool::detach(SourceMeter &m)
{
	if (m.m_volmeter) {
		/* Returns only when no callback of this volmeter is running, and none
		 * can start afterwards; destroy then detaches from the source. */
		obs_volmeter_remove_callback(m.m_volmeter, &SourceMeter::volmeter_cb, &m);
		obs_volmeter_destroy(m.m_volmeter);
		m.m_volmeter = nullptr;
	}
	obs_source_release(m.m_source); /* null-safe */
	m.m_source = nullptr;
}

std::vector<std::shared_ptr<SourceMeter>> MeterPool::sync(const std::vector<obs_source_t *> &wanted)
{
	std::vector<std::shared_ptr<SourceMeter>> out(wanted.size());
	std::vector<bool> kept(m_entries.size(), false);
	for (size_t i = 0; i < wanted.size(); ++i) {
		for (size_t k = 0; k < m_entries.size(); ++k)
			if (!kept[k] && m_entries[k]->m_source == wanted[i]) {
				kept[k] = true;
				out[i] = m_entries[k];
				break;
			}
	}

	/* Gone first (fewer volmeters at any time), then the new ones. */
	std::vector<std::shared_ptr<SourceMeter>> next;
	next.reserve(wanted.size());
	for (size_t k = 0; k < m_entries.size(); ++k) {
		if (kept[k])
			next.push_back(std::move(m_entries[k]));
		else
			detach(*m_entries[k]);
	}
	for (size_t i = 0; i < wanted.size(); ++i) {
		if (out[i] || !wanted[i])
			continue;
		out[i] = attach(wanted[i]);
		if (out[i])
			next.push_back(out[i]);
	}
	m_entries = std::move(next);
	return out;
}

void MeterPool::update_state()
{
	for (const auto &m : m_entries) {
		m->m_mono.store(obs_volmeter_get_nr_channels(m->m_volmeter) == 1, std::memory_order_relaxed);
		m->m_muted.store(obs_source_muted(m->m_source), std::memory_order_relaxed);
	}
}

void MeterPool::clear()
{
	for (const auto &m : m_entries)
		detach(*m);
	m_entries.clear();
}
