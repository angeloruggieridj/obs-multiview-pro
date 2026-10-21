// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "mixer_columns.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
const std::string kEllipsis = "\xE2\x80\xA6"; /* U+2026 */
} // namespace

namespace {
using Ids = std::vector<MixerSourceId>;

MixerSourceLists lists(Ids devices, Ids program, Ids preview, bool studio)
{
	MixerSourceLists in;
	in.devices = std::move(devices);
	in.program = std::move(program);
	in.preview = std::move(preview);
	in.studio_mode = studio;
	return in;
}
} // namespace

TEST_CASE("mixer_select_columns_pgm_only_without_sources")
{
	const MixerSelection s = select_mixer_columns(MixerSourceLists());
	CHECK(s.columns == Ids{kMixerPgm});
	CHECK(s.dropped == 0);
	/* empty channels (0) are not sources */
	CHECK(select_mixer_columns(lists({0, 0, 0, 0, 0, 0}, {}, {}, true)).columns == Ids{kMixerPgm});
	CHECK(select_mixer_columns(lists({}, {}, {}, true)).columns == Ids{kMixerPgm});
}

TEST_CASE("mixer_select_columns_devices_in_channel_order_then_pgm_last")
{
	/* Desktop Audio on channel 1 and Mic/Aux on channel 3, a gap at 2 */
	CHECK(select_mixer_columns(lists({11, 0, 13, 0, 0, 0}, {}, {}, false)).columns == Ids{11, 13, kMixerPgm});
	CHECK(select_mixer_columns(lists({0, 0, 0, 0, 0, 16}, {}, {}, false)).columns == Ids{16, kMixerPgm});
}

TEST_CASE("mixer_select_columns_program_scene_sources_without_devices")
{
	/* The reported case: no global device, the audio is in the scene. */
	CHECK(select_mixer_columns(lists({}, {21, 22, 23}, {}, false)).columns == Ids{21, 22, 23, kMixerPgm});
	CHECK(select_mixer_columns(lists({0, 0}, {23, 21}, {}, true)).columns == Ids{23, 21, kMixerPgm});
}

TEST_CASE("mixer_select_columns_order_devices_program_preview")
{
	const MixerSelection s = select_mixer_columns(lists({11, 0, 12}, {21, 22}, {31, 32}, true));
	CHECK(s.columns == Ids{11, 12, 21, 22, 31, 32, kMixerPgm});
	CHECK(s.dropped == 0);
}

TEST_CASE("mixer_select_columns_preview_only_in_studio_mode")
{
	CHECK(select_mixer_columns(lists({11}, {21}, {31, 32}, false)).columns == Ids{11, 21, kMixerPgm});
	CHECK(select_mixer_columns(lists({11}, {21}, {31, 32}, true)).columns == Ids{11, 21, 31, 32, kMixerPgm});
	/* studio mode off: the preview list does not count for the cap either */
	CHECK(select_mixer_columns(lists({}, {}, {31, 32, 33, 34, 35, 36, 37, 38, 39}, false)).dropped == 0);
}

TEST_CASE("mixer_select_columns_deduplicated_across_categories")
{
	/* 21 is both a global device and in the program scene; 22 is in both
	 * scenes; 31 twice in the preview list: one column each, at the first place. */
	const MixerSelection s = select_mixer_columns(lists({21, 0, 11}, {22, 21, 23, 22}, {22, 31, 31, 23}, true));
	CHECK(s.columns == Ids{21, 11, 22, 23, 31, kMixerPgm});
	CHECK(s.dropped == 0);
	/* a source in the program scene and in the preview scene: one column */
	CHECK(select_mixer_columns(lists({}, {21}, {21}, true)).columns == Ids{21, kMixerPgm});
	/* the same device on two channels */
	CHECK(select_mixer_columns(lists({11, 11}, {}, {}, false)).columns == Ids{11, kMixerPgm});
	/* a source id equal to the PGM id is not a source */
	CHECK(select_mixer_columns(lists({kMixerPgm}, {kMixerPgm}, {kMixerPgm}, true)).columns == Ids{kMixerPgm});
}

TEST_CASE("mixer_select_columns_capped_last_ones_dropped")
{
	/* 3 devices + 4 program + 3 preview = 10 sources, 7 fit beside PGM */
	const MixerSelection s = select_mixer_columns(lists({11, 12, 13}, {21, 22, 23, 24}, {31, 32, 33}, true));
	REQUIRE(s.columns.size() == kMixerMaxColumns);
	CHECK(s.columns == Ids{11, 12, 13, 21, 22, 23, 24, kMixerPgm});
	CHECK(s.columns.back() == kMixerPgm); /* never dropped, always last */
	CHECK(s.dropped == 3);
	/* duplicates do not count as dropped */
	const MixerSelection d = select_mixer_columns(lists({11, 12, 13}, {21, 22, 23, 24, 11}, {31, 21, 32}, true));
	CHECK(d.dropped == 2);
	/* smaller caps */
	const MixerSourceLists in = lists({11, 12}, {21}, {31}, true);
	const MixerSelection three = select_mixer_columns(in, 3);
	CHECK(three.columns == Ids{11, 12, kMixerPgm});
	CHECK(three.dropped == 2);
	const MixerSelection one = select_mixer_columns(in, 1);
	CHECK(one.columns == Ids{kMixerPgm});
	CHECK(one.dropped == 4);
	const MixerSelection none = select_mixer_columns(in, 0);
	CHECK(none.columns.empty());
	CHECK(none.dropped == 4);
	/* exactly full: nothing dropped */
	const MixerSelection full = select_mixer_columns(lists({}, {1, 2, 3, 4, 5, 6, 7}, {}, false));
	CHECK(full.columns.size() == kMixerMaxColumns);
	CHECK(full.dropped == 0);
}

namespace {
/* `in` with the columns currently shown. */
MixerSourceLists with_previous(MixerSourceLists in, Ids previous)
{
	in.previous = std::move(previous);
	return in;
}
} // namespace

TEST_CASE("mixer_stable_columns_survive_a_program_preview_swap")
{
	/* studio mode: program P (21, 22), preview V (31, 32), device 11 */
	const MixerSelection first = select_mixer_columns(lists({11}, {21, 22}, {31, 32}, true));
	CHECK(first.columns == Ids{11, 21, 22, 31, 32, kMixerPgm});
	/* a cut: program and preview swap, every column stays where it was */
	const MixerSelection cut = select_mixer_columns(with_previous(lists({11}, {31, 32}, {21, 22}, true), first.columns));
	CHECK(cut.columns == first.columns);
	CHECK(cut.dropped == 0);
	/* and back */
	CHECK(select_mixer_columns(with_previous(lists({11}, {21, 22}, {31, 32}, true), cut.columns)).columns ==
	      first.columns);
}

TEST_CASE("mixer_stable_columns_new_sources_are_appended")
{
	/* 41 is new and comes first in the program scene: it goes after the existing columns */
	const MixerSelection s = select_mixer_columns(with_previous(lists({11}, {41, 21, 42}, {}, false), {11, 21, kMixerPgm}));
	CHECK(s.columns == Ids{11, 21, 41, 42, kMixerPgm});
	/* a new device too: appended, not put first */
	CHECK(select_mixer_columns(with_previous(lists({12, 11}, {21}, {}, false), {11, 21, kMixerPgm})).columns ==
	      Ids{11, 21, 12, kMixerPgm});
}

TEST_CASE("mixer_stable_columns_removal_compacts_keeping_the_order")
{
	const Ids before = {11, 21, 22, 31, 32, kMixerPgm};
	/* 22 left the program scene: the others keep their relative order, no gap */
	CHECK(select_mixer_columns(with_previous(lists({11}, {21}, {31, 32}, true), before)).columns ==
	      Ids{11, 21, 31, 32, kMixerPgm});
	/* studio mode off: the preview-only sources go */
	CHECK(select_mixer_columns(with_previous(lists({11}, {21, 22}, {31, 32}, false), before)).columns ==
	      Ids{11, 21, 22, kMixerPgm});
	/* everything gone: PGM alone */
	CHECK(select_mixer_columns(with_previous(lists({}, {}, {}, false), before)).columns == Ids{kMixerPgm});
}

TEST_CASE("mixer_stable_columns_cap_dedup_and_pgm_last")
{
	/* full (7 + PGM): a new source is dropped, the existing ones stay */
	const Ids full = {1, 2, 3, 4, 5, 6, 7, kMixerPgm};
	const MixerSelection s = select_mixer_columns(with_previous(lists({}, {9, 1, 2, 3, 4, 5, 6, 7}, {}, false), full));
	CHECK(s.columns == full);
	CHECK(s.dropped == 1);
	/* one of them goes: the waiting source takes the free place, at the end */
	const MixerSelection t = select_mixer_columns(with_previous(lists({}, {9, 1, 2, 4, 5, 6, 7}, {}, false), full));
	CHECK(t.columns == Ids{1, 2, 4, 5, 6, 7, 9, kMixerPgm});
	CHECK(t.dropped == 0);
	/* duplicates in the lists and in `previous`, PGM in the middle of `previous` */
	const MixerSelection d =
		select_mixer_columns(with_previous(lists({11, 11}, {21, 11}, {21}, true), {21, kMixerPgm, 21, 11}));
	CHECK(d.columns == Ids{21, 11, kMixerPgm});
	/* previous columns that are no longer listed anywhere are not kept */
	CHECK(select_mixer_columns(with_previous(lists({}, {}, {}, true), {5, 6})).columns == Ids{kMixerPgm});
	/* a smaller cap than the previous columns: the first ones stay */
	const MixerSelection c = select_mixer_columns(with_previous(lists({}, {1, 2, 3}, {}, false), {3, 2, 1}), 3);
	CHECK(c.columns == Ids{3, 2, kMixerPgm});
	CHECK(c.dropped == 1);
	CHECK(select_mixer_columns(with_previous(lists({}, {1}, {}, false), {1}), 0).columns.empty());
}

TEST_CASE("mixer_value_of_a_muted_source_is_the_mute_word")
{
	/* whatever the level, the tile writes the mute word and the bar is empty */
	CHECK(mixer_value(true, -6.f).style == MixerValueStyle::Mute);
	CHECK(mixer_value(true, -6.f).text.empty());
	CHECK(mixer_value(true, kMeterFloorDb).style == MixerValueStyle::Mute);
}

TEST_CASE("mixer_elide_name")
{
	CHECK(elide_name("Mic", 8) == "Mic");
	CHECK(elide_name("Mic/Aux", 7) == "Mic/Aux"); /* exactly fits */
	CHECK(elide_name("Desktop Audio", 8) == "Desktop" + kEllipsis);
	CHECK(elide_name("Desktop Audio", 9) == "Desktop" + kEllipsis); /* trailing space dropped */
	CHECK(elide_name("Mic/Aux 2", 5) == "Mic/" + kEllipsis);
	CHECK(elide_name("abc", 1) == kEllipsis);
	CHECK(elide_name("abc", 0).empty());
	CHECK(elide_name("", 4).empty());
}

TEST_CASE("mixer_elide_name_middle")
{
	CHECK(elide_name_middle("Mic", 8) == "Mic");
	CHECK(elide_name_middle("Mic/Aux", 7) == "Mic/Aux");
	CHECK(elide_name_middle("Desktop Audio", 4) == "De" + kEllipsis + "o");
	CHECK(elide_name_middle("Desktop Audio 2", 4) == "De" + kEllipsis + "2");
	CHECK(elide_name_middle("Desktop Audio", 4) != elide_name_middle("Desktop Audio 2", 4));
	CHECK(elide_name_middle("Desktop Audio", 8) == "Desk" + kEllipsis + "dio");
	CHECK(elide_name_middle("Mic/Aux 2", 3) == "M" + kEllipsis + "2");
	CHECK(elide_name_middle("abcd\xC3", 4) == "ab" + kEllipsis + "\xC3");
	CHECK(elide_name_middle("abc", 1) == kEllipsis);
	CHECK(elide_name_middle("abc", 0).empty());
	CHECK(elide_name_middle("", 4).empty());
	/* spaces next to the ellipsis are dropped */
	CHECK(elide_name_middle("ab cd ef", 6) == "ab" + kEllipsis + "ef");
	CHECK(elide_name_middle("ab  cd", 4) == "ab" + kEllipsis + "d");
}

TEST_CASE("mixer_elide_name_middle_counts_code_points")
{
	const std::string name = "\xC3\xA9t\xC3\xA9 long \xC3\xA9t\xC3\xA9"; /* accented "ete long ete" */
	const std::string cut = elide_name_middle(name, 5);
	CHECK(cut == "\xC3\xA9t" + kEllipsis + "t\xC3\xA9");
	/* never cuts inside a sequence, even for a truncated tail */
	CHECK(elide_name_middle("abcd\xC3", 4) == "ab" + kEllipsis + "\xC3");
}

TEST_CASE("mixer_elide_name_counts_code_points_not_bytes")
{
	/* "Micre" with e acute: 5 code points, 6 bytes */
	const std::string name = "Micr\xC3\xA9";
	CHECK(elide_name(name, 5) == name);
	CHECK(elide_name(name, 4) == "Mic" + kEllipsis);
	/* never cuts inside a sequence, even for a truncated tail */
	CHECK(elide_name("ab\xC3", 5) == "ab\xC3");
}

TEST_CASE("mixer_max_name_chars")
{
	CHECK(max_name_chars(55.f, 10.f) == 10); /* 55 / 5.5 */
	CHECK(max_name_chars(100.f, 10.f) > max_name_chars(50.f, 10.f));
	CHECK(max_name_chars(50.f, 20.f) < max_name_chars(50.f, 10.f));
	CHECK(max_name_chars(0.f, 10.f) == 0);
	CHECK(max_name_chars(-5.f, 10.f) == 0);
	CHECK(max_name_chars(50.f, 0.f) == 0);
	CHECK(max_name_chars(kNan, 10.f) == 0);
}

TEST_CASE("mixer_format_db_text")
{
	CHECK(format_db_text(-23.04f) == "-23.0");
	CHECK(format_db_text(-23.06f) == "-23.1");
	CHECK(format_db_text(-6.f) == "-6.0");
	CHECK(format_db_text(-59.94f) == "-59.9");
	CHECK(format_db_text(0.f) == "0.0");
	CHECK(format_db_text(-0.02f) == "0.0"); /* never "-0.0" */
	CHECK(format_db_text(3.f) == "0.0");    /* clipping reads 0 */
	/* silence */
	CHECK(format_db_text(kMeterFloorDb) == "--");
	CHECK(format_db_text(-100.f) == "--");
	CHECK(format_db_text(-kInf) == "--");
	CHECK(format_db_text(kNan) == "--");
	CHECK(format_db_text(-89.f) == "-89.0");
}

TEST_CASE("mixer_value_text_and_style")
{
	CHECK(mixer_value(false, -30.f).style == MixerValueStyle::Nominal);
	CHECK(mixer_value(false, -20.f).style == MixerValueStyle::Warning);
	CHECK(mixer_value(false, -10.f).style == MixerValueStyle::Warning);
	CHECK(mixer_value(false, -9.f).style == MixerValueStyle::Error);
	CHECK(mixer_value(false, 0.f).style == MixerValueStyle::Error);
	CHECK(mixer_value(false, -kInf).style == MixerValueStyle::Dim);
	CHECK(mixer_value(false, -kInf).text == "--");
	CHECK(mixer_value(false, -12.34f).text == "-12.3");
	/* muted wins over any level; the word comes from the locale (no text here) */
	CHECK(mixer_value(true, -6.f).text.empty());
	CHECK(mixer_value(true, -6.f).style == MixerValueStyle::Mute);
	CHECK(mixer_value(true, -kInf).text.empty());
	CHECK(mixer_value(true, -kInf).style == MixerValueStyle::Mute);
}

TEST_CASE("mixer_db_text_update_rule")
{
	DbTextState s;
	/* nothing shown yet: always due */
	CHECK(db_text_update_due(s, -20.f, 0));

	s = {true, -20.f, 1000};
	/* too soon, whatever the change */
	CHECK_FALSE(db_text_update_due(s, -6.f, 1000));
	CHECK_FALSE(db_text_update_due(s, -6.f, 1499));
	/* time is there, change is not */
	CHECK_FALSE(db_text_update_due(s, -20.f, 5000));
	CHECK_FALSE(db_text_update_due(s, -20.4f, 5000)); /* rounds to the same dB */
	CHECK_FALSE(db_text_update_due(s, -19.6f, 5000));
	/* both */
	CHECK(db_text_update_due(s, -19.f, 1500));
	CHECK(db_text_update_due(s, -21.f, 1500));
	CHECK(db_text_update_due(s, -6.f, 9999));
	/* >= 1 dB on the real difference: -20 -> -19 */
	CHECK(db_text_update_due(s, -18.6f, 2000));
	/* rounded values differ by 1 (-21 / -20) but the real difference is 0.91 */
	const DbTextState r = {true, -20.51f, 0};
	CHECK_FALSE(db_text_update_due(r, -19.6f, 5000));
	CHECK(db_text_update_due(r, -19.5f, 5000));
	/* a clock that went backwards never makes it due */
	CHECK_FALSE(db_text_update_due(s, -6.f, 400));
}

TEST_CASE("mixer_db_text_update_rule_silence")
{
	DbTextState s = {true, -20.f, 0};
	CHECK(db_text_update_due(s, -kInf, 600));
	CHECK(db_text_update_due(s, kNan, 600));
	s = {true, kMeterFloorDb, 0};
	CHECK(db_text_update_due(s, -20.f, 600));
	CHECK_FALSE(db_text_update_due(s, -kInf, 600));
	CHECK_FALSE(db_text_update_due(s, -90.4f, 600));
	/* "--" <-> a value, however small the difference */
	s = {true, -89.8f, 0};
	CHECK(db_text_update_due(s, -kInf, 600));
	s = {true, kMeterFloorDb, 0};
	CHECK(db_text_update_due(s, -89.8f, 600));
}

TEST_CASE("mixer_geometry_columns")
{
	const RectF inner = {100.f, 50.f, 480.f, 270.f};
	const std::vector<MixerColumnGeometry> g = mixer_geometry(inner, 8);
	REQUIRE(g.size() == 8);
	for (size_t i = 0; i < g.size(); ++i) {
		const MixerColumnGeometry &c = g[i];
		/* everything inside the tile */
		for (const RectF *r : {&c.cell, &c.name, &c.value, &c.bar[0], &c.bar[1]}) {
			CHECK(r->x >= inner.x);
			CHECK(r->y >= inner.y);
			CHECK(r->x + r->w <= inner.x + inner.w + 0.001f);
			CHECK(r->y + r->h <= inner.y + inner.h + 0.001f);
			CHECK(r->w > 0.f);
			CHECK(r->h > 0.f);
		}
		/* name above the bars above the value */
		CHECK(c.name.y + c.name.h <= c.bar[0].y);
		CHECK(c.bar[0].y + c.bar[0].h <= c.value.y);
		/* two equal bars side by side, inside the cell, L left of R */
		CHECK(c.bar[0].w == doctest::Approx(c.bar[1].w));
		CHECK(c.bar[0].h == doctest::Approx(c.bar[1].h));
		CHECK(c.bar[0].x + c.bar[0].w <= c.bar[1].x);
		CHECK(c.bar[0].x >= c.cell.x);
		CHECK(c.bar[1].x + c.bar[1].w <= c.cell.x + c.cell.w + 0.001f);
		if (i > 0) {
			CHECK(c.cell.w == doctest::Approx(g[0].cell.w));
			CHECK(c.cell.x >= g[i - 1].cell.x + g[i - 1].cell.w - 0.001f); /* no overlap */
			CHECK(c.bar[0].h == doctest::Approx(g[0].bar[0].h));
		}
	}
}

TEST_CASE("mixer_geometry_is_proportional")
{
	/* The text image is built for the tile in device pixels, the bars drawn in
	 * canvas pixels: the layout must scale exactly. */
	const auto a = mixer_geometry({0.f, 0.f, 480.f, 270.f}, 5);
	const auto b = mixer_geometry({0.f, 0.f, 960.f, 540.f}, 5);
	REQUIRE(a.size() == 5);
	REQUIRE(b.size() == 5);
	for (size_t i = 0; i < a.size(); ++i) {
		CHECK(b[i].bar[1].x == doctest::Approx(a[i].bar[1].x * 2.f));
		CHECK(b[i].bar[0].y == doctest::Approx(a[i].bar[0].y * 2.f));
		CHECK(b[i].bar[0].h == doctest::Approx(a[i].bar[0].h * 2.f));
		CHECK(b[i].value.y == doctest::Approx(a[i].value.y * 2.f));
		CHECK(b[i].name.w == doctest::Approx(a[i].name.w * 2.f));
	}
}

TEST_CASE("mixer_geometry_degenerate")
{
	CHECK(mixer_geometry({0.f, 0.f, 480.f, 270.f}, 0).empty());
	CHECK(mixer_geometry({0.f, 0.f, 0.f, 270.f}, 3).empty());
	CHECK(mixer_geometry({0.f, 0.f, 480.f, -1.f}, 3).empty());
	/* more columns than the cap are not laid out */
	CHECK(mixer_geometry({0.f, 0.f, 480.f, 270.f}, 20).size() == kMixerMaxColumns);
	/* one column: bars not wider than the cell */
	const auto one = mixer_geometry({0.f, 0.f, 480.f, 270.f}, 1);
	REQUIRE(one.size() == 1);
	CHECK(one[0].bar[1].x + one[0].bar[1].w <= one[0].cell.x + one[0].cell.w);
}
