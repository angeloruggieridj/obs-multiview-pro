// SPDX-License-Identifier: GPL-2.0-or-later
#include "click_target.hpp"

std::string target_for_click(bool studio_mode)
{
	return studio_mode ? "preview" : "program";
}
