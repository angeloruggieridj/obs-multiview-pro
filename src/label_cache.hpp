// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "label_metrics.hpp"

#include <QFont>
#include <QString>

#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

/* One rendered label, immutable once built. Used on the UI thread only: the
 * renderer copies it into the atlas of a label batch. */
struct LabelImage {
	LabelSize size = {0, 0};      /* native label size, label px */
	int tex_w = 0;                /* texture size in texels */
	int tex_h = 0;
	std::vector<uint8_t> pixels;  /* tex_w * tex_h * 4: B, G, R, A, straight alpha */
};

/* Draws label text with Qt into images, the way the native label text source
 * looks (OBS 32.1 Multiview CreateLabel: " name ", bold Arial / Helvetica /
 * Monospace, cell height = face size, white at 50% opacity, no outline), elided
 * with "…" to fit. Qt picks fallback fonts for glyphs the face lacks (other
 * scripts, emoji).
 *
 * UI thread only (QFont / QPainter). Lookups are keyed by (text, face size,
 * max width, raster scale), so a label is redrawn only when one of them
 * changes. */
class LabelCache {
public:
	LabelCache();

	/* Image for `text` at native face size `face_size` (label_face_size),
	 * elided to `max_cx` label px, rasterized for `display_scale` display px
	 * per canvas px. nullptr for an empty text or when not even "…" fits. */
	std::shared_ptr<const LabelImage> get(const QString &text, int face_size, int max_cx, const LabelScale &scale,
					      float display_scale);

	/* Forgets every entry that get() did not return since the previous
	 * prune(). Images still held by snapshots stay alive until those go. */
	void prune();

private:
	using Key = std::tuple<QString, int, int, long, long>;
	struct Entry {
		std::shared_ptr<const LabelImage> image; /* may be null (nothing to show) */
		bool used = false;
	};

	std::shared_ptr<const LabelImage> render(const QString &text, int face_size, int max_cx,
						 const LabelScale &scale, float display_scale);

	QFont m_font; /* reference size: measuring only, glyphs use a device-size copy */
	std::map<Key, Entry> m_entries;
};
