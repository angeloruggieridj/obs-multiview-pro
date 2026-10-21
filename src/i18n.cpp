// SPDX-License-Identifier: GPL-2.0-or-later
#include "i18n.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <mutex>
#include <set>
#include <string>

QString T(const char *key)
{
	const char *text = obs_module_text(key);
	if (text == key) {
		/* Not in any locale file: logged once per key. */
		static std::mutex mutex;
		static std::set<std::string> reported;
		std::lock_guard<std::mutex> lock(mutex);
		if (reported.insert(key).second)
			obs_log(LOG_WARNING, "missing text for locale key '%s'", key);
	}
	return QString::fromUtf8(text);
}

QString T(const char *key, const QString &arg1)
{
	return T(key).arg(arg1);
}

QString T(const char *key, const QString &arg1, const QString &arg2)
{
	return T(key).arg(arg1, arg2);
}
