// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <obs.h>

#include "layout_model.hpp"
#include "tile_assignment.hpp"

#include <QElapsedTimer>
#include <QPoint>
#include <QTimer>
#include <QWidget>

#include <cstdint>
#include <memory>

class ObsState;
class MultiviewRenderer;
class StatusMonitor;
class QMenu;

/* Top-level multiview window with its own obs_display (public API only, set up
 * like OBS's OBSQTDisplay). Owns the ObsState, the renderer and the display.
 * Destroyed on close. */
class MultiviewWindow : public QWidget {
	Q_OBJECT

public:
	/* `instance`: index of this window among the open ones (0..N, the lowest
	 * free one, chosen by plugin.cpp); its settings are saved under it. */
	explicit MultiviewWindow(int instance, QWidget *parent = nullptr);
	~MultiviewWindow() override;

	QPaintEngine *paintEngine() const override;

	int instance() const { return m_instance; }

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void moveEvent(QMoveEvent *event) override;
	bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
	bool eventFilter(QObject *watched, QEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
	static void draw_callback(void *param, uint32_t cx, uint32_t cy);

	void show_layout_picker(const QPoint &global_pos);
	/* Switches the layout: scene assignments of the slots that survive are
	 * kept, and the status sampling runs only while the layout has status
	 * tiles. */
	void apply_preset(Preset preset);
	/* Scene slot under a widget position (letterbox and devicePixelRatio
	 * accounted for), or -1 for PVW / PGM / outside every tile. */
	int scene_slot_at(const QPoint &pos) const;
	/* Scene slot whose label is under a widget position, or -1. */
	int label_slot_at(const QPoint &pos) const;
	/* The scene choices for `slot` (scenes visible in the multiview, current
	 * one checked); picking one assigns it. Used by the context menu and by a
	 * click on a label. */
	void add_scene_choices(QMenu *menu, int slot);
	void show_scene_menu(int slot, const QPoint &global_pos);
	void update_label_scale();
	void sync_scene_names();
	void on_scenes_changed();
	void on_scene_collection_changed();
	void on_scene_collection_changing();
	void assignment_changed();
	void assignment_changed_no_save();
	/* Persistence (SettingsStore: debounced write, final flush on close). */
	void save_options();
	void save_assignment();
	void load_assignment();
	void create_display();
	void destroy_display();
	void resize_display();
	void update_color_space();
	QSize pixel_size() const;

	const int m_instance;
	/* Scene collection the assignment belongs to (the settings key). */
	std::string m_collection;
	/* A collection switch is in progress: no assignment is saved. */
	bool m_switching = false;
	QElapsedTimer m_switch_clock; /* since the switch began */
	/* True while a switch is in progress; one that has been "in progress" for
	 * far longer than any load takes (CHANGING without CHANGED) is ended. */
	bool switching_now();
	TileAssignment m_assignment; /* UI thread only */
	std::unique_ptr<ObsState> m_state;
	std::unique_ptr<MultiviewRenderer> m_renderer;
	std::unique_ptr<StatusMonitor> m_monitor; /* observes m_state: destroyed before it */
	obs_display_t *m_display = nullptr;
	bool m_destroying = false;
	bool m_window_handle_warned = false;

	/* Left-button press / drag state (drag and drop between tiles). */
	bool m_press_active = false;
	bool m_dragging = false;
	int m_press_slot = -1;
	QPoint m_press_pos;
	/* Left press on a scene label: opens the scene menu on release. */
	int m_label_press_slot = -1;

	/* Labels are re-rasterized for the window size once a resize settles. */
	QTimer m_label_scale_timer;
};
