// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Persisted settings model and its JSON form (no OBS / Qt dependency).
//
// File model (version 1):
//   { "version": 1,
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

#include "layout_model.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

constexpr size_t kMaxSettingsBytes = 1024 * 1024;
constexpr int kMaxInstances = 100;      /* instance indices 0..99 */
constexpr size_t kMaxSlotsStored = 64;  /* per collection list */

struct WindowSettings {
	Preset preset = Preset::Scenes8;
	bool safe_areas = true;
	bool meters = true;
	/* Scene collection name -> scene name per slot ("" = empty slot). */
	std::map<std::string, std::vector<std::string>> collections;

	bool operator==(const WindowSettings &o) const
	{
		return preset == o.preset && safe_areas == o.safe_areas && meters == o.meters &&
		       collections == o.collections;
	}
	bool operator!=(const WindowSettings &o) const { return !(*this == o); }
};

struct SettingsFile {
	std::map<int, WindowSettings> instances; /* window instance index -> settings */

	bool operator==(const SettingsFile &o) const { return instances == o.instances; }
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
