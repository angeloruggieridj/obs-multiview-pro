// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "label_cache.hpp"
#include "label_metrics.hpp"
#include "layout_model.hpp"
#include "safe_areas.hpp"
#include "status_format.hpp"
#include "status_tiles.hpp"

#include <obs.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>

#include <QObject>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class ObsState;

/* Draws the multiview tiles into the current obs_display, like the native
 * Multiview::Render (OBS 32.1 frontend/components/Multiview.cpp).
 *
 * Threading:
 *  - Construction, set_preset, set_scene_for_slot, shutdown and destruction
 *    happen on the Qt UI thread (ObsState signals are delivered there too).
 *  - render() runs on the OBS graphics thread (display draw callback).
 *  - The two sides share only an immutable RenderSnapshot, rebuilt on the UI
 *    thread when something changes and handed over through a shared_ptr swap
 *    guarded by a mutex that is held just for the pointer copy. No OBS call is
 *    ever made while holding that mutex.
 *  - The owner must remove the draw callback (which blocks until a running
 *    callback has returned) before calling shutdown() or destroying this.
 *
 * Labels (look and placement of the native, OBS 32.1 Multiview CreateLabel /
 * Render):
 *  - Each text is drawn with Qt on the UI thread (LabelCache), only when the
 *    text, the size or the raster scale changes.
 *  - The labels of a snapshot are packed into one immutable LabelBatch: an
 *    atlas image plus the box and text quads in canvas coordinates. The batch
 *    is reused as is while no label changes (e.g. a program change).
 *  - Ownership of the GPU side: the graphics thread uploads a batch the first
 *    time it draws it (one texture + two static vertex buffers, m_gfx) and
 *    destroys the previous batch's objects at that moment, inside the
 *    graphics context. So at most one set exists, and only render() touches
 *    it; shutdown() destroys it inside obs_enter_graphics/obs_leave_graphics
 *    after the draw callback is gone. Snapshots and batches hold only CPU
 *    memory, so dropping them on any thread never calls gs_*.
 *  - Per frame: two draw calls for all labels, no allocation, no upload.
 *
 * Status tiles (ON AIR, REC, Clock, Mixer of the "con stato" presets): each is
 * rasterized with Qt on the UI thread (render_status_tile) at the tile's size
 * in display pixels, only when its text or size changes (at most once a
 * second, driven by StatusMonitor), and packed into one immutable StatusBatch
 * (2x2 atlas plus the quads). It follows the same ownership rules as the label
 * batch: the graphics thread uploads it once per new batch (m_gfx_status),
 * shutdown() frees it in the graphics context. Per frame: one draw call, no
 * allocation, no upload, no Qt. */
class MultiviewRenderer : public QObject {
public:
	using SceneForSlot = std::function<std::string(int)>;

	explicit MultiviewRenderer(ObsState &state);
	~MultiviewRenderer() override;

	MultiviewRenderer(const MultiviewRenderer &) = delete;
	MultiviewRenderer &operator=(const MultiviewRenderer &) = delete;

	void set_preset(Preset preset);
	Preset preset() const { return m_preset; }

	/* slot index -> scene name ("" = empty tile). Default: the i-th scene of
	 * ObsState::scenes(). Passing an empty function restores the default. */
	void set_scene_for_slot(SceneForSlot scene_for_slot);

	/* UI thread. Re-resolves the scene of every slot (the SceneForSlot
	 * function is called here, on the UI thread only, never from render())
	 * and publishes a new snapshot. Call after the mapping changed. */
	void refresh_scenes();

	/* UI thread. Size of the display in device pixels: labels are rasterized
	 * for it (crisp at any window size / DPI). Re-rasterizes only the labels
	 * whose raster size changes. */
	void set_display_size(int cx, int cy);

	/* UI thread. Scene slot whose label box contains the canvas point, or -1. */
	int label_slot_at(float canvas_x, float canvas_y) const;

	/* UI thread. Shows or hides the native safe areas over the preview tile
	 * (default on, as the native MultiviewDrawAreas). Handed to the graphics
	 * thread through an atomic read once per frame; the graphics thread
	 * creates the buffers on the first frame that wants them and destroys
	 * them on the first frame that does not. */
	void set_safe_areas(bool on) { m_safe_areas.store(on, std::memory_order_relaxed); }
	bool safe_areas() const { return m_safe_areas.load(std::memory_order_relaxed); }

	/* UI thread. The latest StatusSnapshot (StatusMonitor). Rebuilds the
	 * status tiles whose text changed; a no-op for layouts without them. */
	void set_status(const StatusSnapshot &status);

	/* Graphics thread, from the display draw callback. */
	void render(uint32_t cx, uint32_t cy);

	/* UI thread. Drops every scene reference and undoes inc_showing, and frees
	 * the label GPU objects. Idempotent; nothing is rendered afterwards. */
	void shutdown();

private:
	struct SlotEntry {
		obs_weak_source_t *weak = nullptr; /* null: empty tile */
		uint32_t border = 0;               /* ARGB outline colour */
	};

	/* All labels of a snapshot. Immutable once built. */
	struct LabelBatch {
		uint64_t id = 0; /* unique, never reused */
		uint32_t atlas_cx = 0;
		uint32_t atlas_cy = 0;
		std::vector<uint8_t> atlas;     /* BGRA, straight alpha */
		std::vector<vec3> box_points;   /* GS_TRIS, canvas coordinates */
		std::vector<vec3> text_points;  /* GS_TRIS, canvas coordinates */
		std::vector<vec2> text_uvs;     /* atlas coordinates of text_points */
	};

	/* GPU objects of the batch drawn last (graphics thread only). */
	struct GfxLabels {
		uint64_t id = 0;
		gs_texture_t *atlas = nullptr;
		gs_vertbuffer_t *boxes = nullptr;
		gs_vertbuffer_t *texts = nullptr;
		uint32_t box_vertices = 0;
		uint32_t text_vertices = 0;
	};

	/* The status tiles of a snapshot as one atlas (a 2x2 grid of equal cells)
	 * and one quad per tile, in canvas coordinates. Immutable once built. */
	struct StatusBatch {
		uint64_t id = 0; /* unique, never reused */
		uint32_t atlas_cx = 0;
		uint32_t atlas_cy = 0;
		std::vector<uint8_t> atlas;  /* BGRA, opaque */
		std::vector<vec3> points;    /* GS_TRIS, canvas coordinates */
		std::vector<vec2> uvs;       /* atlas coordinates of points */
	};

	/* GPU objects of the status batch drawn last (graphics thread only). */
	struct GfxStatus {
		uint64_t id = 0;
		gs_texture_t *atlas = nullptr;
		gs_vertbuffer_t *quads = nullptr;
		uint32_t vertices = 0;
	};

	/* One placed status tile, for building and comparing batches (UI thread). */
	struct PlacedStatus {
		std::shared_ptr<const StatusTileImage> image;
		RectF rect; /* canvas pixels */
		bool operator==(const PlacedStatus &o) const
		{
			return image == o.image && rect.x == o.rect.x && rect.y == o.rect.y && rect.w == o.rect.w &&
			       rect.h == o.rect.h;
		}
	};

	struct RenderSnapshot {
		std::vector<Tile> tiles;    /* normalized */
		std::vector<RectF> fillers; /* normalized, areas without tiles */
		std::vector<SlotEntry> slot_entries;
		obs_weak_source_t *preview = nullptr; /* studio mode only */
		bool studio_mode = false;
		std::shared_ptr<const LabelBatch> labels; /* null: no labels */
		std::shared_ptr<const StatusBatch> status; /* null: no status tiles */
		uint32_t canvas_cx = 0; /* canvas the labels were laid out for */
		uint32_t canvas_cy = 0;

		RenderSnapshot() = default;
		RenderSnapshot(const RenderSnapshot &) = delete;
		RenderSnapshot &operator=(const RenderSnapshot &) = delete;
		~RenderSnapshot();
	};

	/* One placed label, for building and comparing batches (UI thread). */
	struct PlacedLabel {
		std::shared_ptr<const LabelImage> image;
		float x;
		float y;
		LabelScale scale;
		bool operator==(const PlacedLabel &o) const
		{
			return image == o.image && x == o.x && y == o.y && scale.x == o.scale.x && scale.y == o.scale.y;
		}
	};

	void rebuild_snapshot(); /* outlines / studio mode / preview / labels */
	void build_labels(RenderSnapshot &snap);
	std::shared_ptr<const LabelBatch> make_batch(const std::vector<PlacedLabel> &placed);
	void build_status(RenderSnapshot &snap);
	std::shared_ptr<const StatusBatch> make_status_batch(const std::vector<PlacedStatus> &placed);
	float display_scale(float fw, float fh) const;
	void check_canvas();
	/* Graphics thread only (or with the draw callback gone, in graphics). */
	bool upload_labels(const LabelBatch &batch);
	void destroy_gfx_labels();
	bool upload_status(const StatusBatch &batch);
	void destroy_gfx_status();
	void release_showing();
	void publish(std::shared_ptr<const RenderSnapshot> snapshot);

	ObsState &m_state;
	Preset m_preset = Preset::Scenes8;
	std::vector<Tile> m_tiles;
	std::vector<RectF> m_fillers;
	SceneForSlot m_scene_for_slot;
	bool m_shut_down = false;

	/* UI thread only: per slot, the scene name and an owned weak ref of the
	 * scene this renderer called obs_source_inc_showing on. */
	std::vector<std::string> m_slot_names;
	std::vector<obs_weak_source_t *> m_slot_weak;

	/* UI thread only: labels. */
	LabelCache m_labels;
	int m_display_cx = 0; /* 0: not known yet (rasterize at 1:1 canvas) */
	int m_display_cy = 0;
	uint32_t m_canvas_cx = 0;
	uint32_t m_canvas_cy = 0;
	std::vector<std::pair<int, RectF>> m_label_hits; /* scene slot, canvas box */
	std::vector<PlacedLabel> m_placed;               /* content of m_batch */
	std::shared_ptr<const LabelBatch> m_batch;
	uint64_t m_next_batch_id = 1;
	QTimer m_canvas_timer;

	/* UI thread only: status tiles. */
	StatusSnapshot m_status = make_snapshot(RawStatus());
	struct StatusCacheEntry {
		TileKind kind = TileKind::OnAir;
		int w = 0;
		int h = 0;
		StatusSnapshot content;
		std::shared_ptr<const StatusTileImage> image;
	};
	std::vector<StatusCacheEntry> m_status_cache;
	std::vector<PlacedStatus> m_status_placed; /* content of m_status_batch */
	std::shared_ptr<const StatusBatch> m_status_batch;
	uint64_t m_next_status_id = 1;

	/* Graphics thread only (see the class comment). */
	GfxLabels m_gfx;
	GfxStatus m_gfx_status;
	SafeAreas m_safe;
	std::atomic<bool> m_safe_areas{true};

	std::mutex m_snapshot_mutex; /* guards the pointer only */
	std::shared_ptr<const RenderSnapshot> m_snapshot;
};
