// SPDX-License-Identifier: GPL-2.0-or-later
#include "meter_scale.hpp"

#include "appearance.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>

std::shared_ptr<const MeterScaleImage> render_meter_scale(const MeterGeometry &g, int w, int h, int opacity_percent)
{
	if (w <= 0 || h <= 0 || g.scale.w <= 0.f || g.column.w <= 0.f || g.column.h <= 0.f)
		return nullptr;

	/* canvas px -> texels */
	const double kx = double(w) / double(g.column.w);
	const double ky = double(h) / double(g.column.h);

	/* Text height: a little under the spacing of two scale steps. */
	const double step = double(g.bar[0].h) * ky / 12.0; /* -5 dB */
	const int px = int(std::min(step * 0.85, double(g.scale.w) * kx * 0.42));
	if (px < 6)
		return nullptr;

	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	{
		QPainter p(&img);
		p.setRenderHint(QPainter::TextAntialiasing);
		if (opacity_percent < 100)
			p.setOpacity(double(clamp_percent(opacity_percent)) / 100.0);
		QFont f(
#if defined(_WIN32)
			QStringLiteral("Arial")
#elif defined(__APPLE__)
			QStringLiteral("Helvetica")
#else
			QStringLiteral("Sans Serif")
#endif
		);
		f.setStyleHint(QFont::SansSerif);
		/* Grayscale antialiasing: LCD fringes would be baked into the texture. */
		f.setStyleStrategy(QFont::StyleStrategy(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
		f.setPixelSize(px);
		f.setHintingPreference(QFont::PreferFullHinting);
		p.setFont(f);
		p.setPen(QColor(0xE6, 0xE6, 0xE6));
		const QFontMetricsF fm(f);

		const double sx = (double(g.scale.x) - double(g.column.x)) * kx;
		for (int db : kMeterScaleDb) {
			const double y = (double(meter_y_for_db(g, float(db))) - double(g.column.y)) * ky;
			const QRectF r(sx, y - fm.height() / 2.0, double(g.scale.w) * kx, fm.height());
			p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, QString::number(db));
		}

		if (g.captions.h > 0.f) {
			const char *names[2] = {"L", "R"};
			for (int i = 0; i < 2; ++i) {
				const QRectF r((double(g.bar[i].x) - double(g.column.x)) * kx,
					       (double(g.captions.y) - double(g.column.y)) * ky, double(g.bar[i].w) * kx,
					       double(g.captions.h) * ky);
				p.drawText(r, Qt::AlignCenter, QString::fromLatin1(names[i]));
			}
		}
	}

	static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN, "pixels are uploaded as GS_BGRA");
	img = img.convertToFormat(QImage::Format_ARGB32);

	auto out = std::make_shared<MeterScaleImage>();
	out->w = w;
	out->h = h;
	const size_t row = size_t(w) * 4;
	out->pixels.resize(row * size_t(h));
	for (int y = 0; y < h; ++y)
		std::memcpy(out->pixels.data() + row * size_t(y), img.constScanLine(y), row);
	return out;
}
