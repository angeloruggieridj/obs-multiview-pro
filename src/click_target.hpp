// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

/* Which output a click on a scene tile selects: "preview" in studio mode,
 * "program" otherwise. Pure function, no OBS/Qt dependency. */
std::string target_for_click(bool studio_mode);
