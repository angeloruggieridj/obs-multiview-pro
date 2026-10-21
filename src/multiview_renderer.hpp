// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "appearance.hpp"
#include "audio_meter.hpp"
#include "label_cache.hpp"
#include "label_metrics.hpp"
#include "layout_model.hpp"
#include "meter_pool.hpp"
#include "meter_scale.hpp"
#include "mixer_columns.hpp"
#include "mixer_feeds.hpp"
#include "safe_areas.hpp"
#include "status_format.hpp"
#include "status_tiles.hpp"
#include "tile_meters.hpp"

#include <obs.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>

#include <QObject>
#include <QTimer>

#include <array>
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
 *  - Per frame: two draw calls for all labels, no allocation of ours, no upload.
 *
 * Status tiles (ON AIR, REC, Clock, Mixer of the "con stato" presets): each is
 * rasterized with Qt on the UI thread (render_status_tile) at the tile's size
 * in display pixels, only when its text or size changes (at most once a
 * second, driven by StatusMonitor), and packed into one immutable StatusBatch
 * (2x2 atlas plus the quads). It follows the same ownership rules as the label
 * batch: the graphics thread uploads it once per new batch (m_gfx_status),
 * shutdown() frees it in the graphics context. Per frame: one draw call, no
 * allocation, no upload, no Qt.
 *
 * Audio meters (Program: big, with a dB scale; Preview: small, bars only; both
 * on the left edge of the tile): see set_audio_meters. The levels come from
 * two AudioMeter objects (atomics written by OBS audio threads); each frame the
 * graphics thread reads them once, runs the ballistics of the native meter
 * (pure code, audio_meter_math) with the frame time, and draws a handful of
 * solid quads. The dB numbers are rasterized once with Qt (render_meter_scale)
 * and travel in the snapshot like the status atlas (one texture, one quad,
 * m_gfx_scale). While the meters are off nothing is attached to OBS audio and
 * render() only does one atomic load for them.
 *
 * Mixer tile (status presets, meters on): one column per audio source (the OBS
 * global audio devices, the sources of the program scene and, in studio mode,
 * of the preview scene; MixerFeeds, UI thread) and PGM last (it reads the
 * program levels the Program meter already takes). The columns are stable, and
 * the graphics thread carries each column's ballistics by source id. The bars are solid quads in the same effect loop as the
 * other meters (ballistics per column, same maths), drawn after the status
 * batch so they go over the tile's image. The names and dB values are
 * rasterized with Qt into that image, only when a name, value, mute state or
 * size changes; a dB value is refreshed at most twice a second and only for a
 * change of 1 dB or more (db_text_update_due), from the peak-hold the graphics
 * thread publishes in atomics (m_mixer_hold) and the 500 ms poll reads. Meters
 * off: the dark "Mixer" placeholder, nothing attached.
 *
 * Scene tile meters (meters on and Appearance::tile_meters): two thin bars on
 * the left edge of every scene tile that has a scene (tile_meter_geometry),
 * showing per channel the loudest audio source of that scene
 * (collect_scene_audio_sources: what the OBS mixer would list for it, nested
 * scenes and groups included). A scene emits no audio of its own; sources
 * that only play while their scene is on air stay silent elsewhere, as in the
 * OBS mixer.
 *
 * Source levels (Preview meter, mixer columns, scene tiles): one MeterPool per
 * renderer, one volmeter per distinct source however many places show it, at
 * most kMaxMeterSources (mixer columns first, then the preview scene, then the
 * scene tiles in slot order; logged once per new highest number left out).
 * The 500 ms poll and the ObsState signals rebuild the plan and sync the pool;
 * the snapshot carries the meters and, per consumer, the indices of its
 * sources (AudioFeeds). Each frame the graphics thread takes every meter's
 * peaks once, then each consumer reads the maximum over its indices. */
class MultiviewRenderer : public QObject {
public:
	using SceneForSlot = std::function<std::string(int)>;

	explicit MultiviewRenderer(ObsState &state, const Appearance &appearance = Appearance());
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

	/* UI thread. Shows or hides the audio meters on the Program and Preview
	 * tiles (default on). Turning them on attaches the OBS audio callbacks
	 * (program mix; in studio mode also one volmeter per audio source of the
	 * preview scene); turning them off removes every one of them, so the
	 * audio threads do no work for the multiview. */
	void set_audio_meters(bool on);
	bool audio_meters() const { return m_meters_on.load(std::memory_order_relaxed); }

	/* UI thread. The latest StatusSnapshot (StatusMonitor). Rebuilds the
	 * status tiles whose text changed; a no-op for layouts without them. */
	void set_status(const StatusSnapshot &status);

	/* UI thread. The frame colour, the label opacity and the meter opacity
	 * (see Appearance). They reach the graphics thread in the next snapshot:
	 * the frame colour and the meter opacity are applied at draw time; the
	 * label text and the dB / mixer texts are re-rasterized once, only when
	 * their opacity actually changes. */
	void set_appearance(const Appearance &appearance);
	Appearance appearance() const { return m_appearance; }

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
		/* Per placed label, in order: it got room in the atlas (a label
		 * that did not is not drawn, so it gets no hit box either). */
		std::vector<uint8_t> placed_fits;
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

	/* The scale text of the Program meter as one texture. Immutable. */
	struct MeterScaleBatch {
		uint64_t id = 0; /* unique, never reused */
		std::shared_ptr<const MeterScaleImage> image;
		RectF rect; /* canvas pixels (MeterGeometry::column) */
	};

	/* GPU object of the scale drawn last (graphics thread only). */
	struct GfxScale {
		uint64_t id = 0;
		gs_texture_t *tex = nullptr;
	};

	/* Display state of the four bars (graphics thread only). */
	struct MeterDisplay {
		ChannelBallistics program[2];
		ChannelBallistics preview[2];
		ChannelBallistics mixer[kMixerMaxColumns][2];
		MixerSourceId mixer_ids[kMixerMaxColumns] = {}; /* source of each mixer[] entry */
		size_t mixer_count = 0;
		uint64_t mixer_generation = 0; /* MixerDraw the mixer[] state belongs to */
		ChannelBallistics tiles[kMaxTileMeterSlots][2];
		uint64_t tile_keys[kMaxTileMeterSlots] = {}; /* AudioFeeds::TileFeed::key of each */
		uint64_t last_ns = 0; /* 0: first frame after the meters were turned on */
	};

	/* One column of the mixer tile as the graphics thread needs it. */
	struct MixerDrawColumn {
		MixerSourceId id = kMixerPgm;
		int meter = -1;               /* index into AudioFeeds::meters; -1 program mix, -2 none */
		MixerColumnGeometry geometry; /* canvas pixels */
		bool muted = false;
	};
	/* The mixer tile's columns. Immutable once built. */
	struct MixerDraw {
		uint64_t generation = 0; /* MixerFeeds::generation(): ballistics restart when it changes */
		std::vector<MixerDrawColumn> columns;
	};

	/* The source meters of a snapshot and who reads which. Immutable once
	 * built (UI thread, poll_audio). The pool detaches a meter (UI thread)
	 * before it drops it; the graphics thread only reads the atomics. */
	struct AudioFeeds {
		struct TileFeed {
			bool on = false;  /* the tile has a scene and tile meters are on */
			uint64_t key = 0; /* changes when its sources change: ballistics restart */
			std::vector<size_t> meters;
			bool operator==(const TileFeed &o) const { return on == o.on && key == o.key && meters == o.meters; }
		};
		std::vector<std::shared_ptr<SourceMeter>> meters; /* distinct, non-null, at most kMaxMeterSources */
		std::vector<size_t> preview;                      /* studio mode: the preview scene's */
		std::vector<TileFeed> tiles;                      /* by scene slot */
		std::vector<int> mixer;                           /* per MixerFeeds column, as MixerDrawColumn::meter */
		bool operator==(const AudioFeeds &o) const
		{
			return meters == o.meters && preview == o.preview && tiles == o.tiles && mixer == o.mixer;
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
		std::shared_ptr<const MeterScaleBatch> meter_scale; /* null: meters off / no room */
		std::shared_ptr<const MixerDraw> mixer; /* null: meters off / no mixer tile */
		std::shared_ptr<const AudioFeeds> audio; /* null: meters off */
		bool tile_meters = false;                /* meters on and Appearance::tile_meters */
		/* From the Appearance when the snapshot was built. */
		uint32_t border_argb = 0xFF000000u | kDefaultBorderRgb; /* frame under the tiles */
		uint32_t label_box_argb = 0x33000000;                   /* label background boxes */
		int meter_opacity = kDefaultMeterOpacity;               /* percent */
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
		RectF text; /* canvas: the text texture on whole device pixels */
		bool operator==(const PlacedLabel &o) const
		{
			return image == o.image && x == o.x && y == o.y && scale.x == o.scale.x && scale.y == o.scale.y &&
			       text.x == o.text.x && text.y == o.text.y && text.w == o.text.w && text.h == o.text.h;
		}
	};

	void rebuild_snapshot(); /* outlines / studio mode / preview / labels */
	void build_labels(RenderSnapshot &snap);
	std::shared_ptr<const LabelBatch> make_batch(const std::vector<PlacedLabel> &placed);
	void build_status(RenderSnapshot &snap);
	std::shared_ptr<const StatusBatch> make_status_batch(const std::vector<PlacedStatus> &placed);
	void build_meter_scale(RenderSnapshot &snap);
	void build_mixer(RenderSnapshot &snap);
	bool has_mixer_tile() const;
	/* UI thread: the 500 ms poll (sources of the preview scene, of the mixer
	 * tile and of the scene tiles; mixer texts), syncing the meter pool. */
	void poll_audio();
	/* UI thread: (re)attaches the audio feeds to what is on air / in preview. */
	void refresh_audio();
	bool tile_meters_wanted() const;
	DeviceMap display_map(float fw, float fh) const;
	void check_canvas();
	/* Graphics thread only (or with the draw callback gone, in graphics). */
	bool upload_labels(const LabelBatch &batch);
	void destroy_gfx_labels();
	bool upload_status(const StatusBatch &batch);
	void destroy_gfx_status();
	bool upload_scale(const MeterScaleBatch &batch);
	void destroy_gfx_scale();
	void draw_meters(const RenderSnapshot &snap, float fw, float fh, float scale, bool labels_ok);
	void release_showing();
	void publish(std::shared_ptr<const RenderSnapshot> snapshot);

	ObsState &m_state;
	Appearance m_appearance;
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
		bool mixer_on = false;               /* Mixer: drawn with columns */
		int mixer_opacity = 100;             /* Mixer: opacity of the names / values */
		std::vector<MixerColumnView> mixer;  /* Mixer: what the columns show */
		std::shared_ptr<const StatusTileImage> image;
	};
	std::vector<StatusCacheEntry> m_status_cache;
	std::vector<PlacedStatus> m_status_placed; /* content of m_status_batch */
	std::shared_ptr<const StatusBatch> m_status_batch;
	uint64_t m_next_status_id = 1;

	/* UI thread only: audio meters. m_pgm_audio and the pool's meters are also
	 * read (atomics only) by the graphics thread; m_pgm_audio lives as long as
	 * the renderer, a meter as long as a snapshot holds it. */
	AudioMeter m_pgm_audio;
	MeterPool m_pool; /* one volmeter per distinct source (preview, mixer, scene tiles) */
	std::shared_ptr<const AudioFeeds> m_feeds;
	std::vector<std::vector<MeterSourceId>> m_tile_ids; /* per slot: the sources of its feed */
	std::vector<uint64_t> m_tile_keys;
	uint64_t m_next_tile_key = 1;
	size_t m_meter_dropped_logged = 0; /* highest number of sources left out, as logged */
	QTimer m_audio_timer; /* 500 ms: sources of the preview, mixer and scene tiles */
	MixerFeeds m_mixer;   /* columns of the mixer tile (meters on + mixer tile) */
	std::shared_ptr<const MeterScaleBatch> m_scale_batch;
	std::vector<int> m_scale_key; /* what m_scale_batch was rasterized for */
	uint64_t m_next_scale_id = 1;

	/* Graphics thread only (see the class comment). */
	GfxLabels m_gfx;
	GfxScale m_gfx_scale;
	MeterDisplay m_meter;
	std::array<std::array<float, 2>, kMaxMeterSources> m_frame_peaks{}; /* this frame's peaks per meter */
	/* Written by the graphics thread each frame (peak-hold of each mixer
	 * column, larger channel), read by the UI poll; valid when the generation
	 * equals the one of the columns being read. */
	std::atomic<float> m_mixer_hold[kMixerMaxColumns];
	std::atomic<uint64_t> m_mixer_hold_generation{0};
	std::atomic<bool> m_meters_on{true};
	GfxStatus m_gfx_status;
	SafeAreas m_safe;
	std::atomic<bool> m_safe_areas{true};

	std::mutex m_snapshot_mutex; /* guards the pointer only */
	std::shared_ptr<const RenderSnapshot> m_snapshot;
};
