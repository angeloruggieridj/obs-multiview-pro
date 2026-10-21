// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// User-visible text. Every word the plugin shows comes from the locale files
// data/locale/<lang>.ini (en-US.ini is the source of truth), loaded by OBS for
// the language set in OBS (OBS_MODULE_USE_DEFAULT_LOCALE in plugin.cpp); keys
// missing in a translation fall back to en-US. Keys are dotted, grouped by
// where the text appears ("Menu.Layout", "Tile.OnAir", "Dialog.Tab.About").
// Placeholders are %1, %2 inside the value. UI thread or any thread after the
// module is loaded (the lookup is read-only).
//
// Adding a language only means adding data/locale/<lang>.ini.

#include <QString>

/* The text of `key` in the current language. A key no locale file has comes
 * back as the key itself, with one warning in the OBS log per key. */
QString T(const char *key);

/* T(key) with %1 (and %2) replaced. */
QString T(const char *key, const QString &arg1);
QString T(const char *key, const QString &arg1, const QString &arg2);
