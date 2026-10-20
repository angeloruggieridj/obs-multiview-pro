// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "tile_assignment.hpp"

static std::vector<SceneRef> scenes_named(std::initializer_list<const char *> names)
{
	std::vector<SceneRef> v;
	for (const char *n : names) {
		SceneRef s;
		s.name = n;
		v.push_back(s);
	}
	return v;
}

TEST_CASE("state_assignment_reset_from_fills_first_slots")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A", "B", "C"}), 8);
	CHECK(a.scene_for_slot(0) == "A");
	CHECK(a.scene_for_slot(1) == "B");
	CHECK(a.scene_for_slot(2) == "C");
	for (int i = 3; i < 8; ++i)
		CHECK(a.scene_for_slot(i).empty());
}

TEST_CASE("state_assignment_reset_from_more_scenes_than_slots")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A", "B", "C", "D"}), 2);
	CHECK(a.scene_for_slot(1) == "B");
	CHECK(a.scene_for_slot(2).empty());
}

TEST_CASE("state_assignment_reset_from_skips_hidden_scenes")
{
	std::vector<SceneRef> v = scenes_named({"A", "B", "C"});
	v[1].visible_in_multiview = false;
	TileAssignment a;
	a.reset_from(v, 4);
	CHECK(a.scene_for_slot(0) == "A");
	CHECK(a.scene_for_slot(1) == "C");
	CHECK(a.scene_for_slot(2).empty());
}

TEST_CASE("state_assignment_swap_exchanges")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A", "B", "C"}), 8);
	a.swap(0, 2);
	CHECK(a.scene_for_slot(0) == "C");
	CHECK(a.scene_for_slot(1) == "B");
	CHECK(a.scene_for_slot(2) == "A");
}

TEST_CASE("state_assignment_swap_with_empty_moves")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A"}), 4);
	a.swap(0, 3);
	CHECK(a.scene_for_slot(0).empty());
	CHECK(a.scene_for_slot(3) == "A");
}

TEST_CASE("state_assignment_remove_empties_only_that_slot")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A", "B", "C"}), 8);
	a.remove(1);
	CHECK(a.scene_for_slot(0) == "A");
	CHECK(a.scene_for_slot(1).empty());
	CHECK(a.scene_for_slot(2) == "C");
}

TEST_CASE("state_assignment_assign_sets_and_moves_existing")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A", "B", "C"}), 8);
	a.assign(5, "X");
	CHECK(a.scene_for_slot(5) == "X");
	/* "A" already sits in slot 0: the two slots swap, no duplicate. */
	a.assign(2, "A");
	CHECK(a.scene_for_slot(2) == "A");
	CHECK(a.scene_for_slot(0) == "C");
	/* Assigning to an empty slot a scene shown elsewhere moves it. */
	a.assign(7, "B");
	CHECK(a.scene_for_slot(7) == "B");
	CHECK(a.scene_for_slot(1).empty());
	/* Assigning the scene a slot already has changes nothing. */
	a.assign(7, "B");
	CHECK(a.scene_for_slot(7) == "B");
	a.assign(7, "");
	CHECK(a.scene_for_slot(7).empty());
}

TEST_CASE("state_assignment_out_of_range_is_noop")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A", "B"}), 4);
	a.assign(-1, "Z");
	a.assign(4, "Z");
	a.swap(0, 9);
	a.swap(-3, 1);
	a.remove(12);
	a.remove(-1);
	CHECK(a.scene_for_slot(-1).empty());
	CHECK(a.scene_for_slot(4).empty());
	CHECK(a.scene_for_slot(0) == "A");
	CHECK(a.scene_for_slot(1) == "B");
	CHECK(a.scene_for_slot(2).empty());
}

TEST_CASE("state_assignment_no_scenes")
{
	TileAssignment a;
	a.reset_from({}, 8);
	for (int i = 0; i < 8; ++i)
		CHECK(a.scene_for_slot(i).empty());
	a.on_scene_list_changed({});
	a.swap(0, 1);
	CHECK(a.scene_for_slot(0).empty());
}

TEST_CASE("state_assignment_scene_deleted_empties_only_its_slot")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A", "B", "C"}), 8);
	a.on_scene_list_changed(scenes_named({"A", "C"}));
	CHECK(a.scene_for_slot(0) == "A");
	CHECK(a.scene_for_slot(1).empty());
	CHECK(a.scene_for_slot(2) == "C");
}

TEST_CASE("state_assignment_new_scene_is_not_auto_placed")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A"}), 4);
	a.on_scene_list_changed(scenes_named({"A", "N"}));
	CHECK(a.scene_for_slot(0) == "A");
	CHECK(a.scene_for_slot(1).empty());
}

TEST_CASE("state_assignment_hidden_assigned_scene_survives_list_change")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A"}), 4);
	a.assign(2, "H");
	std::vector<SceneRef> v = scenes_named({"A", "H"});
	v[1].visible_in_multiview = false;
	a.on_scene_list_changed(v);
	CHECK(a.scene_for_slot(2) == "H");
}

TEST_CASE("state_assignment_rename_follows_by_id")
{
	std::vector<SceneRef> v = scenes_named({"A", "B"});
	v[0].id = "ida";
	v[1].id = "idb";
	TileAssignment a;
	a.reset_from(v, 4);
	v[1].name = "B2";
	a.on_scene_list_changed(v);
	CHECK(a.scene_for_slot(1) == "B2");
	CHECK(a.scene_for_slot(0) == "A");
}

TEST_CASE("state_assignment_rename_without_id_empties_slot")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A", "B"}), 4);
	a.on_scene_list_changed(scenes_named({"A", "B2"}));
	CHECK(a.scene_for_slot(1).empty());
}

TEST_CASE("state_assignment_set_slot_count_keeps_and_fills")
{
	std::vector<SceneRef> v = scenes_named({"A", "B", "C", "D", "E"});
	TileAssignment a;
	a.reset_from(v, 2);
	a.swap(0, 1); /* B A */
	a.set_slot_count(v, 4);
	CHECK(a.scene_for_slot(0) == "B");
	CHECK(a.scene_for_slot(1) == "A");
	CHECK(a.scene_for_slot(2) == "C");
	CHECK(a.scene_for_slot(3) == "D");
	a.set_slot_count(v, 3);
	CHECK(a.scene_for_slot(2) == "C");
	CHECK(a.scene_for_slot(3).empty());
}

TEST_CASE("state_assignment_assign_after_rename_keeps_id_for_next_rename")
{
	std::vector<SceneRef> v = scenes_named({"A", "B"});
	v[0].id = "ida";
	v[1].id = "idb";
	TileAssignment a;
	a.reset_from(v, 4);
	/* B renamed; the window syncs (on_scene_list_changed) before resolving names. */
	v[1].name = "B2";
	a.on_scene_list_changed(v);
	a.assign(3, "B2");
	CHECK(a.scene_for_slot(1).empty());
	CHECK(a.scene_for_slot(3) == "B2");
	v[1].name = "B3";
	a.on_scene_list_changed(v);
	CHECK(a.scene_for_slot(3) == "B3");
}

#include "layout_model.hpp"

static int scene_count_of(Preset p)
{
	int n = 0;
	for (const Tile &t : build_layout(p))
		if (t.kind == TileKind::Scene)
			++n;
	return n;
}

TEST_CASE("state_assignment_switching_between_plain_and_status_presets_keeps_surviving_slots")
{
	TileAssignment a;
	const auto scenes = scenes_named({"A", "B", "C", "D", "E", "F", "G", "H", "I"});
	a.reset_from(scenes, scene_count_of(Preset::Scenes8));
	a.assign(1, "H"); /* user rearranged: slot 1 = H */
	a.assign(2, "");  /* slot 2 emptied */
	REQUIRE(a.scene_for_slot(7) == "B"); /* H swapped with B */

	/* 8 -> 8 with status: only 4 scene slots; the first four keep their content. */
	a.set_slot_count(scenes, scene_count_of(Preset::Scenes8Status));
	CHECK(a.scene_for_slot(0) == "A");
	CHECK(a.scene_for_slot(1) == "H");
	CHECK(a.scene_for_slot(2).empty());
	CHECK(a.scene_for_slot(3) == "D");
	CHECK(a.scene_for_slot(4).empty()); /* no slot there any more */

	/* And back: the surviving slots are untouched. */
	a.set_slot_count(scenes, scene_count_of(Preset::Scenes8));
	CHECK(a.scene_for_slot(0) == "A");
	CHECK(a.scene_for_slot(1) == "H");
	CHECK(a.scene_for_slot(2).empty());
	CHECK(a.scene_for_slot(3) == "D");
}

TEST_CASE("state_assignment_names_lists_slots_in_order")
{
	TileAssignment a;
	a.reset_from(scenes_named({"A", "B"}), 4);
	const std::vector<std::string> expect = {"A", "B", "", ""};
	CHECK(a.names(4) == expect);
	/* Beyond the slot count: empty entries, never out of range. */
	CHECK(a.names(6).size() == 6);
	CHECK(a.names(0).empty());
	CHECK(a.names(-3).empty());
}

TEST_CASE("state_assignment_load_restores_saved_names")
{
	TileAssignment a;
	a.load(scenes_named({"A", "B", "C", "D"}), {"D", "", "A", ""}, 4);
	CHECK(a.scene_for_slot(0) == "D");
	CHECK(a.scene_for_slot(1).empty()); /* an empty slot stays empty */
	CHECK(a.scene_for_slot(2) == "A");
	CHECK(a.scene_for_slot(3).empty());
}

TEST_CASE("state_assignment_load_ignores_scenes_that_no_longer_exist")
{
	TileAssignment a;
	a.load(scenes_named({"A", "C"}), {"A", "Gone", "C", "Gone"}, 4);
	CHECK(a.scene_for_slot(0) == "A");
	CHECK(a.scene_for_slot(1).empty()); /* no phantom slot, no shifting */
	CHECK(a.scene_for_slot(2) == "C");
	CHECK(a.scene_for_slot(3).empty());
}

TEST_CASE("state_assignment_load_ignores_duplicates_and_hidden_scenes")
{
	std::vector<SceneRef> v = scenes_named({"A", "B", "H"});
	v[2].visible_in_multiview = false;
	TileAssignment a;
	a.load(v, {"A", "A", "H", "B"}, 4);
	CHECK(a.scene_for_slot(0) == "A");
	CHECK(a.scene_for_slot(1).empty());
	CHECK(a.scene_for_slot(2).empty());
	CHECK(a.scene_for_slot(3) == "B");
}

TEST_CASE("state_assignment_load_shorter_list_fills_added_slots_with_unseen_scenes")
{
	TileAssignment a;
	a.load(scenes_named({"A", "B", "C", "D"}), {"C", ""}, 4);
	CHECK(a.scene_for_slot(0) == "C");
	CHECK(a.scene_for_slot(1).empty());
	CHECK(a.scene_for_slot(2) == "A"); /* slots past the saved list: not-yet-shown scenes */
	CHECK(a.scene_for_slot(3) == "B");
}

TEST_CASE("state_assignment_load_longer_list_is_truncated")
{
	TileAssignment a;
	a.load(scenes_named({"A", "B", "C"}), {"A", "B", "C"}, 2);
	CHECK(a.scene_for_slot(1) == "B");
	CHECK(a.scene_for_slot(2).empty());
	CHECK(a.names(3).size() == 3);
}

TEST_CASE("state_assignment_load_keeps_ids_so_renames_still_follow")
{
	std::vector<SceneRef> v = scenes_named({"A", "B"});
	v[0].id = "id-a";
	v[1].id = "id-b";
	TileAssignment a;
	a.load(v, {"A", "B"}, 2);
	v[0].name = "A renamed";
	a.on_scene_list_changed(v);
	CHECK(a.scene_for_slot(0) == "A renamed");
}

TEST_CASE("state_assignment_load_with_no_scenes_or_no_slots")
{
	TileAssignment a;
	a.load({}, {"A"}, 3);
	CHECK(a.scene_for_slot(0).empty());
	a.load(scenes_named({"A"}), {"A"}, 0);
	CHECK(a.scene_for_slot(0).empty());
}
