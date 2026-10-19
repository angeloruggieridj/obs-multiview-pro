// SPDX-License-Identifier: GPL-2.0-or-later
#include "multiview_renderer.hpp"
#include "obs_state.hpp"

#include <obs.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

/* Copied verbatim from OBS 32.1 frontend/components/Multiview.hpp (ARGB). */
constexpr uint32_t outerColor = 0xFF999999;
constexpr uint32_t backgroundColor = 0xFF000000;
constexpr uint32_t previewColor = 0xFF00D000;
constexpr uint32_t programColor = 0xFFD00000;
/* Outline thickness in canvas pixels, as Multiview::thickness. */
constexpr float thickness = 6.0f;
constexpr float thicknessx2 = thickness * 2.0f;

/* Same as the native startRegion/endRegion (Multiview.hpp). */
inline void start_region(int vX, int vY, int vCX, int vCY, float oL, float oR, float oT, float oB)
{
	gs_projection_push();
	gs_viewport_push();
	gs_set_viewport(vX, vY, vCX, vCY);
	gs_ortho(oL, oR, oT, oB, -100.0f, 100.0f);
}

inline void end_region()
{
	gs_viewport_pop();
	gs_projection_pop();
}

/* Same as the native GetScaleAndCenterPos (frontend/utility/display-helpers.hpp). */
inline void scale_and_center(int baseCX, int baseCY, int windowCX, int windowCY, int &x, int &y, float &scale)
{
	const double windowAspect = double(windowCX) / double(windowCY);
	const double baseAspect = double(baseCX) / double(baseCY);
	int newCX, newCY;

	if (windowAspect > baseAspect) {
		scale = float(windowCY) / float(baseCY);
		newCX = int(double(windowCY) * baseAspect);
		newCY = windowCY;
	} else {
		scale = float(windowCX) / float(baseCX);
		newCX = windowCX;
		newCY = int(float(windowCX) / baseAspect);
	}

	x = windowCX / 2 - newCX / 2;
	y = windowCY / 2 - newCY / 2;
}

/* Normalized layout coordinates are fractions such as 1/6 stored as float;
 * snap the canvas-pixel result so e.g. 5 * (1/6) * 1920 is exactly 1600 like
 * the native integer-ish arithmetic, not 1599.9999. */
inline float snap_px(float v)
{
	return std::nearbyint(v * 256.0f) / 256.0f;
}

int scene_slot_count(const std::vector<Tile> &tiles)
{
	int n = 0;
	for (const Tile &t : tiles)
		if (t.kind == TileKind::Scene)
			n = std::max(n, t.scene_slot + 1);
	return n;
}

obs_weak_source_t *weak_by_name(const std::string &name)
{
	if (name.empty())
		return nullptr;
	obs_source_t *src = obs_get_source_by_name(name.c_str());
	if (!src)
		return nullptr;
	obs_weak_source_t *weak = obs_source_removed(src) ? nullptr : obs_source_get_weak_source(src);
	obs_source_release(src);
	return weak;
}

} // namespace

MultiviewRenderer::RenderSnapshot::~RenderSnapshot()
{
	for (SlotEntry &s : slot_entries)
		obs_weak_source_release(s.weak);
	obs_weak_source_release(preview);
}

MultiviewRenderer::MultiviewRenderer(ObsState &state) : m_state(state)
{
	connect(&m_state, &ObsState::scenes_changed, this, [this] { refresh_scenes(); });
	connect(&m_state, &ObsState::program_changed, this, [this] { rebuild_snapshot(); });
	connect(&m_state, &ObsState::preview_changed, this, [this] { rebuild_snapshot(); });
	connect(&m_state, &ObsState::studio_mode_changed, this, [this](bool) { rebuild_snapshot(); });

	set_preset(Preset::Scenes8);
}

MultiviewRenderer::~MultiviewRenderer()
{
	shutdown();
}

void MultiviewRenderer::set_preset(Preset preset)
{
	if (m_shut_down)
		return;
	m_preset = preset;
	m_tiles = build_layout(preset);
	m_fillers = uncovered_areas(m_tiles);
	refresh_scenes();
}

void MultiviewRenderer::set_scene_for_slot(SceneForSlot scene_for_slot)
{
	if (m_shut_down)
		return;
	m_scene_for_slot = std::move(scene_for_slot);
	refresh_scenes();
}

void MultiviewRenderer::refresh_scenes()
{
	if (m_shut_down)
		return;

	const int count = scene_slot_count(m_tiles);
	std::vector<std::string> names(static_cast<size_t>(count));

	if (m_scene_for_slot) {
		for (int i = 0; i < count; ++i)
			names[static_cast<size_t>(i)] = m_scene_for_slot(i);
	} else {
		std::vector<SceneRef> scenes = m_state.scenes();
		const size_t n = std::min(scenes.size(), names.size());
		for (size_t i = 0; i < n; ++i)
			names[i] = std::move(scenes[i].name);
	}

	/* Like Multiview::Update: show the new set first, then hide the old one,
	 * so a scene present in both never drops to zero showing. */
	std::vector<obs_weak_source_t *> weak(names.size(), nullptr);
	for (size_t i = 0; i < names.size(); ++i) {
		if (names[i].empty())
			continue;
		obs_source_t *src = obs_get_source_by_name(names[i].c_str());
		if (!src)
			continue;
		if (obs_source_removed(src)) {
			/* Being deleted (the scene list refresh can run before
			 * the last reference is gone): treat as missing. */
			obs_source_release(src);
			continue;
		}
		weak[i] = obs_source_get_weak_source(src);
		obs_source_inc_showing(src);
		obs_source_release(src);
	}

	release_showing();
	m_slot_names = std::move(names);
	m_slot_weak = std::move(weak);

	rebuild_snapshot();
}

void MultiviewRenderer::rebuild_snapshot()
{
	if (m_shut_down)
		return;

	auto snap = std::make_shared<RenderSnapshot>();
	snap->tiles = m_tiles;
	snap->fillers = m_fillers;
	snap->studio_mode = m_state.studio_mode();

	const std::string program = m_state.program_scene();
	const std::string preview = snap->studio_mode ? m_state.preview_scene() : std::string();

	snap->slot_entries.resize(m_slot_weak.size());
	for (size_t i = 0; i < m_slot_weak.size(); ++i) {
		if (!m_slot_weak[i])
			continue;
		obs_weak_source_addref(m_slot_weak[i]);
		SlotEntry &e = snap->slot_entries[i];
		e.weak = m_slot_weak[i];
		/* Native: program -> red; else preview -> green in studio mode
		 * (outside studio mode the "preview" is the program, hence red). */
		const std::string &name = m_slot_names[i];
		if (name == program)
			e.border = programColor;
		else if (snap->studio_mode && name == preview)
			e.border = previewColor;
		else
			e.border = outerColor;
	}

	if (snap->studio_mode)
		snap->preview = weak_by_name(preview);

	publish(std::move(snap));
}

void MultiviewRenderer::publish(std::shared_ptr<const RenderSnapshot> snapshot)
{
	std::shared_ptr<const RenderSnapshot> old;
	{
		std::lock_guard<std::mutex> lock(m_snapshot_mutex);
		old = std::move(m_snapshot);
		m_snapshot = std::move(snapshot);
	}
	/* `old` is released here, outside the lock. If the graphics thread still
	 * holds it, the last release (and the obs_weak_source_release calls in its
	 * destructor) happens there at the end of that frame. */
}

void MultiviewRenderer::release_showing()
{
	for (obs_weak_source_t *weak : m_slot_weak) {
		if (!weak)
			continue;
		obs_source_t *src = obs_weak_source_get_source(weak);
		if (src) {
			obs_source_dec_showing(src);
			obs_source_release(src);
		}
		obs_weak_source_release(weak);
	}
	m_slot_weak.clear();
	m_slot_names.clear();
}

void MultiviewRenderer::shutdown()
{
	if (m_shut_down)
		return;
	m_shut_down = true;
	disconnect(&m_state, nullptr, this, nullptr);

	publish(nullptr);
	release_showing();
	/* No gs_* objects are owned yet (outlines and boxes use the base solid
	 * effect with gs_draw_sprite), so there is nothing to free inside
	 * obs_enter_graphics()/obs_leave_graphics(). Later resources (e.g. safe
	 * area vertex buffers) must be destroyed here, inside that pair. */
}

void MultiviewRenderer::render(uint32_t cx, uint32_t cy)
{
	std::shared_ptr<const RenderSnapshot> snap;
	{
		std::lock_guard<std::mutex> lock(m_snapshot_mutex);
		snap = m_snapshot;
	}
	if (!snap || cx == 0 || cy == 0)
		return;

	obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0)
		return;

	const float fw = float(ovi.base_width);
	const float fh = float(ovi.base_height);
	const uint32_t targetCX = ovi.base_width;
	const uint32_t targetCY = ovi.base_height;

	int x, y;
	float scale;
	scale_and_center(int(targetCX), int(targetCY), int(cx), int(cy), x, y, scale);

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");

	auto drawBox = [&](float bcx, float bcy, uint32_t colorVal) {
		gs_effect_set_color(color, colorVal);
		while (gs_effect_loop(solid, "Solid"))
			gs_draw_sprite(nullptr, 0, (uint32_t)bcx, (uint32_t)bcy);
	};

	auto paintAreaWithColor = [&](float tx, float ty, float bcx, float bcy, uint32_t colorVal) {
		gs_matrix_push();
		gs_matrix_translate3f(tx, ty, 0.0f);
		drawBox(bcx, bcy, colorVal);
		gs_matrix_pop();
	};

	auto setRegion = [&](float bx, float by, float bcx, float bcy) {
		/* Same truncation as the native setRegion. */
		start_region(int(float(x) + bx * scale), int(float(y) + by * scale), int(bcx * scale),
			     int(bcy * scale), bx, bx + bcx, by, by + bcy);
	};

	/* Content (scene or main texture) scaled into the inner rect, clipped by
	 * the viewport exactly like the native multiview. */
	auto beginContent = [&](float ix, float iy, float iw, float ih) {
		gs_matrix_push();
		gs_matrix_translate3f(ix, iy, 0.0f);
		gs_matrix_scale3f(iw / fw, ih / fh, 1.0f);
		setRegion(ix, iy, iw, ih);
	};
	auto endContent = [&] {
		end_region();
		gs_matrix_pop();
	};

	/* Whole usable region, background in the outline grey. */
	start_region(x, y, int(float(targetCX) * scale), int(float(targetCY) * scale), 0.0f, fw, 0.0f, fh);
	drawBox(fw, fh, outerColor);

	for (const Tile &t : snap->tiles) {
		const float sx = snap_px(t.rect.x * fw);
		const float sy = snap_px(t.rect.y * fh);
		const float scx = snap_px(t.rect.w * fw);
		const float scy = snap_px(t.rect.h * fh);
		const float ix = sx + thickness;
		const float iy = sy + thickness;
		const float iw = scx - thicknessx2;
		const float ih = scy - thicknessx2;
		if (iw <= 0.0f || ih <= 0.0f)
			continue;

		switch (t.kind) {
		case TileKind::Scene: {
			const size_t slot = static_cast<size_t>(t.scene_slot);
			const SlotEntry *e = slot < snap->slot_entries.size() ? &snap->slot_entries[slot] : nullptr;
			obs_source_t *src = (e && e->weak) ? obs_weak_source_get_source(e->weak) : nullptr;
			if (src && obs_source_removed(src)) {
				obs_source_release(src);
				src = nullptr;
			}

			/* The canvas is already outerColor: only paint highlights. */
			if (src && e->border != outerColor)
				paintAreaWithColor(sx, sy, scx, scy, e->border);
			paintAreaWithColor(ix, iy, iw, ih, backgroundColor);

			if (src) {
				beginContent(ix, iy, iw, ih);
				obs_source_video_render(src);
				endContent();
				obs_source_release(src);
			}
			break;
		}
		case TileKind::Preview: {
			paintAreaWithColor(ix, iy, iw, ih, backgroundColor);
			if (snap->studio_mode) {
				obs_source_t *src = obs_weak_source_get_source(snap->preview);
				if (src) {
					beginContent(ix, iy, iw, ih);
					obs_source_video_render(src);
					endContent();
					obs_source_release(src);
				}
			} else {
				beginContent(ix, iy, iw, ih);
				obs_render_main_texture();
				endContent();
			}
			break;
		}
		case TileKind::Program:
			paintAreaWithColor(ix, iy, iw, ih, backgroundColor);
			beginContent(ix, iy, iw, ih);
			obs_render_main_texture();
			endContent();
			break;
		}
	}

	for (const RectF &r : snap->fillers) {
		const float w = snap_px(r.w * fw) - thicknessx2;
		const float h = snap_px(r.h * fh) - thicknessx2;
		if (w > 0.0f && h > 0.0f)
			paintAreaWithColor(snap_px(r.x * fw) + thickness, snap_px(r.y * fh) + thickness, w, h,
					   backgroundColor);
	}

	end_region();
}
