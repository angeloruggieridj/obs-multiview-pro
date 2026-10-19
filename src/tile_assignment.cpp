// SPDX-License-Identifier: GPL-2.0-or-later
#include "tile_assignment.hpp"

#include <utility>

std::string TileAssignment::scene_for_slot(int slot) const
{
	return valid(slot) ? m_slots[static_cast<size_t>(slot)].name : std::string();
}

int TileAssignment::find_slot(const std::string &name) const
{
	if (name.empty())
		return -1;
	for (size_t i = 0; i < m_slots.size(); ++i)
		if (m_slots[i].name == name)
			return static_cast<int>(i);
	return -1;
}

std::string TileAssignment::id_of(const std::vector<SceneRef> &scenes, const std::string &name) const
{
	for (const SceneRef &s : scenes)
		if (s.name == name)
			return s.id;
	return std::string();
}

void TileAssignment::assign(int slot, const std::string &scene)
{
	if (!valid(slot))
		return;
	if (scene.empty()) {
		remove(slot);
		return;
	}
	const int other = find_slot(scene);
	if (other == slot)
		return;
	Slot incoming{scene, id_of(m_known, scene)};
	if (other >= 0) {
		/* The scene moves; the displaced content takes its old place. */
		m_slots[static_cast<size_t>(other)] = std::move(m_slots[static_cast<size_t>(slot)]);
	}
	m_slots[static_cast<size_t>(slot)] = std::move(incoming);
}

void TileAssignment::swap(int slot_a, int slot_b)
{
	if (!valid(slot_a) || !valid(slot_b) || slot_a == slot_b)
		return;
	std::swap(m_slots[static_cast<size_t>(slot_a)], m_slots[static_cast<size_t>(slot_b)]);
}

void TileAssignment::remove(int slot)
{
	if (valid(slot))
		m_slots[static_cast<size_t>(slot)] = Slot();
}

void TileAssignment::reset_from(const std::vector<SceneRef> &scenes, int slot_count)
{
	m_known = scenes;
	m_slots.assign(slot_count > 0 ? static_cast<size_t>(slot_count) : 0, Slot());
	size_t next = 0;
	for (const SceneRef &s : scenes) {
		if (next >= m_slots.size())
			break;
		if (!s.visible_in_multiview || s.name.empty())
			continue;
		m_slots[next++] = Slot{s.name, s.id};
	}
}

void TileAssignment::set_slot_count(const std::vector<SceneRef> &scenes, int slot_count)
{
	m_known = scenes;
	const size_t old_count = m_slots.size();
	m_slots.resize(slot_count > 0 ? static_cast<size_t>(slot_count) : 0);
	size_t next = old_count;
	for (const SceneRef &s : scenes) {
		if (next >= m_slots.size())
			break;
		if (!s.visible_in_multiview || s.name.empty() || find_slot(s.name) >= 0)
			continue;
		m_slots[next++] = Slot{s.name, s.id};
	}
}

void TileAssignment::on_scene_list_changed(const std::vector<SceneRef> &scenes)
{
	for (Slot &slot : m_slots) {
		if (slot.name.empty())
			continue;
		const SceneRef *found = nullptr;
		if (!slot.id.empty()) {
			for (const SceneRef &s : scenes)
				if (s.id == slot.id) {
					found = &s;
					break;
				}
		}
		if (!found) {
			for (const SceneRef &s : scenes)
				if (s.name == slot.name) {
					found = &s;
					break;
				}
		}
		if (found) {
			slot.name = found->name;
			slot.id = found->id;
		} else {
			slot = Slot();
		}
	}
	m_known = scenes;
}
