// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "layout_model.hpp"

#include <QRect>
#include <QWidget>

#include <array>

/* Popup that shows one thumbnail per layout preset, drawn with QPainter from
 * build_layout (the same geometry the renderer uses). Clicking a thumbnail
 * emits preset_chosen and closes the popup; Esc or a click outside closes it
 * without a choice. UI only: nothing here runs on the draw path. */
class PresetPicker : public QWidget {
	Q_OBJECT

public:
	static constexpr int kPresetCount = 5;

	explicit PresetPicker(Preset current, QWidget *parent = nullptr);

	static Preset preset_at(int index);

	/* Cell (thumbnail + label) of the preset at `index`, in widget coordinates. */
	QRect cell_rect(int index) const;

signals:
	void preset_chosen(Preset preset);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	int index_at(const QPoint &pos) const;

	Preset m_current;
	int m_hover = -1;
	std::array<std::vector<Tile>, kPresetCount> m_layouts;
};
