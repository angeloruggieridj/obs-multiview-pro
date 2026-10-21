// SPDX-License-Identifier: GPL-2.0-or-later
#include "mixer_feeds.hpp"

#include "i18n.hpp"

#include <plugin-support.h>

#include <array>
#include <utility>

MixerFeeds::~MixerFeeds()
{
	clear();
}

void MixerFeeds::clear()
{
	for (Column &c : m_columns)
		if (c.feed)
			c.feed->detach(); /* UI thread: removes the callback, releases the source */
	if (!m_columns.empty())
		++m_generation;
	m_columns.clear();
	m_dropped_logged = 0;
}

namespace {

MixerSourceId id_of(const obs_source_t *source)
{
	return reinterpret_cast<MixerSourceId>(source);
}

/* The ids of `sources`, skipping null ones and those being deleted. */
std::vector<MixerSourceId> live_ids(const std::vector<obs_source_t *> &sources)
{
	std::vector<MixerSourceId> out;
	out.reserve(sources.size());
	for (obs_source_t *s : sources)
		if (s && !obs_source_removed(s))
			out.push_back(id_of(s));
	return out;
}

} // namespace

unsigned MixerFeeds::refresh(int64_t now_ms, const std::vector<obs_source_t *> &program,
			     const std::vector<obs_source_t *> &preview, bool studio_mode)
{
	unsigned changed = kNone;

	/* One counted reference per occupied channel, all released below. */
	std::array<obs_source_t *, kMixerGlobalChannels> devices = {};
	for (size_t i = 0; i < devices.size(); ++i)
		devices[i] = obs_get_output_source(uint32_t(i + 1));

	MixerSourceLists lists;
	lists.devices = live_ids(std::vector<obs_source_t *>(devices.begin(), devices.end()));
	lists.program = live_ids(program);
	lists.preview = live_ids(preview);
	lists.studio_mode = studio_mode;
	const MixerSelection wanted = select_mixer_columns(lists);
	if (wanted.dropped != m_dropped_logged) {
		m_dropped_logged = wanted.dropped;
		if (wanted.dropped > 0)
			obs_log(LOG_INFO, "mixer tile: %zu audio source(s) not shown (at most %zu columns, PGM included)",
				wanted.dropped, kMixerMaxColumns);
	}

	/* Every wanted id is one of these sources, all alive for this call (the
	 * device references above, the caller's references of the scene lists). */
	auto source_of = [&](MixerSourceId id) -> obs_source_t * {
		for (obs_source_t *s : devices)
			if (id_of(s) == id)
				return s;
		for (obs_source_t *s : program)
			if (id_of(s) == id)
				return s;
		for (obs_source_t *s : preview)
			if (id_of(s) == id)
				return s;
		return nullptr;
	};

	std::vector<MixerSourceId> old_ids;
	old_ids.reserve(m_columns.size());
	for (const Column &c : m_columns)
		old_ids.push_back(c.id);

	std::vector<Column> next;
	next.reserve(wanted.columns.size());
	std::vector<bool> kept(m_columns.size(), false);

	for (MixerSourceId id : wanted.columns) {
		/* The existing column of this source (or PGM), if any. */
		size_t old_idx = m_columns.size();
		for (size_t k = 0; k < m_columns.size(); ++k)
			if (m_columns[k].id == id) {
				old_idx = k;
				break;
			}

		if (id == kMixerPgm) {
			if (old_idx < m_columns.size()) {
				kept[old_idx] = true;
				next.push_back(std::move(m_columns[old_idx]));
			} else {
				Column c;
				c.id = kMixerPgm;
				c.name = T("Tile.MixerProgram").toStdString();
				c.db_state = {true, kMeterFloorDb, now_ms};
				next.push_back(std::move(c));
				changed |= kSetChanged;
			}
			continue;
		}

		obs_source_t *s = source_of(id);
		if (!s)
			continue; /* cannot happen: every id comes from the lists above */
		const char *name_c = obs_source_get_name(s);
		const std::string name = name_c ? name_c : "";
		const bool muted = obs_source_muted(s);

		if (old_idx < m_columns.size() && m_columns[old_idx].feed && m_columns[old_idx].feed->has_source(s)) {
			/* Same source as before: keep the volmeter, refresh the rest. */
			kept[old_idx] = true;
			Column c = std::move(m_columns[old_idx]);
			c.feed->update_source_state();
			if (c.name != name) {
				c.name = name;
				changed |= kTextChanged;
			}
			if (c.muted != muted) {
				c.muted = muted;
				c.db_state = {true, kMeterFloorDb, now_ms};
				c.value = mixer_value(muted, kMeterFloorDb);
				changed |= kTextChanged;
			}
			next.push_back(std::move(c));
			continue;
		}

		/* A new source: a new volmeter, which takes a reference of its own
		 * (attach_sources releases it on failure). */
		obs_source_t *ref = obs_source_get_ref(s);
		if (!ref)
			continue;
		auto feed = std::make_shared<AudioMeter>();
		feed->attach_sources(std::vector<obs_source_t *>{ref});
		if (!feed->attached())
			continue; /* no volmeter: no column */
		Column c;
		c.id = id;
		c.feed = std::move(feed);
		c.name = name;
		c.muted = muted;
		c.db_state = {true, kMeterFloorDb, now_ms};
		c.value = mixer_value(muted, kMeterFloorDb);
		next.push_back(std::move(c));
		changed |= kSetChanged;
	}

	for (obs_source_t *s : devices)
		obs_source_release(s); /* null-safe */

	/* Columns that are gone: detach here (UI thread), then drop. */
	for (size_t k = 0; k < m_columns.size(); ++k)
		if (!kept[k] && m_columns[k].feed) {
			m_columns[k].feed->detach();
			changed |= kSetChanged;
		}

	/* Same columns in another order (a program scene switch, a source moved
	 * in its scene) is a new set too: the graphics thread's per-column state
	 * is indexed by position. */
	if (next.size() != old_ids.size())
		changed |= kSetChanged;
	else
		for (size_t k = 0; k < next.size(); ++k)
			if (next[k].id != old_ids[k])
				changed |= kSetChanged;
	m_columns = std::move(next);
	if (changed & kSetChanged)
		++m_generation;
	return changed;
}

unsigned MixerFeeds::apply_levels(const float *hold_db, size_t count, int64_t now_ms)
{
	unsigned changed = kNone;
	for (size_t i = 0; i < m_columns.size() && i < count; ++i) {
		Column &c = m_columns[i];
		if (c.muted || !db_text_update_due(c.db_state, hold_db[i], now_ms))
			continue;
		c.db_state = {true, hold_db[i], now_ms};
		MixerValue v = mixer_value(false, hold_db[i]);
		if (v.text != c.value.text || v.style != c.value.style) {
			c.value = std::move(v);
			changed |= kTextChanged;
		}
	}
	return changed;
}

std::vector<MixerColumnView> MixerFeeds::view() const
{
	std::vector<MixerColumnView> out;
	out.reserve(m_columns.size());
	for (const Column &c : m_columns)
		out.push_back({c.name, c.value.text, c.value.style});
	return out;
}
