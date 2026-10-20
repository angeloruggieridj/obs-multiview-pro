// SPDX-License-Identifier: GPL-2.0-or-later
#include "status_tiles.hpp"

#include "audio_meter_math.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
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
	/* Grayscale antialiasing: LCD fringes would be baked into the texture. */
	f.setStyleStrategy(QFont::StyleStrategy(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
	f.setBold(bold);
	f.setHintingPreference(QFont::PreferFullHinting);
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

void draw_mixer_placeholder(const Painter &d)
{
	d.text(QStringLiteral("Mixer"), 0.04, 0.96, 0.30, 0.70, 0.14, kCaption, false);
}

QColor value_color(MixerValueStyle style)
{
	auto from_argb = [](uint32_t c) { return QColor(int((c >> 16) & 0xFF), int((c >> 8) & 0xFF), int(c & 0xFF)); };
	switch (style) {
	case MixerValueStyle::Nominal:
		return from_argb(meter_color(MeterZone::Nominal, true));
	case MixerValueStyle::Warning:
		return from_argb(meter_color(MeterZone::Warning, true));
	case MixerValueStyle::Error:
		return from_argb(meter_color(MeterZone::Error, true));
	case MixerValueStyle::Mute:
		return kRed;
	case MixerValueStyle::Dim:
		break;
	}
	return QColor(0x80, 0x80, 0x80);
}

/* Names above and values below each column, in the rows of mixer_geometry
 * (computed here for the image's own size: the layout is proportional, so it
 * matches the bars drawn in canvas pixels). Font sizes: 80% of the row, the
 * name shortened to what fits (elide_name), the value shrunk to fit. */
void draw_mixer(QPainter &p, int w, int h, const std::vector<MixerColumnView> &cols)
{
	const std::vector<MixerColumnGeometry> geo = mixer_geometry({0.f, 0.f, float(w), float(h)}, cols.size());
	for (size_t i = 0; i < geo.size(); ++i) {
		const MixerColumnGeometry &g = geo[i];
		const MixerColumnView &col = cols[i];

		auto draw = [&](const RectF &r, const QString &text, const QColor &color, bool bold, bool elide) {
			if (text.isEmpty())
				return;
			int px = int(r.h * 0.8f);
			QFont f = make_font(bold);
			const QRect rect(int(std::lround(r.x)), int(std::lround(r.y)), std::max(1, int(std::lround(r.w))),
					 std::max(1, int(std::lround(r.h))));
			QString shown = text;
			if (elide) {
				/* Shrink the name down to 45% of the row to keep more of it, then cut
				 * in the middle so names that differ at the end stay apart. */
				const std::string full = text.toStdString();
				const int min_px = std::max(6, int(r.h * 0.45f));
				while (px > min_px && elide_name_middle(full, max_name_chars(r.w, float(px))) != full)
					--px;
				const size_t n = max_name_chars(r.w, float(px));
				shown = QString::fromStdString(elide_name_middle(full, n));
				f.setPixelSize(std::max(1, px));
				shown = QFontMetrics(f).elidedText(shown, Qt::ElideMiddle, rect.width()); /* exact guard */
			} else {
				for (; px > 6; --px) {
					f.setPixelSize(px);
					if (QFontMetrics(f).horizontalAdvance(shown) <= rect.width())
						break;
				}
				f.setPixelSize(std::max(1, px));
			}
			if (px < 6 || shown.isEmpty())
				return;
			p.setFont(f);
			p.setPen(color);
			p.drawText(rect, Qt::AlignCenter, shown);
		};
		draw(g.name, QString::fromStdString(col.name), QColor(0xE6, 0xE6, 0xE6), false, true);
		draw(g.value, QString::fromStdString(col.value), value_color(col.style), true, false);
	}
}

} // namespace

std::shared_ptr<const StatusTileImage> render_status_tile(TileKind kind, const StatusSnapshot &snapshot, int w, int h,
							  const std::vector<MixerColumnView> *mixer)
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
		if (mixer)
			bg = kBlack;
		break;
	default:
		return nullptr;
	}

	/* Everything is drawn on a transparent layer and put over the background
	 * afterwards: Qt only uses grayscale antialiasing for text on a surface that
	 * can hold alpha, and LCD fringes cannot be baked into a texture. */
	QImage layer(w, h, QImage::Format_ARGB32_Premultiplied);
	layer.fill(Qt::transparent);
	{
		QPainter p(&layer);
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
			if (mixer)
				draw_mixer(p, w, h, *mixer);
			else
				draw_mixer_placeholder(d);
			break;
		}
	}

	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	img.fill(bg);
	{
		QPainter p(&img);
		p.drawImage(0, 0, layer);
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
