// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings_store.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <cstdio>

static SettingsStore *g_store = nullptr; /* UI thread only */

static constexpr int kSaveDelayMs = 500;

SettingsStore &SettingsStore::instance()
{
	if (!g_store)
		g_store = new SettingsStore();
	return *g_store;
}

void SettingsStore::shutdown()
{
	if (!g_store)
		return;
	g_store->flush();
	delete g_store;
	g_store = nullptr;
}

SettingsStore::SettingsStore()
{
	m_timer.setSingleShot(true);
	m_timer.setInterval(kSaveDelayMs);
	connect(&m_timer, &QTimer::timeout, this, [this] { flush(); });
}

SettingsStore::~SettingsStore() = default;

std::string SettingsStore::path() const
{
	char *p = obs_module_config_path("settings.json");
	if (!p)
		return std::string();
	std::string out(p);
	bfree(p);
	return out;
}

void SettingsStore::ensure_loaded()
{
	if (m_loaded)
		return;
	m_loaded = true;

	const std::string file = path();
	if (file.empty() || !os_file_exists(file.c_str()))
		return; /* nothing saved yet */

	std::string text;
	bool too_big = false;
	if (FILE *f = os_fopen(file.c_str(), "rb")) {
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
			text.append(buf, n);
			if (text.size() > kMaxSettingsBytes) {
				too_big = true;
				break;
			}
		}
		fclose(f);
	} else {
		obs_log(LOG_WARNING, "cannot read the settings file; using defaults");
		return;
	}

	ParseStatus status = ParseStatus::Ok;
	SettingsFile parsed = too_big ? SettingsFile() : from_json(text, &status);
	if (too_big)
		status = ParseStatus::Corrupt;
	if (status == ParseStatus::Ok) {
		m_file = std::move(parsed);
		return;
	}

	/* Keep the unreadable file for inspection (once; an older .bad is
	 * replaced) and carry on with the defaults. */
	const std::string bad = file + ".bad";
	os_unlink(bad.c_str());
	const bool kept = os_rename(file.c_str(), bad.c_str()) == 0;
	obs_log(LOG_WARNING, "settings file %s; using defaults%s",
		status == ParseStatus::WrongVersion ? "has an unsupported version" : "is not valid",
		kept ? " (the file was renamed to settings.json.bad)" : "");
}

WindowSettings SettingsStore::get(int instance)
{
	ensure_loaded();
	const auto it = m_file.instances.find(instance);
	return it == m_file.instances.end() ? WindowSettings() : it->second;
}

void SettingsStore::set_options(int instance, Preset preset, bool safe_areas, bool meters)
{
	if (instance < 0 || instance >= kMaxInstances)
		return;
	ensure_loaded();
	WindowSettings &w = m_file.instances[instance];
	if (w.preset == preset && w.safe_areas == safe_areas && w.meters == meters)
		return;
	w.preset = preset;
	w.safe_areas = safe_areas;
	w.meters = meters;
	mark_dirty();
}

void SettingsStore::set_assignment(int instance, const std::string &collection, const std::vector<std::string> &slot_names)
{
	if (instance < 0 || instance >= kMaxInstances)
		return;
	ensure_loaded();
	std::vector<std::string> kept = slot_names;
	if (kept.size() > kMaxSlotsStored)
		kept.resize(kMaxSlotsStored);
	WindowSettings &w = m_file.instances[instance];
	const auto it = w.collections.find(collection);
	if (it != w.collections.end() && it->second == kept)
		return;
	w.collections[collection] = std::move(kept);
	mark_dirty();
}

void SettingsStore::mark_dirty()
{
	m_dirty = true;
	m_timer.start(); /* restarts: a burst is one write */
}

void SettingsStore::flush()
{
	m_timer.stop();
	if (!m_dirty)
		return;
	m_dirty = false;
	if (!write_file() && !m_write_warned) {
		m_write_warned = true;
		obs_log(LOG_WARNING, "cannot save the multiview settings (further failures are not logged)");
	}
}

bool SettingsStore::write_file()
{
	const std::string file = path();
	if (file.empty())
		return false;
	/* The plugin config directory may not exist yet. */
	const size_t slash = file.find_last_of("/\\");
	if (slash != std::string::npos) {
		const std::string dir = file.substr(0, slash);
		os_mkdirs(dir.c_str());
	}
	const std::string json = to_json(m_file);
	return os_quick_write_utf8_file_safe(file.c_str(), json.data(), json.size(), false, "tmp", nullptr);
}
