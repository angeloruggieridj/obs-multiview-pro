// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "audio_meter.hpp"
#include "mixer_columns.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/* The columns of the mixer tile and what feeds them (UI thread only).
 *
 * The last column is PGM (the program mix: the renderer's own AudioMeter, so
 * `feed` is null there). The others, in channel order, are the OBS global audio devices, i.e. the sources
 * set on output channels 1..6 (Desktop Audio, Mic/Aux ...): each has an
 * AudioMeter with exactly one volmeter on it, which holds a counted reference
 * of the source (the same discipline as the preview feed of the Task 9). At
 * most kMixerMaxColumns columns.
 *
 * refresh() is called from the 500 ms poll and when the scenes change: it
 * enumerates the output channels (obs_get_output_source: one counted
 * reference each, every one released before returning), compares the pointer
 * with the source the column's volmeter is attached to, and attaches / detaches
 * only what changed. A source removed, or replaced on its channel, loses its
 * volmeter (and its reference) at that call. Names and mute states are read at
 * the same time.
 *
 * The dB text of a column follows db_text_update_due (at most twice a second,
 * only for a change of 1 dB or more); the mute word shows at once.
 *
 * Audio threads are not involved here beyond AudioMeter's own rules (atomics
 * only). The destructor detaches everything. */
class MixerFeeds {
public:
	struct Column {
		int channel = kMixerPgm;            /* 0 = PGM, n = OBS output channel n */
		std::shared_ptr<AudioMeter> feed;   /* null for PGM */
		std::string name;
		bool muted = false;
		DbTextState db_state;
		MixerValue value = {"--", MixerValueStyle::Dim};
	};

	enum Change : unsigned { kNone = 0, kSetChanged = 1, kTextChanged = 2 };

	MixerFeeds() = default;
	~MixerFeeds();
	MixerFeeds(const MixerFeeds &) = delete;
	MixerFeeds &operator=(const MixerFeeds &) = delete;

	/* Re-reads the global audio devices (see above). Returns Change bits:
	 * kSetChanged when a column appeared, vanished or changed source,
	 * kTextChanged when a name or a mute state changed. */
	unsigned refresh(int64_t now_ms);

	/* `hold_db[i]` is the peak-hold level of column i (the larger channel), as
	 * measured by the graphics thread. Updates the dB texts that are due.
	 * Returns kTextChanged when one did. */
	unsigned apply_levels(const float *hold_db, size_t count, int64_t now_ms);

	/* Detaches every volmeter and drops the columns. Idempotent. */
	void clear();

	const std::vector<Column> &columns() const { return m_columns; }
	/* Changes whenever the set of columns / feeds changes (not for texts). */
	uint64_t generation() const { return m_generation; }
	std::vector<MixerColumnView> view() const;

private:
	std::vector<Column> m_columns;
	uint64_t m_generation = 1;
};
