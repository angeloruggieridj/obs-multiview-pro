// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <obs.h>

#include "layout_model.hpp"
#include "tile_assignment.hpp"

#include <QElapsedTimer>
#include <QPoint>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <cstdint>
#include <memory>
#include <string>

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

	/* Keeps the window above other windows (the global "always on top"
	 * option). Applied at once; position, size and state are kept. */
	void set_always_on_top(bool on);

protected:
	bool event(QEvent *event) override;
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
	void focusOutEvent(QFocusEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void changeEvent(QEvent *event) override;

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
	/* Forgets a press / drag in progress and restores the cursor. */
	void cancel_press();
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
	/* Connects to the native window's QWindow (visibility, screen, surface
	 * events). Idempotent; follows a QWindow that was replaced. */
	void hook_window();
	void unhook_window();
	/* Puts m_on_top on the native window. Windows: SetWindowPos (the window
	 * is not recreated). Elsewhere: the Qt flag (see apply_on_top_qt). */
	void apply_on_top();
	/* Qt::WindowStaysOnTopHint through the Qt API. Changing a window flag
	 * hides the window and may give it a new native window, so the display
	 * is destroyed first, the QWindow hooks are installed again and the
	 * window is shown again with its geometry and state. */
	void apply_on_top_qt();
#if defined(_WIN32)
	void apply_on_top_win32();
#endif
	void create_display();
	void destroy_display();
	void resize_display();
	/* Renders only while the window is shown and not minimised (UI thread,
	 * idempotent, null-safe); a restored window draws again at once. */
	void update_display_enabled();
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
	/* Handles to the QWindow connections: cut in the destructor, before the
	 * members they use are gone. */
	QMetaObject::Connection m_visible_connection;
	QMetaObject::Connection m_screen_connection;
	QPointer<QWindow> m_hooked_window; /* the QWindow they belong to */
	bool m_on_top = false;             /* "always on top", as applied */
#if defined(_WIN32)
	/* Looks again after a topmost change (see apply_on_top_win32). */
	static constexpr int kOnTopChecks = 3;
	static constexpr int kOnTopCheckMs = 300;
	int m_on_top_checks = 0;
#endif
	bool m_destroying = false;
	bool m_window_handle_warned = false;

	/* Left-button press / drag state (drag and drop between tiles). */
	bool m_press_active = false;
	bool m_dragging = false;
	int m_press_slot = -1;
	std::string m_press_scene; /* the scene in m_press_slot at the press */
	QPoint m_press_pos;
	/* Left press on a scene label: opens the scene menu on release. */
	int m_label_press_slot = -1;

	/* Labels are re-rasterized for the window size once a resize settles. */
	QTimer m_label_scale_timer;
};
