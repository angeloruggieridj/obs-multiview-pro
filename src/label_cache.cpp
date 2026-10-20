// SPDX-License-Identifier: GPL-2.0-or-later
#include "label_cache.hpp"

#include <QFontMetrics>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

/* The layout (label size, elision) is measured with this font at a large pixel
 * size and scaled linearly, so it is the same at every window size. The glyphs
 * themselves are drawn later at the true device size (see render()). */
constexpr int kRefPixelSize = 256;

/* Native text source: color 0xFFFFFF, opacity 50 -> alpha 50 * 255 / 100. */
constexpr int kTextAlpha = 50 * 255 / 100;

/* Hinting of the device-size glyphs: vertical only keeps the baseline and the
 * stems of small text on pixel rows while the advances stay linear, so a label
 * is as wide as the layout measured it. */
constexpr QFont::HintingPreference kDeviceHinting = QFont::PreferVerticalHinting;

QFont make_label_font()
{
	/* Same faces as the native CreateLabel. */
#if defined(_WIN32)
	QFont f(QStringLiteral("Arial"));
	f.setStyleHint(QFont::SansSerif);
#elif defined(__APPLE__)
	QFont f(QStringLiteral("Helvetica"));
	f.setStyleHint(QFont::SansSerif);
#else
	QFont f(QStringLiteral("Monospace"));
	f.setStyleHint(QFont::TypeWriter);
#endif
	f.setBold(true);
	f.setStyleStrategy(QFont::StyleStrategy(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
	f.setPixelSize(kRefPixelSize);
	/* Only measured: hinting would add rounding noise to the scaled metrics. */
	f.setHintingPreference(QFont::PreferNoHinting);
	return f;
}

} // namespace

LabelCache::LabelCache() : m_font(make_label_font()) {}

std::shared_ptr<const LabelImage> LabelCache::get(const QString &text, int face_size, int max_cx,
						  const LabelScale &scale, float display_scale)
{
	if (text.isEmpty() || face_size <= 0 || max_cx <= 0)
		return nullptr;

	const Key key{text, face_size, max_cx, std::lround(double(scale.x) * display_scale * 10000.0),
		      std::lround(double(scale.y) * display_scale * 10000.0)};
	auto it = m_entries.find(key);
	if (it == m_entries.end())
		it = m_entries.emplace(key, Entry{render(text, face_size, max_cx, scale, display_scale), false}).first;
	it->second.used = true;
	return it->second.image;
}

void LabelCache::prune()
{
	for (auto it = m_entries.begin(); it != m_entries.end();) {
		if (!it->second.used) {
			it = m_entries.erase(it);
		} else {
			it->second.used = false;
			++it;
		}
	}
}

std::shared_ptr<const LabelImage> LabelCache::render(const QString &text, int face_size, int max_cx,
						     const LabelScale &scale, float display_scale)
{
	const QFontMetricsF fm(m_font);
	const qreal cell = fm.ascent() + fm.descent();
	if (cell <= 0.0)
		return nullptr;
	/* The native face size is the font cell height (LOGFONT lfHeight > 0). */
	const qreal k = qreal(face_size) / cell; /* label px per reference px */
	const QString space = QStringLiteral(" ");
	const qreal pad = fm.horizontalAdvance(space) * 2.0;

	/* Elide the name (not the padding spaces) until the even-rounded native
	 * width fits max_cx. */
	QString shown;
	LabelSize size = {0, 0};
	qreal avail = qreal(max_cx) / k - pad;
	for (int attempt = 0; attempt < 8 && avail > 0.0; ++attempt) {
		shown = fm.elidedText(text, Qt::ElideRight, avail);
		if (shown.isEmpty())
			return nullptr;
		const qreal w = fm.horizontalAdvance(space + shown + space) * k;
		size = label_text_size(float(w), float(fm.lineSpacing() * k), face_size);
		if (size.cx <= max_cx)
			break;
		avail -= 2.0 / k;
		size = {0, 0};
	}
	if (size.cx <= 0)
		return nullptr;

	const LabelRaster raster = label_raster(size, scale, display_scale);

	/* The glyphs are drawn at their final size: a hinted font whose pixel size is
	 * the on-screen glyph size, at whole-pixel positions, into a texture the
	 * renderer maps 1:1 onto device pixels. Nothing is scaled afterwards, so the
	 * stems stay on the pixel grid instead of being resampled from big outlines. */
	const QString padded = space + shown + space;
	int font_px = label_font_px(face_size, qreal(kRefPixelSize) / cell, scale.y * display_scale);
	const int min_px = std::max(1, font_px * 85 / 100);
	QFont dev_font = m_font;
	dev_font.setHintingPreference(kDeviceHinting);
	/* Hinted advances are not exactly the scaled reference ones: take a pixel
	 * off the size (down to 85%) before cutting letters from the name. */
	for (;; --font_px) {
		dev_font.setPixelSize(font_px);
		if (font_px <= min_px || QFontMetrics(dev_font).horizontalAdvance(padded) <= raster.w)
			break;
	}
	const QFontMetrics dm(dev_font);
	const int pad_px = dm.horizontalAdvance(space) * 2;

	QString drawn = shown;
	if (dm.horizontalAdvance(padded) > raster.w) {
		const QString cut = dm.elidedText(drawn, Qt::ElideRight, std::max(1, raster.w - pad_px));
		if (!cut.isEmpty())
			drawn = cut;
	}
	const QString line = space + drawn + space;
	/* Centered in the texture (the native box is centered on the same text), first
	 * baseline at the font ascent like the text source. */
	const int text_x = std::max(0, (raster.w - dm.horizontalAdvance(line)) / 2);
	const int baseline = dm.ascent();

	QImage img(raster.w, raster.h, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	{
		QPainter p(&img);
		p.setRenderHint(QPainter::TextAntialiasing);
		p.setFont(dev_font);
		p.setPen(Qt::white);
		/* Opacity instead of a translucent pen so colour emoji fade too. */
		p.setOpacity(kTextAlpha / 255.0);
		p.drawText(QPoint(text_x, baseline), line);
	}

	/* GS_BGRA with straight alpha, like the GDI+ ARGB bitmap of the text
	 * source. Format_ARGB32 is B, G, R, A in memory on little-endian hosts. */
	static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN, "label pixels are uploaded as GS_BGRA");
	img = img.convertToFormat(QImage::Format_ARGB32);

	auto out = std::make_shared<LabelImage>();
	out->size = size;
	out->tex_w = raster.w;
	out->tex_h = raster.h;
	const size_t row = size_t(raster.w) * 4;
	out->pixels.resize(row * size_t(raster.h));
	for (int y = 0; y < raster.h; ++y)
		std::memcpy(out->pixels.data() + row * size_t(y), img.constScanLine(y), row);
	return out;
}
