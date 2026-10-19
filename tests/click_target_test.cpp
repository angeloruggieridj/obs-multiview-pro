// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "click_target.hpp"

TEST_CASE("state_target_for_click_studio_mode_selects_preview")
{
	CHECK(target_for_click(true) == "preview");
}

TEST_CASE("state_target_for_click_normal_mode_selects_program")
{
	CHECK(target_for_click(false) == "program");
}
