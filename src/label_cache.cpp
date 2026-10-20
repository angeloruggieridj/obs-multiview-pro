// SPDX-License-Identifier: GPL-2.0-or-later
#include "label_cache.hpp"

#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QtGlobal>

#include <cmath>
#include <cstring>

namespace {

/* Glyphs are measured and drawn at this pixel size and scaled to the target:
 * metrics then scale linearly and a label looks the same at every size. */
constexpr int kRefPixelSize = 256;

/* Native text source: color 0xFFFFFF, opacity 50 -> alpha 50 * 255 / 100. */
constexpr int kTextAlpha = 50 * 255 / 100;

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
	f.setPixelSize(kRefPixelSize);
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

	QImage img(raster.w, raster.h, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	{
		QPainter p(&img);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::TextAntialiasing);
		/* texels <- label px <- reference px */
		p.scale(qreal(raster.w) / size.cx, qreal(raster.h) / size.cy);
		p.scale(k, k);
		p.setFont(m_font);
		p.setPen(Qt::white);
		/* Opacity instead of a translucent pen so colour emoji fade too. */
		p.setOpacity(kTextAlpha / 255.0);
		/* Top-aligned line as the text source (first baseline at the ascent). */
		p.drawText(QPointF(0.0, fm.ascent()), space + shown + space);
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
