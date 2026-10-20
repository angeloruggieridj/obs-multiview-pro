// SPDX-License-Identifier: GPL-2.0-or-later
#include "status_tiles.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <cstring>

namespace {

const QColor kBlack(0x00, 0x00, 0x00);
const QColor kOffBackground(0x1B, 0x1B, 0x1B); /* inactive / placeholder tile */
const QColor kRed(0xE8, 0x22, 0x22);
const QColor kAmber(0xF0, 0xA0, 0x20);
const QColor kWhite(0xFF, 0xFF, 0xFF);
const QColor kCaption(0x9A, 0x9A, 0x9A);
const QColor kDim(0x55, 0x55, 0x55); /* title / values of an inactive output */

QFont make_font(bool bold)
{
#if defined(_WIN32)
	QFont f(QStringLiteral("Arial"));
#elif defined(__APPLE__)
	QFont f(QStringLiteral("Helvetica"));
#else
	QFont f(QStringLiteral("Sans Serif"));
#endif
	f.setStyleHint(QFont::SansSerif);
	f.setBold(bold);
	f.setHintingPreference(QFont::PreferNoHinting);
	return f;
}

/* Draws `text` centered in the band [y0, y1) x [x0, x1) of the tile (fractions
 * of its size), at `px_frac` * tile height pixels, shrunk until it fits the band
 * width. Skipped when the result would be unreadable (< 6 px). */
class Painter {
public:
	Painter(QPainter &p, int w, int h) : m_p(p), m_w(w), m_h(h) {}

	void text(const QString &s, double x0, double x1, double y0, double y1, double px_frac, const QColor &color,
		  bool bold = true, Qt::TextElideMode elide = Qt::ElideNone) const
	{
		if (s.isEmpty())
			return;
		const QRect band(int(x0 * m_w), int(y0 * m_h), std::max(1, int((x1 - x0) * m_w)),
				 std::max(1, int((y1 - y0) * m_h)));
		int px = std::max(1, int(px_frac * m_h));
		QFont f = make_font(bold);
		QString shown = s;
		if (elide != Qt::ElideNone) {
			f.setPixelSize(px);
			shown = QFontMetrics(f).elidedText(s, elide, band.width());
		} else {
			for (; px > 6; --px) {
				f.setPixelSize(px);
				if (QFontMetrics(f).horizontalAdvance(s) <= band.width())
					break;
			}
			f.setPixelSize(px);
		}
		if (px < 6)
			return;
		m_p.setFont(f);
		m_p.setPen(color);
		m_p.drawText(band, Qt::AlignCenter, shown);
	}

private:
	QPainter &m_p;
	int m_w;
	int m_h;
};

void draw_on_air(const Painter &d, const StatusSnapshot &s)
{
	const bool on = s.streaming;
	d.text(QStringLiteral("ON AIR"), 0.04, 0.96, 0.03, 0.36, 0.30, on ? kRed : kDim);
	d.text(QString::fromStdString(s.stream_duration), 0.04, 0.96, 0.37, 0.51, 0.115, on ? kWhite : kDim);
	d.text(QStringLiteral("DATA RATE Mb/s"), 0.02, 0.58, 0.54, 0.62, 0.062, kCaption, false);
	d.text(QString::fromStdString(s.bitrate), 0.02, 0.58, 0.61, 0.79, 0.17, on ? kWhite : kDim);
	d.text(QStringLiteral("CACHE"), 0.60, 0.98, 0.54, 0.62, 0.062, kCaption, false);
	d.text(QString::fromStdString(s.cache), 0.60, 0.98, 0.61, 0.79, 0.17, s.cache == "HIGH" ? kRed : (on ? kWhite : kDim));
	if (on)
		d.text(QStringLiteral("DROPPED ") + QString::fromStdString(s.dropped_frames), 0.04, 0.96, 0.79, 0.88, 0.065,
		       kCaption, false);
	d.text(QString::fromStdString(s.service), 0.04, 0.96, 0.88, 0.99, 0.085, on ? kWhite : kDim, false);
}

void draw_rec(const Painter &d, const StatusSnapshot &s)
{
	const bool on = s.recording;
	d.text(QStringLiteral("REC"), 0.04, 0.96, 0.03, 0.36, 0.30, on ? kRed : kDim);
	d.text(QString::fromStdString(s.record_duration), 0.04, 0.96, 0.37, 0.51, 0.115,
	       s.recording_paused ? kAmber : (on ? kWhite : kDim));
	d.text(QStringLiteral("FILE SIZE"), 0.04, 0.96, 0.54, 0.62, 0.062, kCaption, false);
	d.text(QString::fromStdString(s.record_size), 0.04, 0.96, 0.61, 0.82, 0.17, on ? kWhite : kDim);
	d.text(QString::fromStdString(s.record_file), 0.04, 0.96, 0.86, 0.98, 0.075, on ? kCaption : kDim, false,
	       Qt::ElideMiddle);
}

void draw_clock(const Painter &d, const StatusSnapshot &s)
{
	d.text(QStringLiteral("CLOCK"), 0.04, 0.96, 0.07, 0.20, 0.075, kCaption, false);
	d.text(QString::fromStdString(s.clock), 0.03, 0.97, 0.26, 0.82, 0.27, kWhite);
}

void draw_mixer(const Painter &d)
{
	d.text(QStringLiteral("Mixer"), 0.04, 0.96, 0.30, 0.70, 0.14, kCaption, false);
}

} // namespace

std::shared_ptr<const StatusTileImage> render_status_tile(TileKind kind, const StatusSnapshot &snapshot, int w, int h)
{
	if (w <= 0 || h <= 0)
		return nullptr;
	QColor bg = kOffBackground;
	switch (kind) {
	case TileKind::OnAir:
		bg = snapshot.streaming ? kBlack : kOffBackground;
		break;
	case TileKind::Rec:
		bg = snapshot.recording ? kBlack : kOffBackground;
		break;
	case TileKind::Clock:
		bg = kBlack;
		break;
	case TileKind::Mixer:
		break;
	default:
		return nullptr;
	}

	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	img.fill(bg);
	{
		QPainter p(&img);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::TextAntialiasing);
		const Painter d(p, w, h);
		switch (kind) {
		case TileKind::OnAir:
			draw_on_air(d, snapshot);
			break;
		case TileKind::Rec:
			draw_rec(d, snapshot);
			break;
		case TileKind::Clock:
			draw_clock(d, snapshot);
			break;
		default:
			draw_mixer(d);
			break;
		}
	}

	/* Opaque: Format_ARGB32 is B, G, R, A in memory on little-endian hosts. */
	static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN, "status tiles are uploaded as GS_BGRA");
	img = img.convertToFormat(QImage::Format_ARGB32);

	auto out = std::make_shared<StatusTileImage>();
	out->w = w;
	out->h = h;
	const size_t row = size_t(w) * 4;
	out->pixels.resize(row * size_t(h));
	for (int y = 0; y < h; ++y)
		std::memcpy(out->pixels.data() + row * size_t(y), img.constScanLine(y), row);
	return out;
}
