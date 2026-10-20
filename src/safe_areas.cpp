// SPDX-License-Identifier: GPL-2.0-or-later
#include "safe_areas.hpp"
#include "safe_area_geometry.hpp"

namespace {
/* Native OUTLINE_COLOR (display-helpers.hpp). */
constexpr uint32_t kOutlineColor = 0xFFD0D0D0;
} // namespace

void SafeAreas::init()
{
	if (m_ready)
		return;

	const SafeLines lines = safe_area_lines();
	for (size_t i = 0; i < lines.size(); ++i) {
		gs_render_start(true);
		for (const SafePoint &p : lines[i])
			gs_vertex2f(p.x, p.y);
		m_lines[i] = gs_render_save();
	}
	m_ready = true;
}

void SafeAreas::render(RectF target)
{
	if (!m_ready)
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");

	gs_matrix_push();
	gs_matrix_translate3f(target.x, target.y, 0.0f);
	gs_matrix_scale3f(target.w, target.h, 1.0f);
	for (gs_vertbuffer_t *vb : m_lines) {
		if (!vb)
			continue;
		gs_load_vertexbuffer(vb);
		/* libobs resets effect parameters after each effect loop: set per line, as native. */
		gs_effect_set_color(color, kOutlineColor);
		while (gs_effect_loop(solid, "Solid"))
			gs_draw(GS_LINESTRIP, 0, 0);
	}
	gs_load_vertexbuffer(nullptr);
	gs_matrix_pop();
}

void SafeAreas::shutdown()
{
	for (gs_vertbuffer_t *&vb : m_lines) {
		gs_vertexbuffer_destroy(vb); /* null-safe */
		vb = nullptr;
	}
	m_ready = false;
}
