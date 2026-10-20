// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "layout_model.hpp"

#include <obs.h>

#include <array>

/* The native multiview safe areas (OBS 32.1 InitSafeAreas / RenderSafeAreas):
 * six line-strip vertex buffers drawn with OBS_EFFECT_SOLID.
 *
 * Every method must run inside the graphics context (the display draw
 * callback, or between obs_enter_graphics/obs_leave_graphics). The buffers
 * exist only between init() and shutdown(); nothing is allocated per frame. */
class SafeAreas {
public:
	SafeAreas() = default;
	SafeAreas(const SafeAreas &) = delete;
	SafeAreas &operator=(const SafeAreas &) = delete;

	/* Creates the six buffers. No-op when they already exist. */
	void init();
	bool ready() const { return m_ready; }

	/* Draws the lines over `target`, a rect in the current coordinate space
	 * (the renderer passes the preview cell content in canvas pixels). */
	void render(RectF target);

	/* Destroys the buffers. Idempotent. */
	void shutdown();

private:
	std::array<gs_vertbuffer_t *, 6> m_lines = {};
	bool m_ready = false;
};
