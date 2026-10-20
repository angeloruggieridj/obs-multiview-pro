// SPDX-License-Identifier: GPL-2.0-or-later
#include "multiview_renderer.hpp"
#include "obs_state.hpp"

#include <obs.h>
#include <util/platform.h>

#include <QCoreApplication>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <utility>

namespace {

/* Copied verbatim from OBS 32.1 frontend/components/Multiview.hpp (ARGB). */
constexpr uint32_t outerColor = 0xFF999999;
constexpr uint32_t backgroundColor = 0xFF000000;
constexpr uint32_t previewColor = 0xFF00D000;
constexpr uint32_t programColor = 0xFFD00000;
constexpr uint32_t labelColor = 0x33000000;
/* Outline thickness in canvas pixels, as Multiview::thickness. */
constexpr float thickness = kLabelThickness;
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
	/* The canvas size has no frontend event, and labels depend on it (font
	 * size, placement): check it once per second (one struct copy). */
	m_canvas_timer.setInterval(1000);
	connect(&m_canvas_timer, &QTimer::timeout, this, [this] { check_canvas(); });
	m_canvas_timer.start();

	connect(&m_state, &ObsState::scenes_changed, this, [this] { refresh_scenes(); });
	connect(&m_state, &ObsState::program_changed, this, [this] { rebuild_snapshot(); });
	connect(&m_state, &ObsState::preview_changed, this, [this] { rebuild_snapshot(); });
	connect(&m_state, &ObsState::studio_mode_changed, this, [this](bool) { rebuild_snapshot(); });

	/* Audio feeds follow what is in preview (studio mode). Connected after the
	 * snapshot handlers above, so those run first. */
	connect(&m_state, &ObsState::scenes_changed, this, [this] { refresh_audio(); });
	connect(&m_state, &ObsState::preview_changed, this, [this] { refresh_audio(); });
	connect(&m_state, &ObsState::studio_mode_changed, this, [this](bool) { refresh_audio(); });
	/* Items added to / removed from / shown in the preview scene have no
	 * cheap event: look again twice a second, in studio mode with meters on
	 * only (the timer is stopped otherwise). */
	m_audio_timer.setInterval(500);
	connect(&m_audio_timer, &QTimer::timeout, this, [this] { poll_preview_audio(); });

	set_preset(Preset::Scenes8);
	refresh_audio();
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

	build_labels(*snap);
	build_status(*snap);
	build_meter_scale(*snap);
	publish(std::move(snap));
}

void MultiviewRenderer::build_labels(RenderSnapshot &snap)
{
	m_label_hits.clear();
	std::vector<PlacedLabel> placed;

	/* Publishes `placed` (reusing the current batch when nothing changed). */
	auto finish = [&] {
		m_labels.prune();
		if (!(placed == m_placed)) {
			m_batch = placed.empty() ? nullptr : make_batch(placed);
			m_placed = std::move(placed);
		}
		snap.labels = m_batch;
	};

	obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0) {
		m_canvas_cx = m_canvas_cy = 0;
		finish();
		return;
	}
	m_canvas_cx = ovi.base_width;
	m_canvas_cy = ovi.base_height;
	snap.canvas_cx = ovi.base_width;
	snap.canvas_cy = ovi.base_height;
	const float fw = float(ovi.base_width);
	const float fh = float(ovi.base_height);

	/* Same canvas rect as render() computes for the tile. */
	auto canvas_rect = [&](const RectF &r) {
		return RectF{snap_px(r.x * fw), snap_px(r.y * fh), snap_px(r.w * fw), snap_px(r.h * fh)};
	};

	/* All labels are scaled like the PVW/PGM tiles (native ppiScale). */
	LabelScale scale = {0.f, 0.f, 0};
	for (const Tile &t : m_tiles)
		if (t.kind == TileKind::Preview) {
			const RectF r = canvas_rect(t.rect);
			scale = label_scale(fw, fh, r.w, r.h);
			break;
		}
	if (scale.x <= 0.f || scale.y <= 0.f) {
		finish();
		return;
	}
	const float dscale = display_scale(fw, fh);

	for (const Tile &t : m_tiles) {
		QString text;
		bool pvw_pgm = true;
		switch (t.kind) {
		case TileKind::Preview:
			/* Native: Str("StudioMode.Preview"); no locale files yet. */
			text = QCoreApplication::translate("MultiviewRenderer", "Preview");
			break;
		case TileKind::Program:
			text = QCoreApplication::translate("MultiviewRenderer", "Program");
			break;
		case TileKind::Scene: {
			const size_t slot = static_cast<size_t>(t.scene_slot);
			/* Native: labels only on tiles that have a scene. */
			if (slot >= m_slot_names.size() || slot >= m_slot_weak.size() || !m_slot_weak[slot])
				continue;
			text = QString::fromStdString(m_slot_names[slot]);
			pvw_pgm = false;
			break;
		}
		case TileKind::OnAir:
		case TileKind::Rec:
		case TileKind::Clock:
		case TileKind::Mixer:
			continue; /* status tiles draw their own text (build_status) */
		}

		const RectF r = canvas_rect(t.rect);
		const int face = label_face_size(ovi.base_height, pvw_pgm);
		std::shared_ptr<const LabelImage> img =
			m_labels.get(text, face, label_max_cx(r.w, scale), scale, dscale);
		if (!img)
			continue;
		const LabelGeometry g = label_geometry(r, img->size, scale);
		placed.push_back({std::move(img), g.box.x, g.box.y, scale});
		if (t.kind == TileKind::Scene)
			m_label_hits.emplace_back(t.scene_slot, g.box);
	}
	finish();
}

std::shared_ptr<const MultiviewRenderer::LabelBatch>
MultiviewRenderer::make_batch(const std::vector<PlacedLabel> &placed)
{
	/* Shelf packing, 2 transparent texels between labels so bilinear
	 * filtering never picks up a neighbour. */
	constexpr uint32_t kPad = 2;
	constexpr uint32_t kMaxAtlas = 4096;
	uint32_t widest = 0;
	for (const PlacedLabel &p : placed)
		widest = std::max(widest, uint32_t(p.image->tex_w));
	const uint32_t atlas_cx = std::min(kMaxAtlas, std::max(1024u, widest + kPad));

	struct Spot {
		uint32_t x, y;
		bool fits;
	};
	std::vector<Spot> spots;
	spots.reserve(placed.size());
	uint32_t x = 0, y = 0, row_h = 0;
	for (const PlacedLabel &p : placed) {
		const uint32_t w = uint32_t(p.image->tex_w), h = uint32_t(p.image->tex_h);
		if (x + w > atlas_cx) {
			x = 0;
			y += row_h + kPad;
			row_h = 0;
		}
		const bool fits = w <= atlas_cx && y + h <= kMaxAtlas;
		spots.push_back({x, y, fits});
		if (fits) {
			x += w + kPad;
			row_h = std::max(row_h, h);
		}
	}
	const uint32_t atlas_cy = std::max(1u, std::min(kMaxAtlas, y + row_h));

	auto batch = std::make_shared<LabelBatch>();
	batch->id = m_next_batch_id++;
	batch->atlas_cx = atlas_cx;
	batch->atlas_cy = atlas_cy;
	batch->atlas.assign(size_t(atlas_cx) * atlas_cy * 4, 0);

	auto quad = [](std::vector<vec3> &out, float x0, float y0, float x1, float y1) {
		vec3 v[4];
		vec3_set(&v[0], x0, y0, 0.0f);
		vec3_set(&v[1], x1, y0, 0.0f);
		vec3_set(&v[2], x0, y1, 0.0f);
		vec3_set(&v[3], x1, y1, 0.0f);
		out.push_back(v[0]);
		out.push_back(v[1]);
		out.push_back(v[2]);
		out.push_back(v[1]);
		out.push_back(v[3]);
		out.push_back(v[2]);
	};

	for (size_t i = 0; i < placed.size(); ++i) {
		if (!spots[i].fits)
			continue;
		const PlacedLabel &p = placed[i];
		const LabelImage &img = *p.image;
		const size_t row = size_t(img.tex_w) * 4;
		for (int r = 0; r < img.tex_h; ++r)
			std::memcpy(batch->atlas.data() + ((size_t(spots[i].y) + size_t(r)) * atlas_cx + spots[i].x) * 4,
				    img.pixels.data() + row * size_t(r), row);

		/* Native geometry: box (cx, cy + 2*thickness) and text (cx, cy) label
		 * px, scaled by ppiScale, text shifted down by thickness label px. */
		const float w = float(img.size.cx) * p.scale.x;
		quad(batch->box_points, p.x, p.y, p.x + w, p.y + float(img.size.cy + 2 * int(thickness)) * p.scale.y);
		const float ty = p.y + thickness * p.scale.y;
		quad(batch->text_points, p.x, ty, p.x + w, ty + float(img.size.cy) * p.scale.y);

		const float u0 = float(spots[i].x) / float(atlas_cx);
		const float v0 = float(spots[i].y) / float(atlas_cy);
		const float u1 = float(spots[i].x + uint32_t(img.tex_w)) / float(atlas_cx);
		const float v1 = float(spots[i].y + uint32_t(img.tex_h)) / float(atlas_cy);
		vec2 uv[4];
		vec2_set(&uv[0], u0, v0);
		vec2_set(&uv[1], u1, v0);
		vec2_set(&uv[2], u0, v1);
		vec2_set(&uv[3], u1, v1);
		for (int k : {0, 1, 2, 1, 3, 2})
			batch->text_uvs.push_back(uv[k]);
	}
	return batch;
}

/* Display px per canvas px, in 1% steps so tiny size changes do not redraw
 * every label or status tile. */
float MultiviewRenderer::display_scale(float fw, float fh) const
{
	float scale = 1.0f;
	if (m_display_cx > 0 && m_display_cy > 0) {
		const RectF fit = fit_rect(fw / fh, float(m_display_cx), float(m_display_cy));
		scale = std::max(0.01f, std::round(fit.w / fw * 100.f) / 100.f);
	}
	return scale;
}

namespace {
bool is_status_tile(TileKind k)
{
	return k == TileKind::OnAir || k == TileKind::Rec || k == TileKind::Clock || k == TileKind::Mixer;
}
} // namespace

void MultiviewRenderer::set_status(const StatusSnapshot &status)
{
	if (m_shut_down)
		return;
	m_status = status;
	for (const Tile &t : m_tiles)
		if (is_status_tile(t.kind)) {
			rebuild_snapshot();
			return;
		}
}

/* Rasterizes the status tiles that need it (text or size changed) and packs
 * them into the snapshot's StatusBatch (reused as is when nothing changed). */
void MultiviewRenderer::build_status(RenderSnapshot &snap)
{
	std::vector<PlacedStatus> placed;
	std::vector<StatusCacheEntry> cache;

	if (m_canvas_cx > 0 && m_canvas_cy > 0) {
		const float fw = float(m_canvas_cx);
		const float fh = float(m_canvas_cy);
		const float dscale = display_scale(fw, fh);
		for (const Tile &t : m_tiles) {
			if (!is_status_tile(t.kind))
				continue;
			/* Same inner rect as render() paints. */
			const float ix = snap_px(t.rect.x * fw) + thickness;
			const float iy = snap_px(t.rect.y * fh) + thickness;
			const float iw = snap_px(t.rect.w * fw) - thicknessx2;
			const float ih = snap_px(t.rect.h * fh) - thicknessx2;
			if (iw <= 0.0f || ih <= 0.0f)
				continue;
			const int w = std::min(2048, std::max(1, int(std::lround(iw * dscale))));
			const int h = std::min(2048, std::max(1, int(std::lround(ih * dscale))));

			StatusCacheEntry entry;
			for (const StatusCacheEntry &old : m_status_cache)
				if (old.kind == t.kind && old.w == w && old.h == h && old.image &&
				    status_tile_content_equal(t.kind, old.content, m_status)) {
					entry = old;
					break;
				}
			if (!entry.image) {
				entry.kind = t.kind;
				entry.w = w;
				entry.h = h;
				entry.content = m_status;
				entry.image = render_status_tile(t.kind, m_status, w, h);
			}
			if (!entry.image)
				continue;
			placed.push_back({entry.image, {ix, iy, iw, ih}});
			cache.push_back(std::move(entry));
		}
	}
	m_status_cache = std::move(cache);

	if (!(placed == m_status_placed)) {
		m_status_batch = placed.empty() ? nullptr : make_status_batch(placed);
		m_status_placed = std::move(placed);
	}
	snap.status = m_status_batch;
}

std::shared_ptr<const MultiviewRenderer::StatusBatch>
MultiviewRenderer::make_status_batch(const std::vector<PlacedStatus> &placed)
{
	constexpr uint32_t kPad = 2; /* transparent texels between cells */
	uint32_t cell_w = 0, cell_h = 0;
	for (const PlacedStatus &p : placed) {
		cell_w = std::max(cell_w, uint32_t(p.image->w));
		cell_h = std::max(cell_h, uint32_t(p.image->h));
	}
	const uint32_t n = uint32_t(placed.size());
	const uint32_t cols = n > 1 ? 2 : 1;
	const uint32_t rows = (n + cols - 1) / cols;

	auto batch = std::make_shared<StatusBatch>();
	batch->id = m_next_status_id++;
	batch->atlas_cx = cols * cell_w + (cols - 1) * kPad;
	batch->atlas_cy = rows * cell_h + (rows - 1) * kPad;
	batch->atlas.assign(size_t(batch->atlas_cx) * batch->atlas_cy * 4, 0);

	for (uint32_t i = 0; i < n; ++i) {
		const PlacedStatus &p = placed[i];
		const StatusTileImage &img = *p.image;
		const uint32_t ox = (i % cols) * (cell_w + kPad);
		const uint32_t oy = (i / cols) * (cell_h + kPad);
		const size_t row = size_t(img.w) * 4;
		for (int r = 0; r < img.h; ++r)
			std::memcpy(batch->atlas.data() + ((size_t(oy) + size_t(r)) * batch->atlas_cx + ox) * 4,
				    img.pixels.data() + row * size_t(r), row);

		const float x0 = p.rect.x, y0 = p.rect.y, x1 = p.rect.x + p.rect.w, y1 = p.rect.y + p.rect.h;
		vec3 v[4];
		vec3_set(&v[0], x0, y0, 0.0f);
		vec3_set(&v[1], x1, y0, 0.0f);
		vec3_set(&v[2], x0, y1, 0.0f);
		vec3_set(&v[3], x1, y1, 0.0f);
		/* Half a texel inside, so bilinear filtering never reads the padding. */
		const float u0 = (float(ox) + 0.5f) / float(batch->atlas_cx);
		const float v0 = (float(oy) + 0.5f) / float(batch->atlas_cy);
		const float u1 = (float(ox + uint32_t(img.w)) - 0.5f) / float(batch->atlas_cx);
		const float v1 = (float(oy + uint32_t(img.h)) - 0.5f) / float(batch->atlas_cy);
		vec2 uv[4];
		vec2_set(&uv[0], u0, v0);
		vec2_set(&uv[1], u1, v0);
		vec2_set(&uv[2], u0, v1);
		vec2_set(&uv[3], u1, v1);
		for (int k : {0, 1, 2, 1, 3, 2}) {
			batch->points.push_back(v[k]);
			batch->uvs.push_back(uv[k]);
		}
	}
	return batch;
}

bool MultiviewRenderer::upload_status(const StatusBatch &batch)
{
	if (m_gfx_status.id == batch.id)
		return m_gfx_status.atlas && m_gfx_status.quads;

	/* New batch: free the previous one here, in the graphics context. Happens
	 * only when a tile changed (at most once a second), never per frame. */
	destroy_gfx_status();
	m_gfx_status.id = batch.id; /* also on failure: not retried every frame */
	if (batch.atlas.empty() || batch.points.empty())
		return false;

	const uint8_t *data = batch.atlas.data();
	m_gfx_status.atlas = gs_texture_create(batch.atlas_cx, batch.atlas_cy, GS_BGRA, 1, &data, 0);

	gs_tvertarray uvs = {2, const_cast<vec2 *>(batch.uvs.data())};
	gs_vb_data quads = {};
	quads.num = batch.points.size();
	quads.points = const_cast<vec3 *>(batch.points.data());
	quads.num_tex = 1;
	quads.tvarray = &uvs;
	m_gfx_status.quads = gs_vertexbuffer_create(&quads, GS_DUP_BUFFER);
	m_gfx_status.vertices = uint32_t(batch.points.size());
	return m_gfx_status.atlas && m_gfx_status.quads;
}

void MultiviewRenderer::destroy_gfx_status()
{
	gs_texture_destroy(m_gfx_status.atlas); /* all null-safe */
	gs_vertexbuffer_destroy(m_gfx_status.quads);
	m_gfx_status = GfxStatus();
}

void MultiviewRenderer::set_audio_meters(bool on)
{
	if (m_shut_down || on == m_meters_on.load(std::memory_order_relaxed))
		return;
	m_meters_on.store(on, std::memory_order_relaxed);
	refresh_audio(); /* attaches or detaches every feed */
	rebuild_snapshot(); /* adds or drops the scale texture */
}

/* The program feed is the raw program mix and does not depend on the scene:
 * attached once while the meters are on. The preview feed is one volmeter per
 * audio source of the preview scene, in studio mode only (outside it the
 * Preview tile shows the program, so its meter mirrors the program levels). */
void MultiviewRenderer::refresh_audio()
{
	if (m_shut_down)
		return;
	if (!m_meters_on.load(std::memory_order_relaxed)) {
		m_audio_timer.stop();
		m_pvw_audio.detach();
		m_pgm_audio.detach();
		return;
	}
	if (!m_pgm_audio.attached())
		m_pgm_audio.attach_program_mix();

	if (!m_state.studio_mode()) {
		m_audio_timer.stop();
		m_pvw_audio.detach();
		return;
	}
	if (!m_audio_timer.isActive())
		m_audio_timer.start();
	poll_preview_audio();
}

void MultiviewRenderer::poll_preview_audio()
{
	if (m_shut_down || !m_meters_on.load(std::memory_order_relaxed) || !m_state.studio_mode())
		return;

	std::vector<obs_source_t *> sources;
	const std::string name = m_state.preview_scene();
	if (!name.empty()) {
		obs_source_t *scene = obs_get_source_by_name(name.c_str());
		if (scene) {
			if (!obs_source_removed(scene))
				sources = collect_scene_audio_sources(scene);
			obs_source_release(scene);
		}
	}

	if (m_pvw_audio.has_sources(sources)) {
		/* Unchanged: drop the extra references. */
		for (obs_source_t *s : sources)
			obs_source_release(s);
		m_pvw_audio.update_channel_layout();
		return;
	}
	m_pvw_audio.attach_sources(std::move(sources));
}

/* The dB numbers of the Program meter, rasterized when their size changes. */
void MultiviewRenderer::build_meter_scale(RenderSnapshot &snap)
{
	snap.meter_scale = nullptr;
	if (!m_meters_on.load(std::memory_order_relaxed) || m_canvas_cx == 0 || m_canvas_cy == 0) {
		m_scale_batch = nullptr;
		m_scale_key.clear();
		return;
	}
	const float fw = float(m_canvas_cx);
	const float fh = float(m_canvas_cy);
	for (const Tile &t : m_tiles) {
		if (t.kind != TileKind::Program)
			continue;
		/* Same inner rect as render() paints. */
		const RectF inner = {snap_px(t.rect.x * fw) + thickness, snap_px(t.rect.y * fh) + thickness,
				     snap_px(t.rect.w * fw) - thicknessx2, snap_px(t.rect.h * fh) - thicknessx2};
		if (inner.w <= 0.f || inner.h <= 0.f)
			break;
		const MeterGeometry g = meter_geometry(inner, true);
		const float dscale = display_scale(fw, fh);
		const int w = std::min(2048, std::max(1, int(std::lround(g.column.w * dscale))));
		const int h = std::min(2048, std::max(1, int(std::lround(g.column.h * dscale))));
		const std::vector<int> key = {w, h, int(std::lround(g.column.w * 16.f)),
					      int(std::lround(g.column.h * 16.f)), int(std::lround(g.bar[0].h * 16.f))};
		if (!m_scale_batch || key != m_scale_key) {
			auto image = render_meter_scale(g, w, h);
			if (!image) {
				m_scale_batch = nullptr;
			} else {
				auto batch = std::make_shared<MeterScaleBatch>();
				batch->id = m_next_scale_id++;
				batch->image = std::move(image);
				batch->rect = g.column;
				m_scale_batch = std::move(batch);
			}
			m_scale_key = key;
		}
		snap.meter_scale = m_scale_batch;
		return;
	}
	m_scale_batch = nullptr;
	m_scale_key.clear();
}

bool MultiviewRenderer::upload_scale(const MeterScaleBatch &batch)
{
	if (m_gfx_scale.id == batch.id)
		return m_gfx_scale.tex != nullptr;
	destroy_gfx_scale(); /* graphics context; only when the scale changed */
	m_gfx_scale.id = batch.id; /* also on failure: not retried every frame */
	if (!batch.image || batch.image->pixels.empty())
		return false;
	const uint8_t *data = batch.image->pixels.data();
	m_gfx_scale.tex = gs_texture_create(uint32_t(batch.image->w), uint32_t(batch.image->h), GS_BGRA, 1, &data, 0);
	return m_gfx_scale.tex != nullptr;
}

void MultiviewRenderer::destroy_gfx_scale()
{
	gs_texture_destroy(m_gfx_scale.tex); /* null-safe */
	m_gfx_scale = GfxScale();
}

void MultiviewRenderer::check_canvas()
{
	if (m_shut_down)
		return;
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;
	if (ovi.base_width != m_canvas_cx || ovi.base_height != m_canvas_cy)
		rebuild_snapshot();
}

void MultiviewRenderer::set_display_size(int cx, int cy)
{
	if (m_shut_down || cx <= 0 || cy <= 0 || (cx == m_display_cx && cy == m_display_cy))
		return;
	m_display_cx = cx;
	m_display_cy = cy;
	/* Cached labels whose raster size is unchanged are reused as they are. */
	rebuild_snapshot();
}

int MultiviewRenderer::label_slot_at(float canvas_x, float canvas_y) const
{
	for (const auto &hit : m_label_hits) {
		const RectF &b = hit.second;
		if (canvas_x >= b.x && canvas_x < b.x + b.w && canvas_y >= b.y && canvas_y < b.y + b.h)
			return hit.first;
	}
	return -1;
}

bool MultiviewRenderer::upload_labels(const LabelBatch &batch)
{
	if (m_gfx.id == batch.id)
		return m_gfx.atlas && m_gfx.boxes && m_gfx.texts;

	/* A new batch replaces the previous one: free it here, in the graphics
	 * context. Happens only when the labels changed, never per frame. */
	destroy_gfx_labels();
	m_gfx.id = batch.id; /* also on failure: not retried every frame */
	if (batch.atlas.empty() || batch.box_points.empty() || batch.text_points.empty())
		return false;

	const uint8_t *data = batch.atlas.data();
	m_gfx.atlas = gs_texture_create(batch.atlas_cx, batch.atlas_cy, GS_BGRA, 1, &data, 0);

	/* Static buffers; GS_DUP_BUFFER copies the data, the batch keeps its own. */
	gs_vb_data boxes = {};
	boxes.num = batch.box_points.size();
	boxes.points = const_cast<vec3 *>(batch.box_points.data());
	m_gfx.boxes = gs_vertexbuffer_create(&boxes, GS_DUP_BUFFER);

	gs_tvertarray uvs = {2, const_cast<vec2 *>(batch.text_uvs.data())};
	gs_vb_data texts = {};
	texts.num = batch.text_points.size();
	texts.points = const_cast<vec3 *>(batch.text_points.data());
	texts.num_tex = 1;
	texts.tvarray = &uvs;
	m_gfx.texts = gs_vertexbuffer_create(&texts, GS_DUP_BUFFER);

	m_gfx.box_vertices = uint32_t(batch.box_points.size());
	m_gfx.text_vertices = uint32_t(batch.text_points.size());
	return m_gfx.atlas && m_gfx.boxes && m_gfx.texts;
}

void MultiviewRenderer::destroy_gfx_labels()
{
	gs_texture_destroy(m_gfx.atlas); /* all null-safe */
	gs_vertexbuffer_destroy(m_gfx.boxes);
	gs_vertexbuffer_destroy(m_gfx.texts);
	m_gfx = GfxLabels();
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

	m_canvas_timer.stop();
	/* Every OBS audio callback goes first: after these return none is running
	 * and none can start, so nothing touches this object from an audio thread. */
	m_audio_timer.stop();
	m_pvw_audio.detach();
	m_pgm_audio.detach();
	m_scale_batch.reset();
	publish(nullptr);
	release_showing();
	m_label_hits.clear();

	m_placed.clear();
	m_batch.reset();
	m_status_cache.clear();
	m_status_placed.clear();
	m_status_batch.reset();

	/* The draw callback is gone (class contract), so nothing else touches
	 * m_gfx or m_safe now. gs_* objects are freed inside the graphics context. */
	if (m_gfx.atlas || m_gfx.boxes || m_gfx.texts || m_gfx_status.atlas || m_gfx_status.quads || m_gfx_scale.tex ||
	    m_safe.ready()) {
		obs_enter_graphics();
		destroy_gfx_labels();
		destroy_gfx_status();
		destroy_gfx_scale();
		m_safe.shutdown();
		obs_leave_graphics();
	}
	m_gfx = GfxLabels();
	m_gfx_status = GfxStatus();
	m_gfx_scale = GfxScale();
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

	gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(def, "image");

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

	/* One atomic load per frame. Buffers are made on the first frame that
	 * wants them and freed on the first that does not (graphics thread). */
	const bool want_safe = m_safe_areas.load(std::memory_order_relaxed);
	if (want_safe)
		m_safe.init();
	else if (m_safe.ready())
		m_safe.shutdown();

	/* Labels laid out for another canvas size are skipped until the UI
	 * thread has rebuilt them (check_canvas). */
	const bool labels_ok = snap->canvas_cx == targetCX && snap->canvas_cy == targetCY;

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
			/* Content and safe areas share one region, like the native. */
			beginContent(ix, iy, iw, ih);
			if (snap->studio_mode) {
				obs_source_t *src = obs_weak_source_get_source(snap->preview);
				if (src) {
					obs_source_video_render(src);
					obs_source_release(src);
				}
			} else {
				obs_render_main_texture();
			}
			if (want_safe)
				m_safe.render({0.0f, 0.0f, fw, fh});
			endContent();
			break;
		}
		case TileKind::Program:
			paintAreaWithColor(ix, iy, iw, ih, backgroundColor);
			beginContent(ix, iy, iw, ih);
			obs_render_main_texture();
			endContent();
			break;
		case TileKind::OnAir:
		case TileKind::Rec:
		case TileKind::Clock:
		case TileKind::Mixer:
			/* Black until the status batch below covers it. */
			paintAreaWithColor(ix, iy, iw, ih, backgroundColor);
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

	draw_meters(*snap, fw, fh, labels_ok);

	/* Labels last (each sits inside its own tile, so the order does not change
	 * the picture), all in two draws: the labelColor boxes, then the text from
	 * the atlas drawn like the native text source's Render (default effect,
	 * sRGB framebuffer). Geometry is in canvas coordinates, like the boxes. */
	if (!snap->labels) {
		if (m_gfx.id)
			destroy_gfx_labels();
	} else if (labels_ok && upload_labels(*snap->labels)) {
		gs_load_indexbuffer(nullptr);

		gs_effect_set_color(color, labelColor);
		gs_load_vertexbuffer(m_gfx.boxes);
		while (gs_effect_loop(solid, "Solid"))
			gs_draw(GS_TRIS, 0, m_gfx.box_vertices);

		const bool previous = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(true);
		gs_effect_set_texture_srgb(image, m_gfx.atlas);
		gs_load_vertexbuffer(m_gfx.texts);
		while (gs_effect_loop(def, "Draw"))
			gs_draw(GS_TRIS, 0, m_gfx.text_vertices);
		gs_enable_framebuffer_srgb(previous);

		gs_load_vertexbuffer(nullptr);
	}

	/* Status tiles: one textured draw for all of them, the same path as the
	 * label text (sRGB texture and framebuffer: opaque pixels round-trip). */
	if (!snap->status) {
		if (m_gfx_status.id)
			destroy_gfx_status();
	} else if (labels_ok && upload_status(*snap->status)) {
		gs_load_indexbuffer(nullptr);
		const bool previous = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(true);
		gs_effect_set_texture_srgb(image, m_gfx_status.atlas);
		gs_load_vertexbuffer(m_gfx_status.quads);
		while (gs_effect_loop(def, "Draw"))
			gs_draw(GS_TRIS, 0, m_gfx_status.vertices);
		gs_enable_framebuffer_srgb(previous);
		gs_load_vertexbuffer(nullptr);
	}

	end_region();
}

/* Graphics thread, inside render()'s whole-canvas region (canvas coordinates).
 * Reads each AudioMeter once, steps the ballistics, then draws per bar: the
 * dark bands of the empty scale, the bright bands up to the level and the
 * peak-hold tick (OBS_EFFECT_SOLID, the colour set before every draw because
 * libobs resets effect parameters after each loop), and finally the dB text
 * as one textured quad. No allocation, no Qt, no logging. */
void MultiviewRenderer::draw_meters(const RenderSnapshot &snap, float fw, float fh, bool labels_ok)
{
	if (!m_meters_on.load(std::memory_order_relaxed)) {
		if (m_gfx_scale.id)
			destroy_gfx_scale();
		m_meter = MeterDisplay();
		return;
	}

	/* Frame time; the first frame after switching on has no history. */
	const uint64_t now = os_gettime_ns();
	const float dt = m_meter.last_ns ? float(double(now - m_meter.last_ns) * 1e-9) : 0.0f;
	m_meter.last_ns = now;

	const std::array<float, 2> pgm = m_pgm_audio.take_peaks();
	/* Outside studio mode the Preview tile shows the program (as its video). */
	const std::array<float, 2> pvw = snap.studio_mode ? m_pvw_audio.take_peaks() : pgm;
	for (int c = 0; c < 2; ++c) {
		ballistics_step(m_meter.program[c], pgm[size_t(c)], dt);
		ballistics_step(m_meter.preview[c], pvw[size_t(c)], dt);
	}

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");

	auto fill = [&](float x, float y, float w, float h, uint32_t argb) {
		const uint32_t pw = uint32_t(std::lround(w));
		const uint32_t ph = uint32_t(std::lround(h));
		if (pw == 0 || ph == 0)
			return;
		gs_matrix_push();
		gs_matrix_translate3f(std::round(x), std::round(y), 0.0f);
		gs_effect_set_color(color, argb);
		while (gs_effect_loop(solid, "Solid"))
			gs_draw_sprite(nullptr, 0, pw, ph);
		gs_matrix_pop();
	};

	/* Draws a run of a bar between two fractions of its height. */
	auto run = [&](const RectF &bar, const MeterSegment &s, bool fg) {
		const float top = bar.y + bar.h * (1.0f - s.f1);
		fill(bar.x, top, bar.w, bar.h * (s.f1 - s.f0), meter_color(s.zone, fg));
	};

	auto draw_meter = [&](const MeterGeometry &g, const ChannelBallistics (&st)[2]) {
		fill(g.column.x, g.column.y, g.column.w, g.column.h, 0x99000000);
		MeterSegment seg[3];
		for (int c = 0; c < 2; ++c) {
			const RectF &bar = g.bar[c];
			int n = meter_segments(1.0f, seg);
			for (int i = 0; i < n; ++i)
				run(bar, seg[i], false);
			n = meter_segments(db_to_fraction(st[c].peak_db), seg);
			for (int i = 0; i < n; ++i)
				run(bar, seg[i], true);
			if (st[c].hold_db > kMeterMinDb) {
				const float th = std::max(1.0f, bar.h * 0.005f);
				const float y = std::min(meter_y_for_db(g, st[c].hold_db), bar.y + bar.h - th);
				fill(bar.x, y, bar.w, th, peak_hold_color(st[c].hold_db));
			}
		}
	};

	bool scale_drawn = false;
	for (const Tile &t : snap.tiles) {
		if (t.kind != TileKind::Program && t.kind != TileKind::Preview)
			continue;
		const RectF inner = {snap_px(t.rect.x * fw) + thickness, snap_px(t.rect.y * fh) + thickness,
				     snap_px(t.rect.w * fw) - thicknessx2, snap_px(t.rect.h * fh) - thicknessx2};
		if (inner.w <= 0.0f || inner.h <= 0.0f)
			continue;
		if (t.kind == TileKind::Program) {
			draw_meter(meter_geometry(inner, true), m_meter.program);
			scale_drawn = true;
		} else {
			draw_meter(meter_geometry(inner, false), m_meter.preview);
		}
	}

	/* dB numbers and L / R captions: one textured quad, the same path as the
	 * label text. */
	const MeterScaleBatch *sb = snap.meter_scale.get();
	if (!sb || !scale_drawn || !labels_ok || !upload_scale(*sb)) {
		if (!sb && m_gfx_scale.id)
			destroy_gfx_scale();
		return;
	}
	gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(def, "image");
	const uint32_t tw = uint32_t(sb->image->w);
	const uint32_t th = uint32_t(sb->image->h);
	gs_matrix_push();
	gs_matrix_translate3f(sb->rect.x, sb->rect.y, 0.0f);
	gs_matrix_scale3f(sb->rect.w / float(tw), sb->rect.h / float(th), 1.0f);
	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_effect_set_texture_srgb(image, m_gfx_scale.tex);
	while (gs_effect_loop(def, "Draw"))
		gs_draw_sprite(m_gfx_scale.tex, 0, tw, th);
	gs_enable_framebuffer_srgb(previous);
	gs_matrix_pop();
}
