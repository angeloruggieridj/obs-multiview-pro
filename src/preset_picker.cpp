// SPDX-License-Identifier: GPL-2.0-or-later
#include "preset_picker.hpp"

#include "i18n.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

#include <algorithm>

namespace {
constexpr int kMargin = 10;
constexpr int kGap = 8;
constexpr int kThumbW = 128;
constexpr int kThumbH = 72; /* 16:9, like the canvas */
constexpr int kLabelH = 20;
constexpr int kCellPad = 4;

/* Row 0: "scenes only" (native-identical). Row 1: the same grids with the four
 * status tiles (ON AIR, REC, Clock, Mixer) in the last cells; there is no
 * 4-scene one, so row 1 starts under the second column. */
constexpr int kColumns = 5;
constexpr int kRows = 2;
constexpr Preset kPresets[PresetPicker::kPresetCount] = {
	Preset::Scenes4,       Preset::Scenes8,       Preset::Scenes16,      Preset::Scenes24,      Preset::Scenes32,
	Preset::Scenes8Status, Preset::Scenes16Status, Preset::Scenes24Status, Preset::Scenes32Status};
constexpr int kStatusRowFirstIndex = 5;

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
	setFixedSize(2 * kMargin + kColumns * kCellW + (kColumns - 1) * kGap, 2 * kMargin + kRows * kCellH + (kRows - 1) * kGap);
}

Preset PresetPicker::preset_at(int index)
{
	return kPresets[index];
}

QRect PresetPicker::cell_rect(int index) const
{
	const bool status_row = index >= kStatusRowFirstIndex;
	const int column = status_row ? index - kStatusRowFirstIndex + 1 : index;
	const int row = status_row ? 1 : 0;
	return QRect(kMargin + column * (kCellW + kGap), kMargin + row * (kCellH + kGap), kCellW, kCellH);
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
	const QString on_air = T("Tile.OnAir");
	const QString rec = T("Tile.Rec");
	const QString mix = T("Picker.Mixer");
	const QString clock = QStringLiteral("12:00");
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
			const QString *caption = nullptr;
			switch (t.kind) {
			case TileKind::OnAir:
				fill = QColor(0x7A, 0x14, 0x14);
				caption = &on_air;
				break;
			case TileKind::Rec:
				fill = QColor(0x8A, 0x4A, 0x10);
				caption = &rec;
				break;
			case TileKind::Clock:
				fill = QColor(0x1F, 0x3F, 0x7A);
				caption = &clock;
				break;
			case TileKind::Mixer:
				fill = QColor(0x1F, 0x6A, 0x5A);
				caption = &mix;
				break;
			default:
				break;
			}
			const QRect tile = QRect(thumb.x() + r.x, thumb.y() + r.y, r.w, r.h).adjusted(0, 0, -1, -1);
			p.fillRect(tile, fill);
			/* Status tiles get a short caption when the cell is big enough. */
			if (caption && tile.height() >= 14) {
				QFont f = p.font();
				f.setPixelSize(std::max(6, std::min(9, tile.height() / 2)));
				f.setBold(true);
				p.setFont(f);
				p.setPen(Qt::white);
				p.drawText(tile, Qt::AlignCenter, *caption);
			}
		}

		/* The number shown is the real count of scene tiles of the layout (the status
		 * presets give four cells to ON AIR / REC / Clock / Mixer). */
		const auto &layout = m_layouts[static_cast<size_t>(i)];
		const int scene_count = static_cast<int>(
			std::count_if(layout.begin(), layout.end(), [](const Tile &t) { return t.kind == TileKind::Scene; }));
		p.setFont(font());
		p.setPen(pal.color(QPalette::WindowText));
		p.drawText(QRect(cell.x(), thumb.bottom() + 1, cell.width(), kLabelH), Qt::AlignCenter,
			   T(i >= kStatusRowFirstIndex ? "Picker.ScenesStatus" : "Picker.Scenes", QString::number(scene_count)));
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
