// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "update_logic.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

static bool valid(const std::string &text)
{
	SemVersion v;
	return parse_version(text, v);
}

static SemVersion ver(const std::string &text)
{
	SemVersion v;
	REQUIRE(parse_version(text, v));
	return v;
}

static int cmp(const std::string &a, const std::string &b)
{
	return compare_versions(ver(a), ver(b));
}

static ReleaseInfo rel(const std::string &tag, bool pre = false, bool draft = false, const std::string &notes = "")
{
	ReleaseInfo r;
	r.tag = tag;
	r.name = tag;
	r.notes = notes.empty() ? "Notes of " + tag : notes;
	r.html_url = "https://github.com/angeloruggieridj/obs-multiview-pro/releases/tag/" + tag;
	r.published_at = "2026-10-21T10:00:00Z";
	r.prerelease = pre;
	r.draft = draft;
	return r;
}

static std::vector<std::string> tags(const std::vector<ReleaseInfo> &list)
{
	std::vector<std::string> out;
	for (const ReleaseInfo &r : list)
		out.push_back(r.tag);
	return out;
}

/* ---- parse_version ----------------------------------------------------------- */

TEST_CASE("state_update_version_parse_fields")
{
	SemVersion v;
	REQUIRE(parse_version("1.2.3", v));
	CHECK(v.major == 1);
	CHECK(v.minor == 2);
	CHECK(v.patch == 3);
	CHECK(v.pre.empty());

	REQUIRE(parse_version("v10.20.30-rc.1+build.5", v));
	CHECK(v.major == 10);
	CHECK(v.minor == 20);
	CHECK(v.patch == 30);
	REQUIRE(v.pre.size() == 2);
	CHECK(v.pre[0] == "rc");
	CHECK(v.pre[1] == "1");

	REQUIRE(parse_version("0.0.0", v));
	CHECK(v.major == 0);
	REQUIRE(parse_version("1.0.0-alpha-1.x-y", v));
	REQUIRE(v.pre.size() == 2);
	CHECK(v.pre[0] == "alpha-1");
	CHECK(v.pre[1] == "x-y");
	CHECK(valid("1.0.0+20261021"));
	CHECK(valid("V1.0.0") == false); /* only a lower-case v */
	CHECK(valid("999999999.0.0"));
}

TEST_CASE("state_update_version_rejects_garbage")
{
	const char *bad[] = {"",          "v",          "x.y",        "1.2",       "1.2.3.4",      "1..3",
			     "1.2.3-",    "1.2.3+",     "1.2.3-a..b", "1.2.3-a.",  "01.2.3",       "1.02.3",
			     "1.2.03",    "1.2.3-01",   " 1.2.3",     "1.2.3 ",    "1.2.3\n",      "-1.2.3",
			     "+1.2.3",    "vv1.2.3",    "1.2.3-a_b",  "1.2.3+a_b", "1.2.3-\xc3\xa9",      "1.2.3+b..c",
			     "1000000000.0.0", "1.99999999999999999999.0", "4294967296.0.0", "1.2.3-12345678901",
			     "1.2.x",     "a.b.c",      "1.2.-3"};
	for (const char *b : bad) {
		INFO("text: \"" << b << "\"");
		CHECK_FALSE(valid(b));
	}
	CHECK_FALSE(valid(std::string("1.2.3\0", 6)));
	CHECK_FALSE(valid("1.2.3-" + std::string(200, 'a'))); /* longer than kMaxVersionText */
	CHECK(valid("1.2.3-" + std::string(100, 'a')));
	CHECK(valid("1.2.3-a.") == false);

	/* A failed parse leaves the output alone. */
	SemVersion v;
	v.major = 7;
	CHECK_FALSE(parse_version("junk", v));
	CHECK(v.major == 7);
}

TEST_CASE("state_update_version_precedence")
{
	CHECK(cmp("1.0.0", "1.0.0") == 0);
	CHECK(cmp("v1.0.0", "1.0.0") == 0);
	CHECK(cmp("1.0.0+abc", "1.0.0+xyz") == 0); /* build metadata ignored */
	CHECK(cmp("1.0.0", "2.0.0") < 0);
	CHECK(cmp("2.0.0", "2.1.0") < 0);
	CHECK(cmp("2.1.0", "2.1.1") < 0);
	CHECK(cmp("1.10.0", "1.9.0") > 0); /* numeric, not text */
	CHECK(cmp("1.0.0-rc.1", "1.0.0") < 0);
	CHECK(cmp("1.0.0", "1.0.0-rc.1") > 0);

	/* The semver.org example chain. */
	const char *chain[] = {"1.0.0-alpha",      "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
			       "1.0.0-beta.2",     "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0"};
	const size_t n = sizeof(chain) / sizeof(chain[0]);
	for (size_t i = 0; i < n; ++i)
		for (size_t j = 0; j < n; ++j) {
			INFO(chain[i] << " vs " << chain[j]);
			const int c = cmp(chain[i], chain[j]);
			if (i < j)
				CHECK(c < 0);
			else if (i > j)
				CHECK(c > 0);
			else
				CHECK(c == 0);
		}
	CHECK(cmp("1.0.0-2", "1.0.0-10") < 0);   /* numeric identifiers numerically */
	CHECK(cmp("1.0.0-10", "1.0.0-a") < 0);   /* numeric before alphanumeric */
	CHECK(cmp("1.0.0-B", "1.0.0-a") < 0);    /* ASCII order */
	CHECK(cmp("1.0.0-rc.1", "1.0.0-rc.1.1") < 0);
}

/* ---- newer_releases / changelog ------------------------------------------------ */

TEST_CASE("state_update_newer_releases_selection")
{
	const std::vector<ReleaseInfo> list = {rel("v0.9.0"), rel("v1.2.0"), rel("v1.0.0"), rel("v1.1.0"),
					       rel("v1.3.0", false, true), /* draft */
					       rel("v1.2.1-rc.1", true), rel("not-a-version"), rel("v1.1.0")};

	CHECK(tags(newer_releases(list, "1.0.0", false)) == std::vector<std::string>{"v1.2.0", "v1.1.0"});
	CHECK(tags(newer_releases(list, "1.0.0", true)) ==
	      std::vector<std::string>{"v1.2.1-rc.1", "v1.2.0", "v1.1.0"});
	CHECK(newer_releases(list, "1.2.0", false).empty());
	CHECK(tags(newer_releases(list, "1.2.0", true)) == std::vector<std::string>{"v1.2.1-rc.1"});
	CHECK(newer_releases(list, "9.0.0", true).empty());
	CHECK(newer_releases(list, "garbage", true).empty());
	CHECK(newer_releases({}, "1.0.0", true).empty());

	/* A pre-release of the installed version's successor while on a pre-release. */
	CHECK(tags(newer_releases({rel("v1.0.0-rc.2", true), rel("v1.0.0-rc.1", true), rel("v1.0.0")}, "1.0.0-rc.1",
				  false)) == std::vector<std::string>{"v1.0.0"});
}

TEST_CASE("state_update_changelog_format_and_order")
{
	std::vector<ReleaseInfo> list = {rel("v1.2.0", false, false, "- two\r\n- more"), rel("v1.1.0", false, false, "- one")};
	list[1].published_at = "garbage";
	const std::string md = combined_changelog(list);
	const size_t a = md.find("## v1.2.0 (2026-10-21)");
	const size_t b = md.find("## v1.1.0");
	REQUIRE(a != std::string::npos);
	REQUIRE(b != std::string::npos);
	CHECK(a < b);
	CHECK(md.find("## v1.1.0 (") == std::string::npos); /* no date when it is not one */
	CHECK(md.find("- two\n- more") != std::string::npos);
	CHECK(md.find('\r') == std::string::npos);
	CHECK(md.find("- one") != std::string::npos);
	CHECK(md.find("truncated") == std::string::npos);
	CHECK(combined_changelog({}).empty());
}

TEST_CASE("state_update_changelog_is_capped")
{
	std::string big;
	for (int i = 0; i < 20000; ++i)
		big += "\xc3\xa0\xc3\xa8 line\n"; /* multi-byte UTF-8 */
	const std::vector<ReleaseInfo> list = {rel("v2.0.0", false, false, big), rel("v1.9.0", false, false, big)};
	const std::string md = combined_changelog(list);
	CHECK(md.size() <= kMaxChangelogBytes);
	CHECK(md.size() > kMaxChangelogBytes - 200);
	CHECK(md.find("truncated") != std::string::npos);
	/* Valid UTF-8: no lead byte without its continuation bytes. */
	size_t i = 0;
	bool ok = true;
	while (i < md.size()) {
		const unsigned char c = static_cast<unsigned char>(md[i]);
		size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
		if (len == 0 || i + len > md.size()) {
			ok = false;
			break;
		}
		for (size_t k = 1; k < len; ++k)
			if ((static_cast<unsigned char>(md[i + k]) & 0xC0) != 0x80)
				ok = false;
		i += len;
	}
	CHECK(ok);

	const std::string tiny = combined_changelog(list, 300);
	CHECK(tiny.size() <= 300);
	CHECK(combined_changelog(list, 5).size() <= 5);
}

TEST_CASE("state_update_images_are_stripped")
{
	CHECK(strip_markdown_images("a ![shot](https://evil.example/x.png) b") == "a shot b");
	CHECK(strip_markdown_images("![](http://x/y.png)") == "");
	CHECK(strip_markdown_images("x <img src=\"https://evil.example/t.gif\"> y") == "x  y");
	CHECK(strip_markdown_images("x <IMG SRC=a.png/> y") == "x  y");
	CHECK(strip_markdown_images("[link](https://github.com/a) and !important") ==
	      "[link](https://github.com/a) and !important");
	CHECK(strip_markdown_images("unterminated ![alt](http://x") == "unterminated alt");
	CHECK(strip_markdown_images("unterminated <img src=x") == "unterminated ");
	const std::string md = combined_changelog({rel("v1.0.0", false, false, "see ![x](https://evil.example/p.png)")});
	CHECK(md.find("evil.example") == std::string::npos);
}

/* ---- evaluate / texts ---------------------------------------------------------- */

TEST_CASE("state_update_evaluate")
{
	const std::vector<ReleaseInfo> list = {rel("v1.1.0"), rel("v1.0.0", false, false, "installed notes"),
					       rel("v1.2.0-beta.1", true)};
	UpdateState s = evaluate_releases(list, "1.0.0", false);
	CHECK(s.status == UpdateStatus::UpdateAvailable);
	CHECK(s.latest == "v1.1.0");
	CHECK(s.url == "https://github.com/angeloruggieridj/obs-multiview-pro/releases/tag/v1.1.0");
	CHECK(s.changelog.find("## v1.1.0") != std::string::npos);
	CHECK(s.changelog.find("v1.0.0") == std::string::npos);

	s = evaluate_releases(list, "1.0.0", true);
	CHECK(s.status == UpdateStatus::UpdateAvailable);
	CHECK(s.latest == "v1.2.0-beta.1");
	CHECK(s.changelog.find("## v1.2.0-beta.1") < s.changelog.find("## v1.1.0"));

	s = evaluate_releases(list, "1.1.0", false);
	CHECK(s.status == UpdateStatus::UpToDate);
	CHECK(s.installed_notes.find("Notes of v1.1.0") != std::string::npos);

	s = evaluate_releases(list, "1.0.5", false); /* installed version not listed */
	CHECK(s.status == UpdateStatus::UpdateAvailable);
	s = evaluate_releases({rel("v1.0.0")}, "1.0.5", false);
	CHECK(s.status == UpdateStatus::UpToDate);
	CHECK(s.installed_notes.empty());

	CHECK(evaluate_releases({}, "1.0.0", true).status == UpdateStatus::NoReleases);
	CHECK(evaluate_releases({rel("v2.0.0", false, true)}, "1.0.0", true).status == UpdateStatus::NoReleases);
	CHECK(evaluate_releases({rel("v2.0.0-rc.1", true)}, "1.0.0", false).status == UpdateStatus::NoReleases);
	CHECK(evaluate_releases({rel("junk")}, "1.0.0", true).status == UpdateStatus::NoReleases);

	/* A release page outside the repository is never offered. */
	std::vector<ReleaseInfo> evil = {rel("v3.0.0")};
	evil[0].html_url = "https://evil.example/download";
	s = evaluate_releases(evil, "1.0.0", false);
	CHECK(s.status == UpdateStatus::UpdateAvailable);
	CHECK(s.url.empty());

	s = evaluate_releases(list, "not a version", false);
	CHECK(s.status == UpdateStatus::Error);
	CHECK_FALSE(s.message.empty());
}

TEST_CASE("state_update_status_texts")
{
	UpdateState s;
	CHECK(update_status_text(s, "") == "Not checked yet.");
	s.status = UpdateStatus::Checking;
	CHECK(update_status_text(s, "") == "Checking for updates...");
	s.status = UpdateStatus::UpToDate;
	CHECK(update_status_text(s, "") == "You have the latest version.");
	s.status = UpdateStatus::UpdateAvailable;
	s.latest = "v1.1.0";
	CHECK(update_status_text(s, "") == "Version 1.1.0 is available.");
	CHECK(update_status_text(s, "v1.1.0") == "Version 1.1.0 is available (skipped).");
	CHECK(update_status_text(s, "v1.0.9") == "Version 1.1.0 is available.");
	s.status = UpdateStatus::NoReleases;
	CHECK(update_status_text(s, "") == "No release has been published yet.");
	s.status = UpdateStatus::Error;
	s.message = "Could not reach GitHub.";
	CHECK(update_status_text(s, "") == "Could not reach GitHub.");
	s.message.clear();
	CHECK(update_status_text(s, "") == "The update check failed.");

	CHECK(http_error_text(403) == "GitHub rate limit reached. Try again later.");
	CHECK(http_error_text(429) == "GitHub rate limit reached. Try again later.");
	CHECK(http_error_text(503) == "GitHub is not available right now (HTTP 503). Try again later.");
	CHECK(http_error_text(418) == "Unexpected answer from GitHub (HTTP 418).");
}

/* ---- should_auto_check --------------------------------------------------------- */

TEST_CASE("state_update_auto_check_schedule")
{
	const int64_t day = kAutoCheckIntervalSeconds;
	const int64_t now = 1800000000;
	CHECK(should_auto_check(now, 0, true, false));                  /* never checked */
	CHECK(should_auto_check(now, now - day, true, false));          /* exactly due */
	CHECK_FALSE(should_auto_check(now, now - day + 1, true, false)); /* one second early */
	CHECK_FALSE(should_auto_check(now, now - 60, true, false));
	CHECK_FALSE(should_auto_check(now, 0, false, false));            /* disabled */
	CHECK_FALSE(should_auto_check(now, 0, true, true));              /* streaming / recording */
	CHECK_FALSE(should_auto_check(now, now - 5 * day, true, true));
	CHECK(should_auto_check(now, now + 3600, true, false));          /* clock went back */
	CHECK(should_auto_check(now, -5, true, false));                  /* hostile stored value */
	CHECK(should_auto_check(now, INT64_MIN, true, false));
	CHECK(should_auto_check(now, INT64_MAX, true, false));
	CHECK(should_auto_check(INT64_MAX, 1, true, false));
	CHECK_FALSE(should_auto_check(now, now - 3600, true, false, 7200));
	CHECK(should_auto_check(now, now - 7200, true, false, 7200));

	/* A failed attempt is stored like a successful one: the next one waits a day. */
	int64_t last = 0;
	int attempts = 0;
	for (int64_t t = now; t < now + 3 * day; t += 3600)
		if (should_auto_check(t, last, true, false)) {
			last = t;
			++attempts;
		}
	CHECK(attempts == 3);
}

/* ---- URLs ---------------------------------------------------------------------- */

TEST_CASE("state_update_release_url_allow_list")
{
	CHECK(is_allowed_release_url("https://github.com/angeloruggieridj/obs-multiview-pro/releases/tag/v1.1.0"));
	CHECK(is_allowed_release_url("https://github.com/angeloruggieridj/obs-multiview-pro/releases"));
	CHECK(is_allowed_release_url("https://github.com/angeloruggieridj/obs-multiview-pro/compare/v1.0.0...v1.1.0"));
	CHECK(is_allowed_release_url("https://github.com/angeloruggieridj/obs-multiview-pro/issues/3#issuecomment-1"));

	const char *bad[] = {"",
			     "http://github.com/angeloruggieridj/obs-multiview-pro/releases",
			     "HTTPS://github.com/angeloruggieridj/obs-multiview-pro/releases",
			     "https://github.com/angeloruggieridj/obs-multiview-pro",
			     "https://github.com/angeloruggieridj/obs-multiview-pro-evil/releases",
			     "https://github.com/someone/obs-multiview-pro/releases",
			     "https://github.com.evil.example/angeloruggieridj/obs-multiview-pro/",
			     "https://evil.example/https://github.com/angeloruggieridj/obs-multiview-pro/",
			     "https://user@github.com/angeloruggieridj/obs-multiview-pro/releases",
			     "https://github.com:8443/angeloruggieridj/obs-multiview-pro/releases",
			     "https://GitHub.com/angeloruggieridj/obs-multiview-pro/releases",
			     "https://github.com/angeloruggieridj/obs-multiview-pro/../../evil",
			     "https://github.com/angeloruggieridj/obs-multiview-pro/%2e%2e/evil",
			     "https://github.com/angeloruggieridj/obs-multiview-pro//evil.example",
			     "https://github.com/angeloruggieridj/obs-multiview-pro/a\\b",
			     "https://github.com/angeloruggieridj/obs-multiview-pro/a b",
			     "https://github.com/angeloruggieridj/obs-multiview-pro/a\nb",
			     "https://github.com/angeloruggieridj/obs-multiview-pro/a\tb",
			     "javascript:alert(1)",
			     "file:///C:/Windows/System32/calc.exe",
			     " https://github.com/angeloruggieridj/obs-multiview-pro/releases"};
	for (const char *b : bad) {
		INFO("url: \"" << b << "\"");
		CHECK_FALSE(is_allowed_release_url(b));
	}
	CHECK_FALSE(is_allowed_release_url(std::string("https://github.com/angeloruggieridj/obs-multiview-pro/a\0b", 58)));
	CHECK_FALSE(is_allowed_release_url("https://github.com/angeloruggieridj/obs-multiview-pro/" + std::string(5000, 'a')));
}

TEST_CASE("state_update_redirect_allow_list")
{
	CHECK(is_allowed_redirect("https", "github.com"));
	CHECK(is_allowed_redirect("https", "api.github.com"));
	CHECK(is_allowed_redirect("https", "objects.githubusercontent.com"));
	CHECK(is_allowed_redirect("https", "githubusercontent.com"));
	CHECK_FALSE(is_allowed_redirect("http", "api.github.com"));
	CHECK_FALSE(is_allowed_redirect("https", "evilgithub.com"));
	CHECK_FALSE(is_allowed_redirect("https", "github.com.evil.example"));
	CHECK_FALSE(is_allowed_redirect("https", "githubusercontent.com.evil.example"));
	CHECK_FALSE(is_allowed_redirect("https", ".github.com"));
	CHECK_FALSE(is_allowed_redirect("https", ""));
	CHECK_FALSE(is_allowed_redirect("ftp", "github.com"));
}

TEST_CASE("state_update_url_override")
{
	CHECK(update_url_override(nullptr).empty());
	CHECK(update_url_override("").empty());
	CHECK(update_url_override("http://127.0.0.1:8765/releases.json") == "http://127.0.0.1:8765/releases.json");
	CHECK(update_url_override("http://localhost:8765/r") == "http://localhost:8765/r");
	CHECK(update_url_override("https://example.org/r") == "https://example.org/r");
	CHECK(update_url_override("http://example.org/r").empty());
	CHECK(update_url_override("http://127.0.0.1.evil.example/r").empty());
	CHECK(update_url_override("http://127.0.0.1/r").empty()); /* the port is required */
	CHECK(update_url_override("http://localhost.evil.example:80/r").empty());
	CHECK(update_url_override("file:///C:/x.json").empty());
	CHECK(update_url_override("https://example.org/a b").empty());
	CHECK(update_url_override("https://example.org/a\nb").empty());
}
