// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "layout_model.hpp"

#include <obs.h>

#include <QObject>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
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
 *    callback has returned) before calling shutdown() or destroying this. */
class MultiviewRenderer : public QObject {
public:
	using SceneForSlot = std::function<std::string(int)>;

	explicit MultiviewRenderer(ObsState &state);
	~MultiviewRenderer() override;

	MultiviewRenderer(const MultiviewRenderer &) = delete;
	MultiviewRenderer &operator=(const MultiviewRenderer &) = delete;

	void set_preset(Preset preset);

	/* slot index -> scene name ("" = empty tile). Default: the i-th scene of
	 * ObsState::scenes(). Passing an empty function restores the default. */
	void set_scene_for_slot(SceneForSlot scene_for_slot);

	/* Graphics thread, from the display draw callback. */
	void render(uint32_t cx, uint32_t cy);

	/* UI thread. Drops every scene reference and undoes inc_showing. Idempotent;
	 * nothing is rendered afterwards. */
	void shutdown();

private:
	struct SlotEntry {
		obs_weak_source_t *weak = nullptr; /* null: empty tile */
		uint32_t border = 0;               /* ARGB outline colour */
	};

	struct RenderSnapshot {
		std::vector<Tile> tiles;    /* normalized */
		std::vector<RectF> fillers; /* normalized, areas without tiles */
		std::vector<SlotEntry> slot_entries;
		obs_weak_source_t *preview = nullptr; /* studio mode only */
		bool studio_mode = false;

		RenderSnapshot() = default;
		RenderSnapshot(const RenderSnapshot &) = delete;
		RenderSnapshot &operator=(const RenderSnapshot &) = delete;
		~RenderSnapshot();
	};

	void refresh_scenes();  /* re-resolve slot scenes, then rebuild */
	void rebuild_snapshot(); /* outlines / studio mode / preview */
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

	std::mutex m_snapshot_mutex; /* guards the pointer only */
	std::shared_ptr<const RenderSnapshot> m_snapshot;
};
