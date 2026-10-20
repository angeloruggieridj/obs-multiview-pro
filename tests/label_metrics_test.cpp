// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "label_metrics.hpp"

/* Expected numbers worked out by hand from the native formulae (OBS 32.1
 * Multiview.cpp / obs-text.cpp) for a 1920x1080 canvas. */

TEST_CASE("layout_label_face_size_from_canvas_height")
{
	/* scenes: int((1080 / 3) / 9.81) = int(36.697) ; PVW/PGM: int(540 / 9.81) = int(55.045) */
	CHECK(label_face_size(1080, false) == 36);
	CHECK(label_face_size(1080, true) == 55);
	/* 720p: int(240 / 9.81) = 24, int(360 / 9.81) = 36 */
	CHECK(label_face_size(720, false) == 24);
	CHECK(label_face_size(720, true) == 36);
	/* integer division first: 1081 / 3 = 360 -> 36 */
	CHECK(label_face_size(1081, false) == 36);
	CHECK(label_face_size(0, false) == 0);
}

TEST_CASE("layout_label_text_size_like_text_source")
{
	/* Arial bold cell 36: line spacing ~37.05 -> 37 -> even 38 */
	LabelSize s = label_text_size(203.4f, 37.05f, 36);
	CHECK(s.cx == 204);
	CHECK(s.cy == 38);
	/* shorter than the face size: cy = face; even width stays */
	s = label_text_size(10.f, 20.f, 36);
	CHECK(s.cx == 10);
	CHECK(s.cy == 36);
	/* odd face is rounded up to even too */
	s = label_text_size(99.99999f, 20.f, 55);
	CHECK(s.cx == 100);
	CHECK(s.cy == 56);
	/* empty text: minimum 2 x 2 */
	s = label_text_size(0.f, 0.f, 0);
	CHECK(s.cx == 2);
	CHECK(s.cy == 2);
}

TEST_CASE("layout_label_scale_8_and_24")
{
	/* 8 layout: PVW 960x540 -> ppiScale (960-12)/1920, (540-12)/1080, n 4 */
	const LabelScale s8 = label_scale(1920.f, 1080.f, 960.f, 540.f);
	CHECK(s8.x == doctest::Approx(0.49375));
	CHECK(s8.y == doctest::Approx(528.0 / 1080.0));
	CHECK(s8.n == 4);
	/* 24 layout: PVW 640x360 -> (640-12)/1920, (360-12)/1080, n 6 */
	const LabelScale s24 = label_scale(1920.f, 1080.f, 640.f, 360.f);
	CHECK(s24.x == doctest::Approx(628.0 / 1920.0));
	CHECK(s24.y == doctest::Approx(348.0 / 1080.0));
	CHECK(s24.n == 6);
	/* degenerate canvas */
	const LabelScale z = label_scale(0.f, 0.f, 0.f, 0.f);
	CHECK(z.x == 0.f);
	CHECK(z.y == 0.f);
}

TEST_CASE("layout_label_offset_native")
{
	/* 8 layout scene tile 480 wide: 480/2 - uint32(204/4) = 240 - 51 */
	CHECK(label_offset(204, 4, 480.f) == 189.f);
	/* 24 layout scene tile 320 wide: 160 - uint32(204/6 = 34) */
	CHECK(label_offset(204, 6, 320.f) == 126.f);
	/* 24 layout: 203 / 6 = 33.8 -> 33 */
	CHECK(label_offset(203, 6, 320.f) == 127.f);
	/* tile width truncated before halving: uint32(481.9) / 2 = 240 */
	CHECK(label_offset(204, 4, 481.9f) == 189.f);
	/* too wide: the native wraps (unsigned); we clamp to 0 */
	CHECK(label_offset(2000, 4, 480.f) == 0.f);
	CHECK(label_offset(10, 0, 480.f) == 240.f);
}

TEST_CASE("layout_label_geometry_8_layout_scene_tile")
{
	/* 1920x1080, 8 layout, scene slot 0 = (0, 540, 480, 270), label 204x38 */
	const LabelScale s = label_scale(1920.f, 1080.f, 960.f, 540.f);
	const LabelGeometry g = label_geometry({0.f, 540.f, 480.f, 270.f}, {204, 38}, s);
	CHECK(g.box.x == doctest::Approx(189.0));
	/* 540 + 270 - 38 * 0.488889 - 18 */
	CHECK(g.box.y == doctest::Approx(810.0 - 38.0 * 528.0 / 1080.0 - 18.0));
	CHECK(g.box.y == doctest::Approx(773.4222).epsilon(1e-6));
	CHECK(g.box.w == doctest::Approx(204.0 * 0.49375));
	CHECK(g.box.h == doctest::Approx(50.0 * 528.0 / 1080.0));
	CHECK(g.text.x == doctest::Approx(189.0));
	CHECK(g.text.y == doctest::Approx(773.4222 + 6.0 * 528.0 / 1080.0).epsilon(1e-6));
	CHECK(g.text.w == doctest::Approx(204.0 * 0.49375));
	CHECK(g.text.h == doctest::Approx(38.0 * 528.0 / 1080.0));
}

TEST_CASE("layout_label_geometry_24_layout_scene_tile")
{
	/* 1920x1080, 24 layout, scene slot 7 = (320, 540, 320, 180), label 204x38 */
	const LabelScale s = label_scale(1920.f, 1080.f, 640.f, 360.f);
	const LabelGeometry g = label_geometry({320.f, 540.f, 320.f, 180.f}, {204, 38}, s);
	CHECK(g.box.x == doctest::Approx(320.0 + 126.0));
	/* 540 + 180 - 38 * 0.322222 - 18 = 689.7556 */
	CHECK(g.box.y == doctest::Approx(689.7556).epsilon(1e-6));
	CHECK(g.box.w == doctest::Approx(204.0 * 628.0 / 1920.0));
	CHECK(g.box.h == doctest::Approx(50.0 * 348.0 / 1080.0));
	CHECK(g.text.y == doctest::Approx(689.7556 + 6.0 * 348.0 / 1080.0).epsilon(1e-6));
}

TEST_CASE("layout_label_geometry_8_layout_preview_tile")
{
	/* PVW (0, 0, 960, 540), "Preview" label 220x56: offset 480 - 55 */
	const LabelScale s = label_scale(1920.f, 1080.f, 960.f, 540.f);
	const LabelGeometry g = label_geometry({0.f, 0.f, 960.f, 540.f}, {220, 56}, s);
	CHECK(g.box.x == doctest::Approx(425.0));
	/* labelY = pvwprgCY: 540 - 56 * 0.488889 - 18 = 494.6222 */
	CHECK(g.box.y == doctest::Approx(494.6222).epsilon(1e-6));
}

TEST_CASE("layout_label_max_cx_keeps_box_inside_outline")
{
	const LabelScale s8 = label_scale(1920.f, 1080.f, 960.f, 540.f);
	/* offset 240 - uint32(lw/4) >= 6 -> lw <= 939; right edge 6 + 939*0.49375 = 469.6 <= 474 */
	CHECK(label_max_cx(480.f, s8) == 939);
	const LabelScale s24 = label_scale(1920.f, 1080.f, 640.f, 360.f);
	/* 160 - uint32(lw/6) >= 6 -> lw <= 929; right edge 6 + 929*0.327 = 309.9 <= 314 */
	CHECK(label_max_cx(320.f, s24) == 929);

	/* property: every tile kind of every layout keeps the widest label inside */
	struct Case {
		float tile_cx;
		LabelScale s;
	};
	const Case cases[] = {{480.f, s8}, {960.f, s8}, {240.f, s8}, {320.f, s24}, {640.f, s24}};
	for (const Case &c : cases) {
		const int lw = label_max_cx(c.tile_cx, c.s);
		REQUIRE(lw > 0);
		const LabelGeometry g = label_geometry({0.f, 0.f, c.tile_cx, 300.f}, {lw, 38}, c.s);
		CHECK(g.box.x >= kLabelThickness);
		CHECK(g.box.x + g.box.w <= c.tile_cx - kLabelThickness);
		/* one more label px no longer fits */
		const LabelGeometry g2 = label_geometry({0.f, 0.f, c.tile_cx, 300.f}, {lw + 1, 38}, c.s);
		CHECK((g2.box.x < kLabelThickness || g2.box.x + g2.box.w > c.tile_cx - kLabelThickness));
	}
	/* tile too small for any label */
	CHECK(label_max_cx(12.f, s8) == 0);
	CHECK(label_max_cx(100.f, LabelScale{0.f, 0.f, 4}) == 0);
}

TEST_CASE("layout_label_raster_follows_display_scale")
{
	const LabelScale s8 = label_scale(1920.f, 1080.f, 960.f, 540.f);
	/* window 960x540 for a 1920x1080 canvas: 204 * 0.49375 * 0.5 = 50.36 -> 51,
	 * 38 * 0.488889 * 0.5 = 9.29 -> 10 */
	LabelRaster r = label_raster({204, 38}, s8, 0.5f);
	CHECK(r.w == 51);
	CHECK(r.h == 10);
	/* full screen 1920x1080: 100.725 -> 101, 18.58 -> 19 */
	r = label_raster({204, 38}, s8, 1.0f);
	CHECK(r.w == 101);
	CHECK(r.h == 19);
	/* clamps */
	r = label_raster({204, 38}, s8, 0.f);
	CHECK(r.w == 2);
	CHECK(r.h == 2);
	r = label_raster({20000, 38}, s8, 10.f);
	CHECK(r.w == 4096);
}
