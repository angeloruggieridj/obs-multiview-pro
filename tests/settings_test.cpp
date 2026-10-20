// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "settings.hpp"

#include <string>
#include <vector>

static WindowSettings sample()
{
	WindowSettings w;
	w.preset = Preset::Scenes16Status;
	w.safe_areas = false;
	w.meters = true;
	w.collections["Main"] = {"Cam 1", "", "Cam 3"};
	w.collections["Other"] = {"", ""};
	return w;
}

static ParseStatus status_of(const std::string &json)
{
	ParseStatus st = ParseStatus::Ok;
	(void)from_json(json, &st);
	return st;
}

TEST_CASE("state_settings_roundtrip")
{
	SettingsFile f;
	f.instances[0] = sample();
	f.instances[3] = WindowSettings();
	ParseStatus st = ParseStatus::Corrupt;
	const SettingsFile back = from_json(to_json(f), &st);
	CHECK(st == ParseStatus::Ok);
	CHECK(back == f);
}

TEST_CASE("state_settings_roundtrip_every_preset")
{
	const Preset all[] = {Preset::Scenes4,       Preset::Scenes8,        Preset::Scenes16,
			      Preset::Scenes24,      Preset::Scenes32,       Preset::Scenes8Status,
			      Preset::Scenes16Status, Preset::Scenes24Status, Preset::Scenes32Status};
	for (Preset p : all) {
		CHECK(preset_from_name(preset_name(p)) == p);
		SettingsFile f;
		f.instances[1].preset = p;
		CHECK(from_json(to_json(f)) == f);
	}
}

TEST_CASE("state_settings_preset_names_are_stable")
{
	CHECK(std::string(preset_name(Preset::Scenes8)) == "scenes8");
	CHECK(std::string(preset_name(Preset::Scenes8Status)) == "scenes8_status");
	CHECK(std::string(preset_name(Preset::Scenes32Status)) == "scenes32_status");
	CHECK(preset_from_name("nonsense") == Preset::Scenes8);
	CHECK(preset_from_name("") == Preset::Scenes8);
}

TEST_CASE("state_settings_defaults")
{
	const WindowSettings d;
	CHECK(d.preset == Preset::Scenes8);
	CHECK(d.safe_areas);
	CHECK(d.meters);
	CHECK(d.collections.empty());
}

TEST_CASE("state_settings_empty_and_blank_input")
{
	CHECK(from_json("") == SettingsFile());
	CHECK(status_of("") == ParseStatus::Ok);
	CHECK(status_of("   \n") == ParseStatus::Corrupt);
}

TEST_CASE("state_settings_corrupt_inputs_give_defaults")
{
	const char *bad[] = {"{",
			     "}",
			     "[]",
			     "null",
			     "42",
			     "\"x\"",
			     "{\"version\":1,",
			     "{\"version\":1,\"instances\":",
			     "{\"version\":1,\"instances\":{\"0\":{\"preset\":\"sce",
			     "{\"version\":1,\"instances\":{}} trailing",
			     "{\"version\":1,\"instances\":{},}",
			     "{'version':1}",
			     "{\"version\":01,\"instances\":{}}",
			     "{\"version\":1,\"instances\":{\"0\":{\"meters\":tru}}}",
			     "{\"version\":1,\"instances\":{\"0\":{\"preset\":\"a\nb\"}}}",
			     "{\"version\":1,\"instances\":{\"0\":{\"preset\":\"\\q\"}}}",
			     "{\"version\":1,\"instances\":{\"0\":{\"preset\":\"\\u12\"}}}",
			     "garbage garbage garbage"};
	for (const char *b : bad) {
		CAPTURE(b);
		ParseStatus st = ParseStatus::Ok;
		const SettingsFile f = from_json(b, &st);
		CHECK(f == SettingsFile());
		CHECK(st == ParseStatus::Corrupt);
	}
}

TEST_CASE("state_settings_truncation_at_every_length_never_crashes")
{
	SettingsFile f;
	f.instances[0] = sample();
	f.instances[1] = sample();
	const std::string full = to_json(f);
	CHECK(!full.empty());
	for (size_t n = 0; n < full.size(); ++n) {
		const std::string cut = full.substr(0, n);
		ParseStatus st = ParseStatus::Ok;
		const SettingsFile r = from_json(cut, &st);
		if (st != ParseStatus::Ok)
			CHECK(r == SettingsFile());
	}
	CHECK(from_json(full) == f);
}

TEST_CASE("state_settings_binary_garbage")
{
	std::string bin;
	for (int i = 0; i < 4096; ++i)
		bin.push_back(static_cast<char>((i * 131 + 7) & 0xff));
	ParseStatus st = ParseStatus::Ok;
	CHECK(from_json(bin, &st) == SettingsFile());
	CHECK(st == ParseStatus::Corrupt);
	const std::string nul = std::string("{\"version\":1,\"instances\":{}}") + std::string(1, '\0') + "x";
	CHECK(status_of(nul) == ParseStatus::Corrupt);
	CHECK(status_of("\xEF\xBB\xBF{\"version\":1,\"instances\":{}}") == ParseStatus::Corrupt);
}

TEST_CASE("state_settings_wrong_version")
{
	ParseStatus st = ParseStatus::Ok;
	CHECK(from_json("{\"version\":2,\"instances\":{\"0\":{\"preset\":\"scenes4\"}}}", &st) == SettingsFile());
	CHECK(st == ParseStatus::WrongVersion);
	CHECK(status_of("{\"version\":\"1\",\"instances\":{}}") == ParseStatus::WrongVersion);
	CHECK(status_of("{\"instances\":{}}") == ParseStatus::WrongVersion);
	CHECK(status_of("{\"version\":1.5,\"instances\":{}}") == ParseStatus::WrongVersion);
}

TEST_CASE("state_settings_unknown_preset_falls_back")
{
	const SettingsFile f =
		from_json("{\"version\":1,\"instances\":{\"0\":{\"preset\":\"scenes99\",\"meters\":false}}}");
	REQUIRE(f.instances.count(0) == 1);
	CHECK(f.instances.at(0).preset == Preset::Scenes8);
	CHECK_FALSE(f.instances.at(0).meters);
}

TEST_CASE("state_settings_wrong_types_use_field_defaults")
{
	const SettingsFile f = from_json("{\"version\":1,\"instances\":{\"0\":{\"preset\":5,\"safe_areas\":\"no\","
					 "\"meters\":null,\"collections\":{\"A\":\"x\",\"B\":[1,true,\"s\",null,[],{}]}},"
					 "\"1\":7,\"2\":[],\"abc\":{},\"-1\":{},\"100\":{},\"01\":{}}}");
	REQUIRE(f.instances.count(0) == 1);
	const WindowSettings &w = f.instances.at(0);
	CHECK(w.preset == Preset::Scenes8);
	CHECK(w.safe_areas);
	CHECK(w.meters);
	CHECK(w.collections.count("A") == 0);
	REQUIRE(w.collections.count("B") == 1);
	const std::vector<std::string> expect = {"", "", "s", "", "", ""};
	CHECK(w.collections.at("B") == expect);
	/* Instances with a non-object entry or an invalid key are dropped. */
	CHECK(f.instances.size() == 1);
}

TEST_CASE("state_settings_unknown_keys_ignored")
{
	const SettingsFile f = from_json("{\"version\":1,\"future\":{\"a\":[1,2,{}]},\"instances\":{\"2\":{"
					 "\"x\":1,\"preset\":\"scenes4\",\"collections\":{}}}}");
	REQUIRE(f.instances.count(2) == 1);
	CHECK(f.instances.at(2).preset == Preset::Scenes4);
}

TEST_CASE("state_settings_duplicate_keys_last_wins")
{
	const SettingsFile f = from_json("{\"version\":1,\"instances\":{\"0\":{\"preset\":\"scenes4\","
					 "\"preset\":\"scenes16\"},\"0\":{\"meters\":false}}}");
	REQUIRE(f.instances.count(0) == 1);
	/* The second "0" replaces the first as a whole. */
	CHECK(f.instances.at(0).preset == Preset::Scenes8);
	CHECK_FALSE(f.instances.at(0).meters);
	const SettingsFile g = from_json("{\"version\":1,\"instances\":{\"0\":{\"preset\":\"scenes4\","
					 "\"preset\":\"scenes16\"}}}");
	CHECK(g.instances.at(0).preset == Preset::Scenes16);
}

TEST_CASE("state_settings_scene_names_with_special_characters")
{
	WindowSettings w;
	w.collections["Col \"quoted\" \\ back"] = {"say \"hi\"",
						    "back\\slash",
						    "line1\nline2\ttab\r",
						    std::string("ctl\x01\x1f end"),
						    "caff\xC3\xA8 \xE6\x97\xA5\xE6\x9C\xAC", /* e-grave, Japanese */
						    "party \xF0\x9F\x8E\x89 emoji",           /* U+1F389 */
						    "</script> {not json} [\"]",
						    "",
						    "slash/ and \x7f del"};
	SettingsFile f;
	f.instances[0] = w;
	const std::string json = to_json(f);
	/* No raw control characters except the layout newlines. */
	for (char c : json)
		CHECK((static_cast<unsigned char>(c) >= 0x20 || c == 0x0A));
	CHECK(from_json(json) == f);
}

TEST_CASE("state_settings_non_ascii_passes_through_unescaped")
{
	SettingsFile f;
	f.instances[0].collections["c"] = {"caff\xC3\xA8"};
	CHECK(to_json(f).find("caff\xC3\xA8") != std::string::npos);
}

TEST_CASE("state_settings_parses_unicode_escapes_and_surrogate_pairs")
{
	const SettingsFile f = from_json("{\"version\":1,\"instances\":{\"0\":{\"collections\":{\"c\":["
					 "\"\\u00e8\",\"\\ud83c\\udf89\",\"a\\/b\\b\\f\\n\\r\\t\\\"\\\\\","
					 "\"\\ud83c\",\"\\udf89x\",\"\\u0000\"]}}}}");
	REQUIRE(f.instances.count(0) == 1);
	const std::vector<std::string> &v = f.instances.at(0).collections.at("c");
	REQUIRE(v.size() == 6);
	CHECK(v[0] == "\xC3\xA8");
	CHECK(v[1] == "\xF0\x9F\x8E\x89");
	CHECK(v[2] == "a/b\b\f\n\r\t\"\\");
	CHECK(v[3] == "\xEF\xBF\xBD");  /* lone high surrogate */
	CHECK(v[4] == "\xEF\xBF\xBDx"); /* lone low surrogate */
	CHECK(v[5].empty());            /* NUL cannot live in a scene name: treated as empty */
}

TEST_CASE("state_settings_invalid_utf8_in_name_is_sanitized_on_write")
{
	SettingsFile f;
	f.instances[0].collections["c"] = {"bad\xFF\xFE"
					   "x",
					   "trunc\xE6\x97", "over\xC0\x80long"};
	ParseStatus st = ParseStatus::Corrupt;
	const SettingsFile back = from_json(to_json(f), &st);
	CHECK(st == ParseStatus::Ok); /* the file stays readable */
	REQUIRE(back.instances.count(0) == 1);
	CHECK(back.instances.at(0).collections.at("c").size() == 3);
}

TEST_CASE("state_settings_invalid_utf8_in_file_is_corrupt")
{
	CHECK(status_of("{\"version\":1,\"instances\":{\"0\":{\"collections\":{\"\xFF\":[]}}}}") ==
	      ParseStatus::Corrupt);
}

TEST_CASE("state_settings_null_slots")
{
	const SettingsFile f = from_json("{\"version\":1,\"instances\":{\"0\":{\"collections\":{\"c\":"
					 "[null,\"A\",null]}}}}");
	const std::vector<std::string> expect = {"", "A", ""};
	REQUIRE(f.instances.count(0) == 1);
	CHECK(f.instances.at(0).collections.at("c") == expect);
	/* Empty slots are written as null. */
	SettingsFile g;
	g.instances[0].collections["c"] = expect;
	CHECK(to_json(g).find("[null,\"A\",null]") != std::string::npos);
}

TEST_CASE("state_settings_many_slots")
{
	SettingsFile f;
	std::vector<std::string> names;
	for (int i = 0; i < 40; ++i)
		names.push_back(i % 5 == 0 ? std::string() : "Scene " + std::to_string(i));
	f.instances[0].collections["big"] = names;
	CHECK(from_json(to_json(f)) == f);
}

TEST_CASE("state_settings_slot_list_is_capped")
{
	std::string json = "{\"version\":1,\"instances\":{\"0\":{\"collections\":{\"c\":[";
	for (size_t i = 0; i < kMaxSlotsStored + 50; ++i)
		json += i ? ",\"s\"" : "\"s\"";
	json += "]}}}}";
	const SettingsFile f = from_json(json);
	REQUIRE(f.instances.count(0) == 1);
	CHECK(f.instances.at(0).collections.at("c").size() == kMaxSlotsStored);
}

TEST_CASE("state_settings_deep_nesting_is_rejected_without_overflow")
{
	const std::string deep(100000, '[');
	ParseStatus st = ParseStatus::Ok;
	CHECK(from_json(deep, &st) == SettingsFile());
	CHECK(st == ParseStatus::Corrupt);
	std::string json = "{\"version\":1,\"x\":";
	for (int i = 0; i < 5000; ++i)
		json += "{\"a\":";
	json += "1";
	for (int i = 0; i < 5000; ++i)
		json += "}";
	json += ",\"instances\":{}}";
	CHECK(status_of(json) == ParseStatus::Corrupt);
	/* A reasonable depth in an unknown key is fine. */
	CHECK(status_of("{\"version\":1,\"x\":[[[[1]]]],\"instances\":{}}") == ParseStatus::Ok);
}

TEST_CASE("state_settings_huge_input_is_rejected")
{
	std::string big = "{\"version\":1,\"x\":\"";
	big.append(kMaxSettingsBytes, 'a');
	big += "\",\"instances\":{}}";
	ParseStatus st = ParseStatus::Ok;
	CHECK(from_json(big, &st) == SettingsFile());
	CHECK(st == ParseStatus::Corrupt);
}

TEST_CASE("state_settings_numbers_and_literals_in_unknown_keys")
{
	CHECK(status_of("{\"version\":1,\"a\":-1.5e+3,\"b\":0,\"c\":true,\"d\":false,\"e\":null,"
			"\"instances\":{}}") == ParseStatus::Ok);
	CHECK(status_of("{\"version\":1,\"a\":1.,\"instances\":{}}") == ParseStatus::Corrupt);
	CHECK(status_of("{\"version\":1,\"a\":-,\"instances\":{}}") == ParseStatus::Corrupt);
	CHECK(status_of("{\"version\":1,\"a\":.5,\"instances\":{}}") == ParseStatus::Corrupt);
	CHECK(status_of(" \t\r\n{\"version\":1,\"instances\":{}} \n") == ParseStatus::Ok);
}

TEST_CASE("state_settings_output_is_deterministic")
{
	SettingsFile f;
	f.instances[2] = sample();
	f.instances[0] = sample();
	CHECK(to_json(f) == to_json(from_json(to_json(f))));
	CHECK(!to_json(f).empty());
}

TEST_CASE("state_settings_load_decision")
{
	/* Nothing saved yet: defaults, writing allowed. */
	CHECK(decide_load(LoadOutcome::Missing, ParseStatus::Ok) == LoadAction::UseDefaults);
	/* Fully read: valid is used, invalid is set aside as .bad. */
	CHECK(decide_load(LoadOutcome::Read, ParseStatus::Ok) == LoadAction::UseParsed);
	CHECK(decide_load(LoadOutcome::Read, ParseStatus::Corrupt) == LoadAction::BackupAndUseDefaults);
	CHECK(decide_load(LoadOutcome::Read, ParseStatus::WrongVersion) == LoadAction::BackupAndUseDefaults);
	/* Exists but could not be read: never touch it (no .bad, no overwrite),
	 * whatever the (meaningless) parse status says. */
	CHECK(decide_load(LoadOutcome::Unreadable, ParseStatus::Ok) == LoadAction::BlockWrites);
	CHECK(decide_load(LoadOutcome::Unreadable, ParseStatus::Corrupt) == LoadAction::BlockWrites);
}

TEST_CASE("state_settings_merge_keeps_file_and_applies_in_memory_changes")
{
	SettingsFile base;
	base.instances[0].preset = Preset::Scenes24;
	base.instances[0].collections["A"] = {"a1", "a2"};
	base.instances[0].collections["B"] = {"b1"};
	base.instances[2].meters = false;
	SettingsFile over;
	over.instances[0].preset = Preset::Scenes4;
	over.instances[0].options_touched = true;
	over.instances[0].collections["B"] = {"b9"};
	over.instances[1].safe_areas = false;
	over.instances[1].options_touched = true;
	merge_settings(base, over);
	CHECK(base.instances.at(0).preset == Preset::Scenes4);
	CHECK(base.instances.at(0).collections.at("A") == std::vector<std::string>({"a1", "a2"}));
	CHECK(base.instances.at(0).collections.at("B") == std::vector<std::string>({"b9"}));
	CHECK_FALSE(base.instances.at(1).safe_areas);
	CHECK_FALSE(base.instances.at(2).meters);
}

TEST_CASE("state_settings_assignment_save_guard")
{
	/* Normal: the window's collection is the current one. */
	CHECK(may_save_assignment(false, "A", "A"));
	/* A collection switch is in progress: never. */
	CHECK_FALSE(may_save_assignment(true, "A", "A"));
	CHECK_FALSE(may_save_assignment(true, "A", "B"));
	/* The current collection is no longer the one the slots belong to. */
	CHECK_FALSE(may_save_assignment(false, "A", "B"));
	CHECK(may_save_assignment(false, "", ""));
}

TEST_CASE("state_settings_merge_keeps_file_options_of_an_untouched_instance")
{
	SettingsFile base;
	base.instances[0].preset = Preset::Scenes24;
	base.instances[0].safe_areas = false;
	base.instances[0].meters = false;
	SettingsFile over; /* only set_assignment was called: defaults + a collection */
	over.instances[0].collections["A"] = {"a1"};
	merge_settings(base, over);
	CHECK(base.instances.at(0).preset == Preset::Scenes24);
	CHECK_FALSE(base.instances.at(0).safe_areas);
	CHECK_FALSE(base.instances.at(0).meters);
	CHECK(base.instances.at(0).collections.at("A") == std::vector<std::string>({"a1"}));
}

TEST_CASE("state_settings_collections_are_capped_to_the_most_recent")
{
	WindowSettings w;
	for (int i = 0; i < 10; ++i) {
		const std::string n = "c" + std::to_string(i);
		w.collections[n] = {"s"};
		note_collection_saved(w, n, 4);
	}
	CHECK(w.collections.size() == 4);
	CHECK(w.collections.count("c9"));
	CHECK(w.collections.count("c6"));
	CHECK_FALSE(w.collections.count("c5"));
	/* saving an older one again makes it the newest */
	w.collections["c6"] = {"x"};
	note_collection_saved(w, "c6", 4);
	w.collections["c10"] = {"s"};
	note_collection_saved(w, "c10", 4);
	CHECK(w.collections.count("c6"));
	CHECK_FALSE(w.collections.count("c7"));
	/* names that were never ordered (loaded from a file) are the oldest */
	WindowSettings f;
	f.collections = {{"a", {"1"}}, {"b", {"1"}}, {"c", {"1"}}};
	f.collections["d"] = {"1"};
	note_collection_saved(f, "d", 2);
	CHECK(f.collections.size() == 2);
	CHECK(f.collections.count("d"));
	CHECK(f.collections.count("c"));
	/* the capped file stays far below the reader's limit */
	SettingsFile file;
	WindowSettings big;
	for (int i = 0; i < 200; ++i) {
		const std::string n = std::string(200, 'n') + std::to_string(i);
		big.collections[n] = std::vector<std::string>(kMaxSlotsStored, std::string(100, 's'));
		note_collection_saved(big, n);
	}
	file.instances[0] = big;
	CHECK(big.collections.size() == kMaxCollectionsStored);
	CHECK(to_json(file).size() < kMaxSettingsBytes);
}
