// SPDX-License-Identifier: GPL-2.0-or-later
#include "update_logic.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char *kReleasePrefix = "https://github.com/angeloruggieridj/obs-multiview-pro/";
constexpr size_t kMaxUrlLength = 2048;

bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

bool is_ident_char(char c)
{
	return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

bool all_digits(const std::string &s)
{
	if (s.empty())
		return false;
	for (char c : s)
		if (!is_digit(c))
			return false;
	return true;
}

/* A decimal number without a leading zero, at most kMaxVersionNumber. */
bool parse_number(const std::string &s, uint32_t &out)
{
	if (!all_digits(s) || s.size() > 9 || (s.size() > 1 && s[0] == '0'))
		return false;
	uint32_t v = 0;
	for (char c : s)
		v = v * 10 + static_cast<uint32_t>(c - '0');
	if (v > kMaxVersionNumber)
		return false;
	out = v;
	return true;
}

/* Splits on '.'; every part must be non-empty and made of [0-9A-Za-z-]. */
bool split_identifiers(const std::string &s, std::vector<std::string> &out)
{
	out.clear();
	std::string cur;
	for (char c : s) {
		if (c == '.') {
			if (cur.empty())
				return false;
			out.push_back(cur);
			cur.clear();
		} else if (is_ident_char(c)) {
			cur.push_back(c);
		} else {
			return false;
		}
	}
	if (cur.empty())
		return false;
	out.push_back(cur);
	return true;
}

int compare_identifiers(const std::string &a, const std::string &b)
{
	const bool na = all_digits(a);
	const bool nb = all_digits(b);
	if (na && nb) {
		/* No leading zeros (checked by the parser): longer is larger. */
		if (a.size() != b.size())
			return a.size() < b.size() ? -1 : 1;
		return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
	}
	if (na != nb)
		return na ? -1 : 1; /* numeric identifiers have lower precedence */
	const int c = a.compare(b);
	return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

/* "YYYY-MM-DD" from an ISO 8601 time stamp, or "". */
std::string date_part(const std::string &iso)
{
	if (iso.size() < 10)
		return std::string();
	for (size_t i = 0; i < 10; ++i) {
		const bool dash = i == 4 || i == 7;
		if (dash ? iso[i] != '-' : !is_digit(iso[i]))
			return std::string();
	}
	return iso.substr(0, 10);
}

std::string normalize_newlines(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '\r') {
			out.push_back('\n');
			if (i + 1 < s.size() && s[i + 1] == '\n')
				++i;
		} else {
			out.push_back(s[i]);
		}
	}
	return out;
}

/* Largest n <= limit such that s[0, n) does not end inside a UTF-8 sequence. */
size_t utf8_cut(const std::string &s, size_t limit)
{
	if (limit >= s.size())
		return s.size();
	size_t n = limit;
	/* Step back over continuation bytes to the lead byte of the cut sequence. */
	while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
		--n;
	return n;
}

bool starts_with(const std::string &s, const char *prefix)
{
	const std::string p(prefix);
	return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool ends_with(const std::string &s, const char *suffix)
{
	const std::string x(suffix);
	return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

bool has_space_or_control(const std::string &s)
{
	for (char ch : s) {
		const unsigned char c = static_cast<unsigned char>(ch);
		if (c <= 0x20 || c == 0x7F)
			return true;
	}
	return false;
}

char lower_ascii(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

} // namespace

/* ---- Versions -------------------------------------------------------------- */

bool parse_version(const std::string &text, SemVersion &out)
{
	if (text.empty() || text.size() > kMaxVersionText)
		return false;
	std::string s = text[0] == 'v' ? text.substr(1) : text;

	/* Build metadata: validated, then dropped. */
	const size_t plus = s.find('+');
	if (plus != std::string::npos) {
		std::vector<std::string> build;
		if (!split_identifiers(s.substr(plus + 1), build))
			return false;
		s.erase(plus);
	}
	std::vector<std::string> pre;
	const size_t dash = s.find('-');
	if (dash != std::string::npos) {
		if (!split_identifiers(s.substr(dash + 1), pre))
			return false;
		for (const std::string &id : pre)
			if (all_digits(id)) {
				uint32_t unused;
				if (!parse_number(id, unused))
					return false; /* leading zero or too large */
			}
		s.erase(dash);
	}

	std::vector<std::string> core;
	if (!split_identifiers(s, core) || core.size() != 3)
		return false;
	SemVersion v;
	if (!parse_number(core[0], v.major) || !parse_number(core[1], v.minor) || !parse_number(core[2], v.patch))
		return false;
	v.pre = std::move(pre);
	out = std::move(v);
	return true;
}

std::string version_for_display(const std::string &tag)
{
	return !tag.empty() && tag[0] == 'v' ? tag.substr(1) : tag;
}

int compare_versions(const SemVersion &a, const SemVersion &b)
{
	if (a.major != b.major)
		return a.major < b.major ? -1 : 1;
	if (a.minor != b.minor)
		return a.minor < b.minor ? -1 : 1;
	if (a.patch != b.patch)
		return a.patch < b.patch ? -1 : 1;
	if (a.pre.empty() || b.pre.empty()) {
		if (a.pre.empty() == b.pre.empty())
			return 0;
		return a.pre.empty() ? 1 : -1; /* a pre-release comes before the release */
	}
	const size_t n = std::min(a.pre.size(), b.pre.size());
	for (size_t i = 0; i < n; ++i) {
		const int c = compare_identifiers(a.pre[i], b.pre[i]);
		if (c != 0)
			return c;
	}
	if (a.pre.size() == b.pre.size())
		return 0;
	return a.pre.size() < b.pre.size() ? -1 : 1;
}

/* ---- Releases ---------------------------------------------------------------- */

std::vector<ReleaseInfo> newer_releases(const std::vector<ReleaseInfo> &releases, const std::string &current,
					bool include_prereleases)
{
	std::vector<ReleaseInfo> out;
	SemVersion cur;
	if (!parse_version(current, cur))
		return out;

	std::vector<std::pair<SemVersion, const ReleaseInfo *>> found;
	for (const ReleaseInfo &r : releases) {
		if (r.draft || (r.prerelease && !include_prereleases))
			continue;
		SemVersion v;
		if (!parse_version(r.tag, v) || compare_versions(v, cur) <= 0)
			continue;
		/* A tag with a pre-release part counts as a pre-release even when
		 * the release is not marked as one. */
		if (!v.pre.empty() && !include_prereleases)
			continue;
		bool duplicate = false;
		for (const auto &f : found)
			if (compare_versions(f.first, v) == 0)
				duplicate = true;
		if (!duplicate)
			found.emplace_back(v, &r);
	}
	std::stable_sort(found.begin(), found.end(), [](const auto &x, const auto &y) {
		return compare_versions(x.first, y.first) > 0;
	});
	out.reserve(found.size());
	for (const auto &f : found)
		out.push_back(*f.second);
	return out;
}

std::string strip_markdown_images(const std::string &markdown)
{
	std::string out;
	out.reserve(markdown.size());
	size_t i = 0;
	const size_t n = markdown.size();
	while (i < n) {
		/* ![alt](target) -> alt */
		if (markdown[i] == '!' && i + 1 < n && markdown[i + 1] == '[') {
			const size_t close = markdown.find(']', i + 2);
			if (close != std::string::npos && close + 1 < n && markdown[close + 1] == '(') {
				out.append(markdown, i + 2, close - (i + 2));
				const size_t end = markdown.find(')', close + 2);
				i = end == std::string::npos ? n : end + 1;
				continue;
			}
		}
		/* <img ...> (any case) -> nothing */
		if (markdown[i] == '<' && i + 4 <= n && lower_ascii(markdown[i + 1]) == 'i' &&
		    lower_ascii(markdown[i + 2]) == 'm' && lower_ascii(markdown[i + 3]) == 'g' &&
		    (i + 4 == n || markdown[i + 4] == ' ' || markdown[i + 4] == '/' || markdown[i + 4] == '>' ||
		     markdown[i + 4] == '\t' || markdown[i + 4] == '\n')) {
			const size_t end = markdown.find('>', i + 4);
			i = end == std::string::npos ? n : end + 1;
			continue;
		}
		out.push_back(markdown[i]);
		++i;
	}
	return out;
}

std::string combined_changelog(const std::vector<ReleaseInfo> &releases, size_t max_bytes)
{
	std::string md;
	for (const ReleaseInfo &r : releases) {
		if (!md.empty())
			md += "\n\n";
		md += "## " + r.tag;
		const std::string date = date_part(r.published_at);
		if (!date.empty())
			md += " (" + date + ")";
		md += "\n\n";
		const std::string notes = strip_markdown_images(normalize_newlines(r.notes));
		md += notes.empty() ? std::string("*No notes for this release.*") : notes;
		if (md.size() > max_bytes)
			break; /* no need to build what is cut anyway */
	}
	if (md.size() <= max_bytes)
		return md;

	const std::string marker = "\n\n---\n\n*Change log truncated: see the release page for the full notes.*\n";
	if (max_bytes < marker.size())
		return md.substr(0, utf8_cut(md, max_bytes));
	return md.substr(0, utf8_cut(md, max_bytes - marker.size())) + marker;
}

/* ---- Check result ------------------------------------------------------------ */

UpdateState evaluate_releases(const std::vector<ReleaseInfo> &releases, const std::string &current,
			      bool include_prereleases)
{
	UpdateState state;
	SemVersion cur;
	if (!parse_version(current, cur)) {
		state.status = UpdateStatus::Error;
		state.message = "The installed version number is not valid.";
		return state;
	}

	/* What counts as published under the current option. */
	bool any = false;
	for (const ReleaseInfo &r : releases) {
		SemVersion v;
		if (r.draft || !parse_version(r.tag, v))
			continue;
		if (!include_prereleases && (r.prerelease || !v.pre.empty()))
			continue;
		any = true;
		if (compare_versions(v, cur) == 0 && state.installed_notes.empty())
			state.installed_notes = combined_changelog({r});
	}
	if (!any) {
		state.status = UpdateStatus::NoReleases;
		state.installed_notes.clear();
		return state;
	}

	const std::vector<ReleaseInfo> newer = newer_releases(releases, current, include_prereleases);
	if (newer.empty()) {
		state.status = UpdateStatus::UpToDate;
		return state;
	}
	state.installed_notes.clear();
	state.status = UpdateStatus::UpdateAvailable;
	state.latest = newer.front().tag;
	if (is_allowed_release_url(newer.front().html_url))
		state.url = newer.front().html_url;
	state.changelog = combined_changelog(newer);
	return state;
}

std::string update_status_text(const UpdateState &state, const std::string &skipped)
{
	switch (state.status) {
	case UpdateStatus::Idle:
		return "Not checked yet.";
	case UpdateStatus::Checking:
		return "Checking for updates...";
	case UpdateStatus::UpToDate:
		return "You have the latest version.";
	case UpdateStatus::UpdateAvailable:
		return "Version " + version_for_display(state.latest) + " is available" +
		       (!skipped.empty() && skipped == state.latest ? " (skipped)." : ".");
	case UpdateStatus::NoReleases:
		return "No release has been published yet.";
	case UpdateStatus::Error:
		return state.message.empty() ? std::string("The update check failed.") : state.message;
	}
	return std::string();
}

std::string http_error_text(int http_status)
{
	if (http_status == 403 || http_status == 429)
		return "GitHub rate limit reached. Try again later.";
	if (http_status >= 500 && http_status <= 599)
		return "GitHub is not available right now (HTTP " + std::to_string(http_status) + "). Try again later.";
	return "Unexpected answer from GitHub (HTTP " + std::to_string(http_status) + ").";
}

/* ---- Scheduling -------------------------------------------------------------- */

bool should_auto_check(int64_t now, int64_t last_check, bool enabled, bool streaming_or_recording, int64_t interval)
{
	if (!enabled || streaming_or_recording)
		return false;
	if (last_check <= 0 || now < last_check)
		return true; /* never checked, a hostile value, or the clock went back */
	return now - last_check >= interval; /* both positive here: no overflow */
}

/* ---- Addresses --------------------------------------------------------------- */

bool is_allowed_release_url(const std::string &url)
{
	if (url.size() > kMaxUrlLength || !starts_with(url, kReleasePrefix) || has_space_or_control(url))
		return false;
	const std::string rest = url.substr(std::string(kReleasePrefix).size());
	if (rest.empty() || rest[0] == '/')
		return false;
	if (rest.find('\\') != std::string::npos || rest.find("//") != std::string::npos ||
	    rest.find('@') != std::string::npos)
		return false;
	/* No dot segments, also not percent-encoded. */
	std::string lowered;
	lowered.reserve(rest.size());
	for (char c : rest)
		lowered.push_back(lower_ascii(c));
	if (lowered.find("%2e") != std::string::npos || lowered.find("%2f") != std::string::npos ||
	    lowered.find("%5c") != std::string::npos)
		return false;
	const std::string path = lowered.substr(0, lowered.find_first_of("?#"));
	size_t start = 0;
	while (start <= path.size()) {
		const size_t slash = path.find('/', start);
		const std::string seg = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		if (seg == "." || seg == "..")
			return false;
		if (slash == std::string::npos)
			break;
		start = slash + 1;
	}
	return true;
}

bool is_allowed_redirect(const std::string &scheme, const std::string &host)
{
	if (scheme != "https" || host.empty())
		return false;
	if (host == "github.com" || host == "githubusercontent.com")
		return true;
	for (const char *suffix : {".github.com", ".githubusercontent.com"}) {
		const std::string sfx(suffix);
		if (ends_with(host, suffix) && host.size() > sfx.size())
			return true;
	}
	return false;
}

std::string update_url_override(const char *env_value)
{
	if (!env_value)
		return std::string();
	const std::string v(env_value);
	if (v.size() > kMaxUrlLength || has_space_or_control(v))
		return std::string();
	if (starts_with(v, "https://") || starts_with(v, "http://127.0.0.1:") || starts_with(v, "http://localhost:"))
		return v;
	return std::string();
}
