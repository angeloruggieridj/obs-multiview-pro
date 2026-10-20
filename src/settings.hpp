// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Persisted settings model and its JSON form (no OBS / Qt dependency).
//
// File model (version 1):
//   { "version": 1,
//     "global": { "appearance": { "border_color": "#999999", "label_opacity": 50,
//                                 "meter_opacity": 100 },
//                 "updates": { "auto_check": true, "include_prereleases": false,
//                              "last_check_unix": 0, "last_seen_version": "",
//                              "skipped_version": "" } },
//     "instances": { "0": { "preset": "scenes8", "safe_areas": true,
//                           "meters": true,
//                           "collections": { "<collection>": ["scene"|null, ...] } } } }
//
// The JSON is read by a small strict parser written for this fixed schema.
// Reading never throws and never crashes: anything that is not valid JSON of
// this shape, a version other than 1, or more than kMaxSettingsBytes of input
// yields the defaults (an empty SettingsFile) and a non-Ok status. Inside a
// valid file, a field of the wrong type falls back to its default and unknown
// keys are ignored.

#include "appearance.hpp"
#include "layout_model.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

constexpr size_t kMaxSettingsBytes = 1024 * 1024;
constexpr int kMaxInstances = 100;      /* instance indices 0..99 */
constexpr size_t kMaxSlotsStored = 64;  /* per collection list */
constexpr size_t kMaxCollectionsStored = 64; /* per instance: the most recently saved ones are kept */
constexpr size_t kMaxStoredVersionText = 64; /* longer version strings are dropped */
constexpr int64_t kMaxStoredUnixTime = 253402300799; /* 9999-12-31: larger values are clamped */

struct WindowSettings {
	Preset preset = Preset::Scenes8;
	bool safe_areas = true;
	bool meters = true;
	/* Scene collection name -> scene name per slot ("" = empty slot). */
	std::map<std::string, std::vector<std::string>> collections;

	/* Not stored in the file. */
	/* set_options was called on this entry in this session: only then do its
	 * preset / safe_areas / meters override the file's when merging. */
	bool options_touched = false;
	/* Collection names, oldest save first, for pruning (names missing from
	 * here count as older than any listed). */
	std::vector<std::string> collection_order;

	bool operator==(const WindowSettings &o) const
	{
		return preset == o.preset && safe_areas == o.safe_areas && meters == o.meters &&
		       collections == o.collections;
	}
	bool operator!=(const WindowSettings &o) const { return !(*this == o); }
};

/* Global update check settings. The check is automatic by default (once a
 * day, never while streaming or recording); see update_manager.hpp. */
struct UpdateSettings {
	bool auto_check = true;
	bool include_prereleases = false;
	/* Unix seconds of the last automatic or manual attempt, 0 = never. */
	int64_t last_check_unix = 0;
	/* Newest version a check reported (logged once, and the "update
	 * available" marker at startup until it is installed or skipped). */
	std::string last_seen_version;
	/* The user chose "Skip this version": no marker for it. */
	std::string skipped_version;

	bool operator==(const UpdateSettings &o) const
	{
		return auto_check == o.auto_check && include_prereleases == o.include_prereleases &&
		       last_check_unix == o.last_check_unix && last_seen_version == o.last_seen_version &&
		       skipped_version == o.skipped_version;
	}
	bool operator!=(const UpdateSettings &o) const { return !(*this == o); }
};

struct SettingsFile {
	std::map<int, WindowSettings> instances; /* window instance index -> settings */
	Appearance appearance;                   /* global, shared by every window */
	UpdateSettings updates;                  /* global */

	bool operator==(const SettingsFile &o) const
	{
		return instances == o.instances && appearance == o.appearance && updates == o.updates;
	}
	bool operator!=(const SettingsFile &o) const { return !(*this == o); }
};

enum class ParseStatus {
	Ok,           /* also: empty input (nothing saved yet) */
	Corrupt,      /* not valid JSON of the expected shape, or too large */
	WrongVersion, /* valid JSON, "version" is not 1 */
};

/* Stable names stored in the file (never the enum's integer value). */
const char *preset_name(Preset preset);
/* Unknown names give Scenes8, the default. */
Preset preset_from_name(const std::string &name);

std::string to_json(const SettingsFile &file);
SettingsFile from_json(const std::string &json, ParseStatus *status = nullptr);

/* ---- Loading policy (pure, the OS part lives in SettingsStore) ------------ */

/* What reading the file gave. Unreadable = it exists but could not be read
 * completely (sharing violation, permissions, read error). */
enum class LoadOutcome { Missing, Read, Unreadable };

enum class LoadAction {
	UseDefaults,          /* nothing saved: defaults, writing allowed */
	UseParsed,            /* valid content */
	BackupAndUseDefaults, /* fully read but invalid: rename to .bad, defaults */
	BlockWrites,          /* never overwrite what could not be read; retry later */
};

LoadAction decide_load(LoadOutcome outcome, ParseStatus status);

/* Marks `name` as the most recently saved collection of `w`, then drops the
 * oldest ones until at most `max_collections` remain (the marked one is always
 * kept while max_collections >= 1). Keeps the file below the reader's size cap. */
void note_collection_saved(WindowSettings &w, const std::string &name, size_t max_collections = kMaxCollectionsStored);

/* Puts the entries of `overlay` over `base`: the appearance when
 * `overlay_appearance_touched`, the update settings when
 * `overlay_updates_touched`; the options (preset, safe areas,
 * meters) of an instance are taken from `overlay` only when its
 * options_touched is set; collections are merged by name (overlay wins).
 * Used when a file that was unreadable at first can be read later and the
 * changes made meanwhile must be applied to it. */
void merge_settings(SettingsFile &base, const SettingsFile &overlay, bool overlay_appearance_touched = false,
		    bool overlay_updates_touched = false);

/* Assignments may be saved only when no scene collection switch is in progress
 * and the window's collection is still the current one: during a switch the
 * scene list is transient and the slots would be stored under the wrong key. */
bool may_save_assignment(bool switching, const std::string &window_collection, const std::string &current_collection);
