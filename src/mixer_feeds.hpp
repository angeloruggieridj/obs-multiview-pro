// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "mixer_columns.hpp"

#include <obs.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/* The columns of the mixer tile (UI thread only).
 *
 * The last column is PGM (the program mix). The others are the audio sources,
 * as select_mixer_columns picks them: the OBS global audio devices (the
 * sources set on output channels 1..6: Desktop Audio, Mic/Aux ...), the audio
 * sources of the program scene and, in studio mode, those of the preview
 * scene; one column per source however many places it is in. The columns are
 * stable: a source keeps its place while it is listed (a studio-mode cut moves
 * nothing), new sources are appended, gone ones leave no gap. At most
 * kMixerMaxColumns columns; the sources beyond are not shown (logged once per
 * new highest number of them in a session).
 *
 * The levels are not read here: the renderer meters each column's source
 * through its MeterPool (one volmeter per source, shared with the Preview
 * meter and the scene tiles) by the column's id, which is the source address.
 *
 * refresh() is called from the 500 ms poll and when the scenes, the program,
 * the preview or studio mode change, with the lists the caller holds counted
 * references of. Names and mute states are read at the same time.
 *
 * The dB text of a column follows db_text_update_due (at most twice a second,
 * only for a change of 1 dB or more); the mute word shows at once. */
class MixerFeeds {
public:
	struct Column {
		MixerSourceId id = kMixerPgm; /* kMixerPgm, or the source's address */
		std::string name;
		bool muted = false;
		DbTextState db_state;
		MixerValue value = {"--", MixerValueStyle::Dim};
	};

	enum Change : unsigned { kNone = 0, kSetChanged = 1, kTextChanged = 2 };

	/* `devices` (channel order, null for an empty channel), `program` and
	 * `preview` are borrowed: the caller holds their references for the call.
	 * `preview` counts only when `studio_mode`. Returns Change bits:
	 * kSetChanged when a column appeared, vanished or moved, kTextChanged when
	 * a name or a mute state changed. */
	unsigned refresh(int64_t now_ms, const std::vector<obs_source_t *> &devices,
			 const std::vector<obs_source_t *> &program, const std::vector<obs_source_t *> &preview,
			 bool studio_mode);

	/* `hold_db[i]` is the peak-hold level of column i (the larger channel), as
	 * measured by the graphics thread. Updates the dB texts that are due.
	 * Returns kTextChanged when one did. */
	unsigned apply_levels(const float *hold_db, size_t count, int64_t now_ms);

	/* Drops the columns. Idempotent. */
	void clear();

	const std::vector<Column> &columns() const { return m_columns; }
	/* Changes whenever the set or the order of the columns changes (not for texts). */
	uint64_t generation() const { return m_generation; }
	std::vector<MixerColumnView> view() const;

private:
	std::vector<Column> m_columns;
	uint64_t m_generation = 1;
	size_t m_dropped_logged = 0; /* highest number of left-out sources logged so far */
};
