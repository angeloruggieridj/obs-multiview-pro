// SPDX-License-Identifier: GPL-2.0-or-later
#include "mixer_feeds.hpp"

#include "i18n.hpp"

#include <plugin-support.h>

#include <initializer_list>
#include <utility>

void MixerFeeds::clear()
{
	if (!m_columns.empty())
		++m_generation;
	m_columns.clear();
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

unsigned MixerFeeds::refresh(int64_t now_ms, const std::vector<obs_source_t *> &devices,
			     const std::vector<obs_source_t *> &program, const std::vector<obs_source_t *> &preview,
			     bool studio_mode)
{
	unsigned changed = kNone;

	MixerSourceLists lists;
	lists.devices = live_ids(devices);
	lists.program = live_ids(program);
	lists.preview = live_ids(preview);
	lists.studio_mode = studio_mode;
	lists.previous.reserve(m_columns.size());
	for (const Column &c : m_columns)
		lists.previous.push_back(c.id);
	const MixerSelection wanted = select_mixer_columns(lists);
	if (wanted.dropped > m_dropped_logged) {
		m_dropped_logged = wanted.dropped;
		obs_log(LOG_INFO, "mixer tile: %zu audio source(s) not shown (at most %zu columns, PGM included)",
			wanted.dropped, kMixerMaxColumns);
	}

	/* Every wanted id is one of these sources, all alive for this call. */
	auto source_of = [&](MixerSourceId id) -> obs_source_t * {
		for (const auto *list : {&devices, &program, &preview})
			for (obs_source_t *s : *list)
				if (s && id_of(s) == id)
					return s;
		return nullptr;
	};

	std::vector<Column> next;
	next.reserve(wanted.columns.size());
	for (MixerSourceId id : wanted.columns) {
		size_t old_idx = m_columns.size();
		for (size_t k = 0; k < m_columns.size(); ++k)
			if (m_columns[k].id == id) {
				old_idx = k;
				break;
			}

		if (id == kMixerPgm) {
			if (old_idx < m_columns.size()) {
				next.push_back(std::move(m_columns[old_idx]));
			} else {
				Column c;
				c.id = kMixerPgm;
				c.name = T("Tile.MixerProgram").toStdString();
				c.db_state = {true, kMeterFloorDb, now_ms};
				next.push_back(std::move(c));
			}
			continue;
		}

		obs_source_t *s = source_of(id);
		if (!s)
			continue; /* cannot happen: every id comes from the lists above */
		const char *name_c = obs_source_get_name(s);
		const std::string name = name_c ? name_c : "";
		const bool muted = obs_source_muted(s);

		if (old_idx < m_columns.size()) {
			/* Same source as before (its id is its address, which the
			 * renderer's meter keeps unique): refresh the texts. */
			Column c = std::move(m_columns[old_idx]);
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

		Column c;
		c.id = id;
		c.name = name;
		c.muted = muted;
		c.db_state = {true, kMeterFloorDb, now_ms};
		c.value = mixer_value(muted, kMeterFloorDb);
		next.push_back(std::move(c));
	}

	/* A column appeared, vanished or moved: the graphics thread's per-column
	 * state follows the columns by id (see the renderer). */
	if (next.size() != lists.previous.size())
		changed |= kSetChanged;
	else
		for (size_t k = 0; k < next.size(); ++k)
			if (next[k].id != lists.previous[k])
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
