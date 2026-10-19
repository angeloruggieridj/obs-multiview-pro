// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

/* A scene is referenced by name; `id` (the source UUID, may be empty) lets a
 * rename be told apart from a delete + add. Dependency-free on purpose: used by
 * the pure modules and their unit tests. */
struct SceneRef {
	std::string name;
	std::string id;
	/* false when the scene's private "show_in_multiview" setting is off. */
	bool visible_in_multiview = true;
};
