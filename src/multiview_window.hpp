// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <obs.h>

#include <QWidget>

#include <cstdint>
#include <memory>

class ObsState;
class MultiviewRenderer;

/* Top-level multiview window with its own obs_display (public API only, set up
 * like OBS's OBSQTDisplay). Owns the ObsState, the renderer and the display.
 * Destroyed on close. */
class MultiviewWindow : public QWidget {
	Q_OBJECT

public:
	explicit MultiviewWindow(QWidget *parent = nullptr);
	~MultiviewWindow() override;

	QPaintEngine *paintEngine() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void moveEvent(QMoveEvent *event) override;
	bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
	bool eventFilter(QObject *watched, QEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;

private:
	static void draw_callback(void *param, uint32_t cx, uint32_t cy);

	void show_layout_picker(const QPoint &global_pos);
	void create_display();
	void destroy_display();
	void resize_display();
	void update_color_space();
	QSize pixel_size() const;

	std::unique_ptr<ObsState> m_state;
	std::unique_ptr<MultiviewRenderer> m_renderer;
	obs_display_t *m_display = nullptr;
	bool m_destroying = false;
	bool m_window_handle_warned = false;
};
