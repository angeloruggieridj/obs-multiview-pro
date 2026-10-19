// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Slot -> scene mapping (no OBS / Qt dependency). A scene occupies at most one
// slot; an empty string means an empty tile. Out-of-range slots are no-ops.

#include "scene_ref.hpp"

#include <string>
#include <vector>

class TileAssignment {
public:
	/* "" for an empty or out-of-range slot. */
	std::string scene_for_slot(int slot) const;

	/* Puts `scene` in `slot`. If the scene already sits in another slot the two
	 * slots swap contents (so a scene never appears twice). "" empties the slot. */
	void assign(int slot, const std::string &scene);
	void swap(int slot_a, int slot_b);
	void remove(int slot);

	/* Default population: the first `slot_count` scenes that are visible in
	 * the multiview, in order; the other slots stay empty. */
	void reset_from(const std::vector<SceneRef> &scenes, int slot_count);

	/* Preset change: keeps the assignments of slots that still exist (no
	 * reshuffle). Added slots take, in order, visible scenes not shown yet;
	 * removed slots are dropped. */
	void set_slot_count(const std::vector<SceneRef> &scenes, int slot_count);

	/* `scenes` is the full list (hidden ones too). Slots whose scene is gone
	 * are emptied, the others are untouched; a renamed scene (same id, new
	 * name) follows its new name. Scenes that appeared are not auto-placed. */
	void on_scene_list_changed(const std::vector<SceneRef> &scenes);

private:
	struct Slot {
		std::string name;
		std::string id;
	};

	bool valid(int slot) const { return slot >= 0 && slot < static_cast<int>(m_slots.size()); }
	int find_slot(const std::string &name) const;
	std::string id_of(const std::vector<SceneRef> &scenes, const std::string &name) const;

	std::vector<Slot> m_slots;
	std::vector<SceneRef> m_known;
};
