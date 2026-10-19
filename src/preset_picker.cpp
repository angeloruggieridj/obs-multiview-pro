// SPDX-License-Identifier: GPL-2.0-or-later
#include "preset_picker.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

namespace {
constexpr int kMargin = 10;
constexpr int kGap = 8;
constexpr int kThumbW = 128;
constexpr int kThumbH = 72; /* 16:9, like the canvas */
constexpr int kLabelH = 20;
constexpr int kCellPad = 4;

constexpr Preset kPresets[PresetPicker::kPresetCount] = {Preset::Scenes4, Preset::Scenes8, Preset::Scenes16,
							  Preset::Scenes24, Preset::Scenes32};
constexpr int kSceneCounts[PresetPicker::kPresetCount] = {4, 8, 16, 24, 32};

constexpr int kCellW = kThumbW + 2 * kCellPad;
constexpr int kCellH = kThumbH + kLabelH + 2 * kCellPad;
} // namespace

PresetPicker::PresetPicker(Preset current, QWidget *parent)
	: QWidget(parent, Qt::Popup | Qt::FramelessWindowHint), m_current(current)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setMouseTracking(true);
	for (int i = 0; i < kPresetCount; ++i)
		m_layouts[static_cast<size_t>(i)] = build_layout(kPresets[i]);
	setFixedSize(2 * kMargin + kPresetCount * kCellW + (kPresetCount - 1) * kGap, 2 * kMargin + kCellH);
}

Preset PresetPicker::preset_at(int index)
{
	return kPresets[index];
}

QRect PresetPicker::cell_rect(int index) const
{
	return QRect(kMargin + index * (kCellW + kGap), kMargin, kCellW, kCellH);
}

int PresetPicker::index_at(const QPoint &pos) const
{
	for (int i = 0; i < kPresetCount; ++i)
		if (cell_rect(i).contains(pos))
			return i;
	return -1;
}

void PresetPicker::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	const QPalette &pal = palette();
	p.fillRect(rect(), pal.color(QPalette::Window));
	p.setPen(pal.color(QPalette::Mid));
	p.drawRect(rect().adjusted(0, 0, -1, -1));

	for (int i = 0; i < kPresetCount; ++i) {
		const QRect cell = cell_rect(i);
		const bool current = kPresets[i] == m_current;
		if (i == m_hover)
			p.fillRect(cell, pal.color(QPalette::Highlight).lighter(150));
		if (current) {
			p.setPen(QPen(pal.color(QPalette::Highlight), 2));
			p.drawRect(cell.adjusted(1, 1, -1, -1));
		}

		const QRect thumb(cell.x() + kCellPad, cell.y() + kCellPad, kThumbW, kThumbH);
		p.fillRect(thumb, Qt::black);
		for (const Tile &t : m_layouts[static_cast<size_t>(i)]) {
			const PixelRect r = to_pixel_rect(t.rect, kThumbW, kThumbH);
			QColor fill(0x55, 0x55, 0x55);
			if (t.kind == TileKind::Preview)
				fill = QColor(0x00, 0x90, 0x00);
			else if (t.kind == TileKind::Program)
				fill = QColor(0x90, 0x00, 0x00);
			p.fillRect(QRect(thumb.x() + r.x, thumb.y() + r.y, r.w, r.h).adjusted(0, 0, -1, -1), fill);
		}

		p.setPen(pal.color(QPalette::WindowText));
		p.drawText(QRect(cell.x(), thumb.bottom() + 1, cell.width(), kLabelH), Qt::AlignCenter,
			   tr("%1 scenes").arg(kSceneCounts[i]));
	}
}

void PresetPicker::mouseMoveEvent(QMouseEvent *event)
{
	const int hover = index_at(event->position().toPoint());
	if (hover != m_hover) {
		m_hover = hover;
		update();
	}
}

void PresetPicker::leaveEvent(QEvent *)
{
	m_hover = -1;
	update();
}

void PresetPicker::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;
	const int index = index_at(event->position().toPoint());
	if (index < 0)
		return;
	const Preset chosen = kPresets[index];
	close();
	emit preset_chosen(chosen);
}

void PresetPicker::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape)
		close();
	else
		QWidget::keyPressEvent(event);
}
