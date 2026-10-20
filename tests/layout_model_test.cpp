#include "doctest.h"

#include "layout_model.hpp"

#include <cmath>
#include <vector>

namespace {
constexpr float kEps = 1e-5f;

int count_kind(const std::vector<Tile> &tiles, TileKind kind)
{
	int n = 0;
	for (const Tile &t : tiles)
		if (t.kind == kind)
			++n;
	return n;
}

bool overlap(const RectF &a, const RectF &b)
{
	const bool sep_x = a.x + a.w <= b.x + kEps || b.x + b.w <= a.x + kEps;
	const bool sep_y = a.y + a.h <= b.y + kEps || b.y + b.h <= a.y + kEps;
	return !(sep_x || sep_y);
}

const Preset kAllPresets[] = {Preset::Scenes4, Preset::Scenes8, Preset::Scenes16, Preset::Scenes24,
			      Preset::Scenes32};
} // namespace

TEST_CASE("layout_scenes8_has_one_pvw_one_pgm_eight_scenes")
{
	const auto tiles = build_layout(Preset::Scenes8);
	CHECK(count_kind(tiles, TileKind::Preview) == 1);
	CHECK(count_kind(tiles, TileKind::Program) == 1);
	CHECK(count_kind(tiles, TileKind::Scene) == 8);
	for (const Tile &t : tiles)
		CHECK((t.kind == TileKind::Scene) == (t.scene_slot >= 0));
}

TEST_CASE("layout_scene_counts_per_preset")
{
	CHECK(count_kind(build_layout(Preset::Scenes4), TileKind::Scene) == 4);
	CHECK(count_kind(build_layout(Preset::Scenes8), TileKind::Scene) == 8);
	CHECK(count_kind(build_layout(Preset::Scenes16), TileKind::Scene) == 16);
	CHECK(count_kind(build_layout(Preset::Scenes24), TileKind::Scene) == 24);
	CHECK(count_kind(build_layout(Preset::Scenes32), TileKind::Scene) == 32);
}

TEST_CASE("layout_scene_slots_are_sequential")
{
	for (Preset p : kAllPresets) {
		const auto tiles = build_layout(p);
		int expected = 0;
		for (const Tile &t : tiles) {
			if (t.kind == TileKind::Scene) {
				CHECK(t.scene_slot == expected);
				++expected;
			} else {
				CHECK(t.scene_slot == -1);
			}
		}
	}
}

TEST_CASE("layout_no_overlap_and_inside_unit_square")
{
	for (Preset p : kAllPresets) {
		const auto tiles = build_layout(p);
		for (size_t i = 0; i < tiles.size(); ++i) {
			const RectF &r = tiles[i].rect;
			CHECK(r.w > 0.f);
			CHECK(r.h > 0.f);
			CHECK(r.x >= -kEps);
			CHECK(r.y >= -kEps);
			CHECK(r.x + r.w <= 1.f + kEps);
			CHECK(r.y + r.h <= 1.f + kEps);
			for (size_t j = i + 1; j < tiles.size(); ++j)
				CHECK_FALSE(overlap(r, tiles[j].rect));
		}
	}
}

TEST_CASE("layout_scenes8_matches_native_horizontal_top_8")
{
	const auto tiles = build_layout(Preset::Scenes8);
	REQUIRE(tiles.size() == 10);
	for (const Tile &t : tiles) {
		switch (t.kind) {
		case TileKind::Preview:
			CHECK(t.rect.x == doctest::Approx(0.f));
			CHECK(t.rect.y == doctest::Approx(0.f));
			CHECK(t.rect.w == doctest::Approx(0.5f));
			CHECK(t.rect.h == doctest::Approx(0.5f));
			break;
		case TileKind::Program:
			CHECK(t.rect.x == doctest::Approx(0.5f));
			CHECK(t.rect.y == doctest::Approx(0.f));
			CHECK(t.rect.w == doctest::Approx(0.5f));
			CHECK(t.rect.h == doctest::Approx(0.5f));
			break;
		case TileKind::Scene: {
			const int i = t.scene_slot;
			CHECK(t.rect.x == doctest::Approx((i % 4) * 0.25f));
			CHECK(t.rect.y == doctest::Approx(0.5f + (i / 4) * 0.25f));
			CHECK(t.rect.w == doctest::Approx(0.25f));
			CHECK(t.rect.h == doctest::Approx(0.25f));
			break;
		}
		}
	}
}

TEST_CASE("layout_scenes24_matches_native_horizontal_top_24")
{
	const auto tiles = build_layout(Preset::Scenes24);
	REQUIRE(tiles.size() == 26);
	const float third = 1.f / 3.f;
	const float sixth = 1.f / 6.f;
	for (const Tile &t : tiles) {
		switch (t.kind) {
		case TileKind::Preview:
			CHECK(t.rect.x == doctest::Approx(sixth));
			CHECK(t.rect.y == doctest::Approx(0.f));
			CHECK(t.rect.w == doctest::Approx(third));
			CHECK(t.rect.h == doctest::Approx(third));
			break;
		case TileKind::Program:
			CHECK(t.rect.x == doctest::Approx(0.5f));
			CHECK(t.rect.y == doctest::Approx(0.f));
			CHECK(t.rect.w == doctest::Approx(third));
			CHECK(t.rect.h == doctest::Approx(third));
			break;
		case TileKind::Scene: {
			const int i = t.scene_slot;
			CHECK(t.rect.x == doctest::Approx((i % 6) * sixth));
			CHECK(t.rect.y == doctest::Approx(third + (i / 6) * sixth));
			CHECK(t.rect.w == doctest::Approx(sixth));
			CHECK(t.rect.h == doctest::Approx(sixth));
			break;
		}
		}
	}
}

TEST_CASE("layout_hit_test_inside_tile_3")
{
	const auto tiles = build_layout(Preset::Scenes8);
	REQUIRE(tiles.size() > 3);
	const RectF &r3 = tiles[3].rect;
	CHECK(hit_test(tiles, r3.x + r3.w / 2, r3.y + r3.h / 2) == 3);
}

TEST_CASE("layout_hit_test_outside_returns_minus_one")
{
	const auto tiles = build_layout(Preset::Scenes4); // bottom quarter unused
	CHECK(hit_test(tiles, 0.5f, 0.95f) == -1);
	CHECK(hit_test(tiles, -0.1f, 0.5f) == -1);
	CHECK(hit_test(tiles, 1.5f, 0.5f) == -1);
	CHECK(hit_test(tiles, 0.5f, -0.2f) == -1);
	CHECK(hit_test({}, 0.5f, 0.5f) == -1);
}

TEST_CASE("layout_hit_test_shared_edge_returns_single_tile")
{
	const auto tiles = build_layout(Preset::Scenes8);
	// x = 0.5 is shared by Preview and Program: the right tile owns the edge.
	const int a = hit_test(tiles, 0.5f, 0.25f);
	REQUIRE(a >= 0);
	CHECK(tiles[a].kind == TileKind::Program);
	// y = 0.5 is shared by the top row and the scenes: the lower tile owns it.
	const int b = hit_test(tiles, 0.25f, 0.5f);
	REQUIRE(b >= 0);
	CHECK(tiles[b].kind == TileKind::Scene);
	// The top-left corner of every tile belongs to exactly one tile.
	for (const Tile &t : tiles) {
		int hits = 0;
		for (const Tile &o : tiles) {
			const RectF &r = o.rect;
			if (t.rect.x >= r.x && t.rect.x < r.x + r.w && t.rect.y >= r.y && t.rect.y < r.y + r.h)
				++hits;
		}
		CHECK(hits == 1);
		CHECK(hit_test(tiles, t.rect.x, t.rect.y) >= 0);
	}
}

TEST_CASE("layout_fit_rect_16_9_in_square")
{
	const RectF r = fit_rect(16.f / 9, 1000, 1000);
	CHECK(r.x == doctest::Approx(0.f));
	CHECK(r.w == doctest::Approx(1000.f));
	CHECK(r.h == doctest::Approx(562.5f));
	CHECK(r.y == doctest::Approx((1000.f - 562.5f) / 2));
}

TEST_CASE("layout_fit_rect_pillarbox_and_degenerate")
{
	const RectF r = fit_rect(16.f / 9, 2000, 500);
	CHECK(r.h == doctest::Approx(500.f));
	CHECK(r.w == doctest::Approx(500.f * 16.f / 9));
	CHECK(r.y == doctest::Approx(0.f));
	CHECK(r.x == doctest::Approx((2000.f - r.w) / 2));

	const RectF z = fit_rect(16.f / 9, 0, 0);
	CHECK(z.w == doctest::Approx(0.f));
	CHECK(z.h == doctest::Approx(0.f));
}

namespace {
bool rect_eq(const RectF &a, const RectF &b)
{
	return std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f && std::abs(a.w - b.w) < 1e-4f &&
	       std::abs(a.h - b.h) < 1e-4f;
}

float area(const RectF &r)
{
	return r.w * r.h;
}
} // namespace

TEST_CASE("layout_uncovered_scenes8_and_scenes32_are_full")
{
	CHECK(uncovered_areas(build_layout(Preset::Scenes8)).empty());
	CHECK(uncovered_areas(build_layout(Preset::Scenes32)).empty());
}

TEST_CASE("layout_uncovered_scenes24_matches_native_side_regions")
{
	/* Native HORIZONTAL_TOP_24_SCENES paints the two areas beside PVW/PGM. */
	const auto fillers = uncovered_areas(build_layout(Preset::Scenes24));
	REQUIRE(fillers.size() == 2);
	CHECK(rect_eq(fillers[0], {0.f, 0.f, 1.f / 6, 1.f / 3}));
	CHECK(rect_eq(fillers[1], {5.f / 6, 0.f, 1.f / 6, 1.f / 3}));
}

TEST_CASE("layout_uncovered_scenes4_is_bottom_strip")
{
	const auto fillers = uncovered_areas(build_layout(Preset::Scenes4));
	REQUIRE(fillers.size() == 1);
	CHECK(rect_eq(fillers[0], {0.f, 0.75f, 1.f, 0.25f}));
}

TEST_CASE("layout_uncovered_areas_complete_the_canvas_without_overlap")
{
	for (Preset p : kAllPresets) {
		const auto tiles = build_layout(p);
		const auto fillers = uncovered_areas(tiles);
		float total = 0.f;
		for (const Tile &t : tiles)
			total += area(t.rect);
		for (const RectF &f : fillers) {
			total += area(f);
			for (const Tile &t : tiles)
				CHECK_FALSE(overlap(f, t.rect));
		}
		CHECK(std::abs(total - 1.f) < 1e-4f);
	}
}

TEST_CASE("layout_pixel_rect_maps_full_canvas_and_halves")
{
	const PixelRect full = to_pixel_rect(RectF{0.f, 0.f, 1.f, 1.f}, 160, 90);
	CHECK(full.x == 0);
	CHECK(full.y == 0);
	CHECK(full.w == 160);
	CHECK(full.h == 90);

	const PixelRect right = to_pixel_rect(RectF{0.5f, 0.5f, 0.5f, 0.5f}, 160, 90);
	CHECK(right.x == 80);
	CHECK(right.y == 45);
	CHECK(right.w == 80);
	CHECK(right.h == 45);
}

TEST_CASE("layout_pixel_rect_adjacent_tiles_share_edges_without_gaps")
{
	/* Edges are rounded, not sizes: neighbours never leave a gap or overlap. */
	for (Preset p : kAllPresets) {
		const auto tiles = build_layout(p);
		for (const Tile &a : tiles) {
			const PixelRect pa = to_pixel_rect(a.rect, 97, 55);
			CHECK(pa.w >= 0);
			CHECK(pa.h >= 0);
			CHECK(pa.x >= 0);
			CHECK(pa.x + pa.w <= 97);
			CHECK(pa.y + pa.h <= 55);
		}
		for (size_t i = 0; i < tiles.size(); ++i)
			for (size_t j = i + 1; j < tiles.size(); ++j) {
				const PixelRect a = to_pixel_rect(tiles[i].rect, 97, 55);
				const PixelRect b = to_pixel_rect(tiles[j].rect, 97, 55);
				const bool sep_x = a.x + a.w <= b.x || b.x + b.w <= a.x;
				const bool sep_y = a.y + a.h <= b.y || b.y + b.h <= a.y;
				CHECK((sep_x || sep_y));
			}
	}
}

namespace {
const Preset kStatusPresets[] = {Preset::Scenes8Status, Preset::Scenes16Status, Preset::Scenes24Status,
				 Preset::Scenes32Status};

bool is_status_kind(TileKind k)
{
	return k == TileKind::OnAir || k == TileKind::Rec || k == TileKind::Clock || k == TileKind::Mixer;
}
} // namespace

TEST_CASE("layout_status_presets_scene_and_status_counts")
{
	const int scenes[] = {4, 12, 20, 28};
	for (int i = 0; i < 4; ++i) {
		const auto tiles = build_layout(kStatusPresets[i]);
		CHECK(count_kind(tiles, TileKind::Preview) == 1);
		CHECK(count_kind(tiles, TileKind::Program) == 1);
		CHECK(count_kind(tiles, TileKind::Scene) == scenes[i]);
		CHECK(count_kind(tiles, TileKind::OnAir) == 1);
		CHECK(count_kind(tiles, TileKind::Rec) == 1);
		CHECK(count_kind(tiles, TileKind::Clock) == 1);
		CHECK(count_kind(tiles, TileKind::Mixer) == 1);
		CHECK(tiles.size() == size_t(scenes[i] + 6));
	}
}

TEST_CASE("layout_status_presets_scene_slots_sequential_and_status_has_none")
{
	for (Preset p : kStatusPresets) {
		int expected = 0;
		for (const Tile &t : build_layout(p)) {
			if (t.kind == TileKind::Scene)
				CHECK(t.scene_slot == expected++);
			else
				CHECK(t.scene_slot == -1);
		}
	}
}

TEST_CASE("layout_status_presets_take_the_last_four_grid_cells_in_order")
{
	for (size_t i = 0; i < 4; ++i) {
		const Preset status = kStatusPresets[i];
		const Preset plain = kAllPresets[i + 1]; /* Scenes8..Scenes32 */
		const auto s = build_layout(status);
		const auto p = build_layout(plain);
		REQUIRE(s.size() == p.size());
		/* Same geometry tile for tile; only the kind of the last four changes. */
		const size_t n = p.size();
		for (size_t k = 0; k < n; ++k)
			CHECK(rect_eq(s[k].rect, p[k].rect));
		for (size_t k = 0; k + 4 < n; ++k)
			CHECK(s[k].kind == p[k].kind);
		CHECK(s[n - 4].kind == TileKind::OnAir);
		CHECK(s[n - 3].kind == TileKind::Rec);
		CHECK(s[n - 2].kind == TileKind::Clock);
		CHECK(s[n - 1].kind == TileKind::Mixer);
	}
}

TEST_CASE("layout_status_presets_no_overlap_and_complete_the_canvas")
{
	for (Preset p : kStatusPresets) {
		const auto tiles = build_layout(p);
		const auto fillers = uncovered_areas(tiles);
		float total = 0.f;
		for (size_t i = 0; i < tiles.size(); ++i) {
			total += area(tiles[i].rect);
			for (size_t j = i + 1; j < tiles.size(); ++j)
				CHECK_FALSE(overlap(tiles[i].rect, tiles[j].rect));
		}
		for (const RectF &f : fillers) {
			total += area(f);
			for (const Tile &t : tiles)
				CHECK_FALSE(overlap(f, t.rect));
		}
		CHECK(std::abs(total - 1.f) < 1e-4f);
	}
}

TEST_CASE("layout_status_presets_have_the_same_fillers_as_their_plain_family")
{
	for (size_t i = 0; i < 4; ++i) {
		const auto a = uncovered_areas(build_layout(kStatusPresets[i]));
		const auto b = uncovered_areas(build_layout(kAllPresets[i + 1]));
		REQUIRE(a.size() == b.size());
		for (size_t k = 0; k < a.size(); ++k)
			CHECK(rect_eq(a[k], b[k]));
	}
}

TEST_CASE("layout_hit_test_on_status_tiles_never_returns_a_scene_tile")
{
	for (Preset p : kStatusPresets) {
		const auto tiles = build_layout(p);
		for (const Tile &t : tiles) {
			if (!is_status_kind(t.kind))
				continue;
			const int idx = hit_test(tiles, t.rect.x + t.rect.w / 2, t.rect.y + t.rect.h / 2);
			REQUIRE(idx >= 0);
			CHECK(tiles[size_t(idx)].kind == t.kind);
			CHECK(tiles[size_t(idx)].kind != TileKind::Scene);
			CHECK(tiles[size_t(idx)].scene_slot == -1);
		}
	}
}

TEST_CASE("layout_plain_presets_are_not_status_and_the_family_helpers_map")
{
	for (Preset p : kAllPresets)
		CHECK_FALSE(preset_has_status(p));
	for (Preset p : kStatusPresets)
		CHECK(preset_has_status(p));
	CHECK(preset_with_status(Preset::Scenes8) == Preset::Scenes8Status);
	CHECK(preset_with_status(Preset::Scenes16) == Preset::Scenes16Status);
	CHECK(preset_with_status(Preset::Scenes24) == Preset::Scenes24Status);
	CHECK(preset_with_status(Preset::Scenes32) == Preset::Scenes32Status);
	CHECK(preset_with_status(Preset::Scenes4) == Preset::Scenes4); /* no 4-tile status family */
	CHECK(preset_with_status(Preset::Scenes24Status) == Preset::Scenes24Status);
	CHECK(preset_without_status(Preset::Scenes8Status) == Preset::Scenes8);
	CHECK(preset_without_status(Preset::Scenes32Status) == Preset::Scenes32);
	CHECK(preset_without_status(Preset::Scenes16) == Preset::Scenes16);
}
