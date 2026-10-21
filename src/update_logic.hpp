// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Update check: the decisions that need no network, no Qt and no OBS.
//
//  - Semantic versions ("v1.2.3", "1.2.3-rc.1", "1.2.3+build"): parsing and
//    precedence as in semver.org 2.0.0. Anything else is rejected; nothing
//    here throws.
//  - Releases as returned by the GitHub API: which ones are newer than the
//    installed version, and the change log shown for them.
//  - When an automatic check is due.
//  - Which addresses may be opened in the browser.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/* ---- Versions -------------------------------------------------------------- */

struct SemVersion {
	uint32_t major = 0;
	uint32_t minor = 0;
	uint32_t patch = 0;
	/* Pre-release identifiers ("rc", "1" for "-rc.1"); empty for a release. */
	std::vector<std::string> pre;
};

/* Longest accepted version text, and largest accepted numeric part. */
constexpr size_t kMaxVersionText = 128;
constexpr uint32_t kMaxVersionNumber = 999999999;

/* Optional leading "v", MAJOR.MINOR.PATCH, optional "-pre.release", optional
 * "+build" (checked, then ignored). Numbers without leading zeros and at most
 * kMaxVersionNumber. False (and `out` untouched) for anything else. */
bool parse_version(const std::string &text, SemVersion &out);

/* "v1.2.0" -> "1.2.0" for messages; anything else unchanged. */
std::string version_for_display(const std::string &tag);

/* Negative, 0 or positive like strcmp, by semver precedence:
 * 1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-beta < 1.0.0-rc.1 < 1.0.0. */
int compare_versions(const SemVersion &a, const SemVersion &b);

/* ---- Releases ---------------------------------------------------------------- */

struct ReleaseInfo {
	std::string tag;          /* "v1.2.0" */
	std::string name;         /* release title */
	std::string notes;        /* Markdown body */
	std::string html_url;     /* release page */
	std::string published_at; /* ISO 8601, "2026-10-21T10:00:00Z" */
	bool prerelease = false;
	bool draft = false;
};

/* Upper bound of the combined change log (bytes, marker included). */
constexpr size_t kMaxChangelogBytes = 64 * 1024;

/* The releases newer than `current`, newest first: drafts and tags that are not
 * versions are skipped, pre-releases too unless `include_prereleases`; one
 * entry per version. Empty when `current` is not a version. */
std::vector<ReleaseInfo> newer_releases(const std::vector<ReleaseInfo> &releases, const std::string &current,
					bool include_prereleases);

/* Markdown for the given releases in the given order: a "## <tag> (<date>)"
 * heading, then the notes with image syntax removed (no remote resource is
 * ever loaded). At most `max_bytes` (UTF-8 kept whole); a longer text ends
 * with a truncation marker. */
std::string combined_changelog(const std::vector<ReleaseInfo> &releases, size_t max_bytes = kMaxChangelogBytes);

/* Every Markdown image form ("![alt](url)", "![alt][ref]", "![alt][]",
 * "![alt]", nested brackets, images inside link text or alt text) becomes its
 * alt text; an unclosed "![" is dropped; HTML <img ...> tags are dropped. */
std::string strip_markdown_images(const std::string &markdown);

/* ---- Check result ------------------------------------------------------------ */

enum class UpdateStatus {
	Idle,            /* nothing checked in this session */
	Checking,        /* a request is in flight */
	UpToDate,        /* no newer release */
	UpdateAvailable, /* `latest` is newer than the installed version */
	NoReleases,      /* the project has published no (matching) release */
	Error,           /* `message` says what went wrong */
};

struct UpdateState {
	UpdateStatus status = UpdateStatus::Idle;
	std::string latest;          /* UpdateAvailable: tag of the newest release */
	std::string url;             /* UpdateAvailable: its release page */
	std::string changelog;       /* UpdateAvailable: Markdown of every newer release */
	std::string installed_notes; /* UpToDate: Markdown of the installed release, if listed */
	std::string message;         /* Error: short text for the user */
};

/* The state for a successfully fetched release list. */
UpdateState evaluate_releases(const std::vector<ReleaseInfo> &releases, const std::string &current,
			      bool include_prereleases);

/* One line for the Updates tab. `skipped`: the version the user chose to skip. */
std::string update_status_text(const UpdateState &state, const std::string &skipped);

/* Short message for an HTTP status that is not 200 (403 / 429: rate limit). */
std::string http_error_text(int http_status);

/* ---- Scheduling -------------------------------------------------------------- */

constexpr int64_t kAutoCheckIntervalSeconds = 24 * 60 * 60;

/* An automatic check may run now: enabled, not streaming or recording, and
 * the last attempt (successful or not, Unix seconds, 0 = never) is at least
 * `interval` old. A last attempt in the future (the clock went back) allows
 * one check; the caller then stores `now` as the last attempt. */
bool should_auto_check(int64_t now, int64_t last_check, bool enabled, bool streaming_or_recording,
		       int64_t interval = kAutoCheckIntervalSeconds);

/* ---- Addresses --------------------------------------------------------------- */

/* Only "https://github.com/angeloruggieridj/obs-multiview-pro/..." with a
 * plain path (no "..", no "//", no backslash, no spaces or control
 * characters, no user info, no port). */
bool is_allowed_release_url(const std::string &url);

/* Redirects of the API request: https to api.github.com or github.com only
 * (host in lower case). */
bool is_allowed_redirect(const std::string &scheme, const std::string &host);

/* Test aid: the value of OBS_MULTIVIEW_PRO_UPDATE_URL is used only when it is
 * http or https to a loopback host (127.0.0.1, localhost, [::1]), with an
 * optional port and a path, and has no "@", backslash, spaces or control
 * characters; otherwise "" (use the GitHub API). */
std::string update_url_override(const char *env_value);
