// SPDX-License-Identifier: GPL-2.0-or-later
#include "mixer_feeds.hpp"

#include "i18n.hpp"

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
}

unsigned MixerFeeds::refresh(int64_t now_ms)
{
	unsigned changed = kNone;

	/* One counted reference per occupied channel, all released below. */
	std::array<obs_source_t *, kMixerGlobalChannels> src = {};
	std::vector<bool> present(kMixerGlobalChannels, false);
	for (int i = 0; i < kMixerGlobalChannels; ++i) {
		src[size_t(i)] = obs_get_output_source(uint32_t(i + 1));
		/* Being deleted: its channel is about to be cleared. */
		if (src[size_t(i)] && obs_source_removed(src[size_t(i)])) {
			obs_source_release(src[size_t(i)]);
			src[size_t(i)] = nullptr;
		}
		present[size_t(i)] = src[size_t(i)] != nullptr;
	}
	const std::vector<int> wanted = select_mixer_columns(present);

	std::vector<Column> next;
	next.reserve(wanted.size());
	std::vector<bool> kept(m_columns.size(), false);

	for (int id : wanted) {
		/* The existing column of this channel, if any. */
		size_t old_idx = m_columns.size();
		for (size_t k = 0; k < m_columns.size(); ++k)
			if (m_columns[k].channel == id) {
				old_idx = k;
				break;
			}

		if (id == kMixerPgm) {
			if (old_idx < m_columns.size()) {
				kept[old_idx] = true;
				next.push_back(std::move(m_columns[old_idx]));
			} else {
				Column c;
				c.channel = kMixerPgm;
				c.name = T("Tile.MixerProgram").toStdString();
				c.db_state = {true, kMeterFloorDb, now_ms};
				next.push_back(std::move(c));
				changed |= kSetChanged;
			}
			continue;
		}

		obs_source_t *&s = src[size_t(id - 1)];
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

		/* New source on this channel: a new volmeter, which takes our reference. */
		auto feed = std::make_shared<AudioMeter>();
		feed->attach_sources(std::vector<obs_source_t *>{s});
		s = nullptr; /* ownership passed (attach_sources released it on failure) */
		if (!feed->attached())
			continue; /* no volmeter: no column */
		Column c;
		c.channel = id;
		c.feed = std::move(feed);
		c.name = name;
		c.muted = muted;
		c.db_state = {true, kMeterFloorDb, now_ms};
		c.value = mixer_value(muted, kMeterFloorDb);
		next.push_back(std::move(c));
		changed |= kSetChanged;
	}

	for (obs_source_t *s : src)
		if (s)
			obs_source_release(s);

	/* Columns that are gone: detach here (UI thread), then drop. */
	for (size_t k = 0; k < m_columns.size(); ++k)
		if (!kept[k] && m_columns[k].feed) {
			m_columns[k].feed->detach();
			changed |= kSetChanged;
		}

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
