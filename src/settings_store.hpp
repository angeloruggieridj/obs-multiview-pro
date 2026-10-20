// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "layout_model.hpp"
#include "settings.hpp"

#include <QObject>
#include <QTimer>

#include <string>
#include <vector>

/* The settings file of the plugin: one JSON document
 * (obs_module_config_path("settings.json")) with the settings of every window
 * instance. The whole-file model lives in memory; a change updates only its
 * own instance's entry and rewrites the whole file, so instances never
 * clobber each other.
 *
 * Threading: UI thread only (windows, frontend events and plugin unload all
 * run there), so no locking is needed. Nothing here is called from the draw
 * path or from audio threads.
 *
 * Writing: changes are coalesced by a 500 ms single-shot timer (a drag and
 * drop or a burst of toggles is one write); flush() writes at once (window
 * close, plugin unload). The write is atomic (os_quick_write_utf8_file_safe:
 * settings.json.tmp, then renamed over settings.json) and the file is a few
 * hundred bytes, so it never blocks the UI for long. A failed write is logged
 * once and otherwise ignored.
 *
 * Reading: lazily on first use. A file that is not valid, or has another
 * version, is renamed once to settings.json.bad (an older .bad is replaced),
 * a warning is logged once and the defaults are used; the next change writes
 * a fresh file. The user's data is thus kept for inspection but never
 * overwritten silently. A missing or empty file is simply "nothing saved".
 *
 * Instances: a window has an index 0..N (lowest free one when it opens, see
 * plugin.cpp); closing a window frees its index, and the next window to take
 * it finds that instance's saved settings. */
class SettingsStore : public QObject {
public:
	/* Created on first use (UI thread). */
	static SettingsStore &instance();
	/* Flushes pending changes and destroys the store (plugin unload). */
	static void shutdown();

	/* Defaults when the instance has nothing saved. */
	WindowSettings get(int instance);

	void set_options(int instance, Preset preset, bool safe_areas, bool meters);
	/* `slot_names`: scene name per slot, "" = empty. */
	void set_assignment(int instance, const std::string &collection, const std::vector<std::string> &slot_names);

	/* Writes now if something changed since the last write. */
	void flush();

private:
	SettingsStore();
	~SettingsStore() override;

	void ensure_loaded();
	void mark_dirty();
	bool write_file();
	std::string path() const;

	SettingsFile m_file;
	bool m_loaded = false;
	bool m_dirty = false;
	bool m_write_warned = false;
	QTimer m_timer;
};
