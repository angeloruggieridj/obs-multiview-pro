// SPDX-License-Identifier: GPL-2.0-or-later
//
// Locale consistency: data/locale/*.ini against each other and against the
// sources in src/. Reads the source tree (MVPRO_SOURCE_DIR, set by CMake); no
// OBS, no Qt.
//
//  - Every locale key used in src/ exists in en-US.ini, and every en-US key is
//    used (no dead text).
//  - Every other locale only has en-US keys, and it-IT.ini translates all of
//    them (OBS would silently fall back to English).
//  - Placeholders (%1 %2 ...), '&' accelerators and \n line breaks match the
//    English value.
//  - The files follow the syntax OBS's text lookup reads (libobs
//    util/text-lookup.c): Key="Value" on one line, '#' comment lines, no BOM,
//    escapes \n and \" only; no empty values, no leading/trailing spaces, no
//    duplicate keys, valid UTF-8.
//  - Heuristic: no user-visible literal left in src/ (see kAllowedLiterals).

#include "doctest.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef MVPRO_SOURCE_DIR
#error "MVPRO_SOURCE_DIR must point at the source tree"
#endif

namespace fs = std::filesystem;

namespace {

const fs::path kRoot = fs::u8path(MVPRO_SOURCE_DIR);

std::string read_file(const fs::path &p)
{
	std::ifstream in(p, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string join(const std::vector<std::string> &lines)
{
	std::string out;
	for (const std::string &l : lines)
		out += "\n  " + l;
	return out;
}

bool is_letter(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool is_ident(char c)
{
	return is_letter(c) || (c >= '0' && c <= '9') || c == '_';
}

bool valid_utf8(const std::string &s)
{
	size_t i = 0;
	while (i < s.size()) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		const size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
		if (len == 0 || i + len > s.size())
			return false;
		for (size_t k = 1; k < len; ++k)
			if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80)
				return false;
		i += len;
	}
	return true;
}

/* Dotted OBS-style key: "Menu.Layout", "Dialog.Appearance.BorderColor". */
bool looks_like_key(const std::string &s)
{
	if (s.empty() || !(s[0] >= 'A' && s[0] <= 'Z'))
		return false;
	bool dot = false;
	char prev = 0;
	for (char c : s) {
		if (c == '.') {
			if (prev == '.' || prev == 0)
				return false;
			dot = true;
		} else if (!is_ident(c)) {
			return false;
		}
		prev = c;
	}
	return dot && prev != '.';
}

/* ---- ini files ------------------------------------------------------------- */

struct Locale {
	std::string name; /* "en-US" */
	std::map<std::string, std::string> values; /* raw value (escapes kept) */
	std::vector<std::string> errors;
};

Locale parse_locale(const fs::path &file)
{
	Locale loc;
	loc.name = file.stem().string();
	const std::string text = read_file(file);
	if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0)
		loc.errors.push_back(loc.name + ": starts with a UTF-8 BOM");
	if (!valid_utf8(text))
		loc.errors.push_back(loc.name + ": not valid UTF-8");

	std::istringstream lines(text);
	std::string line;
	int number = 0;
	while (std::getline(lines, line)) {
		++number;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		const std::string where = loc.name + ":" + std::to_string(number) + ": ";
		if (line.empty() || line[0] == '#')
			continue;
		const size_t eq = line.find('=');
		if (eq == std::string::npos || eq == 0) {
			loc.errors.push_back(where + "not Key=\"Value\"");
			continue;
		}
		const std::string key = line.substr(0, eq);
		if (!looks_like_key(key)) {
			loc.errors.push_back(where + "bad key '" + key + "'");
			continue;
		}
		const std::string rest = line.substr(eq + 1);
		if (rest.size() < 2 || rest.front() != '"' || rest.back() != '"') {
			loc.errors.push_back(where + key + ": the value must be in double quotes, nothing after it");
			continue;
		}
		const std::string value = rest.substr(1, rest.size() - 2);
		bool ok = true;
		for (size_t i = 0; i < value.size(); ++i) {
			if (value[i] == '"') {
				loc.errors.push_back(where + key + ": unescaped quote inside the value");
				ok = false;
				break;
			}
			if (value[i] == '\\') {
				const char next = i + 1 < value.size() ? value[i + 1] : '\0';
				if (next != 'n' && next != '"') {
					loc.errors.push_back(where + key + ": only \\n and \\\" escapes are allowed");
					ok = false;
					break;
				}
				++i;
			}
			if (value[i] == '\t') {
				loc.errors.push_back(where + key + ": tab inside the value");
				ok = false;
				break;
			}
		}
		if (!ok)
			continue;
		if (value.empty())
			loc.errors.push_back(where + key + ": empty value");
		else if (value.front() == ' ' || value.back() == ' ' || value.compare(0, 2, "\\n") == 0 ||
			 (value.size() >= 2 && value.compare(value.size() - 2, 2, "\\n") == 0))
			loc.errors.push_back(where + key + ": leading or trailing space / line break");
		if (loc.values.count(key))
			loc.errors.push_back(where + "duplicate key " + key);
		loc.values[key] = value;
	}
	return loc;
}

std::vector<Locale> all_locales()
{
	std::vector<Locale> out;
	const fs::path dir = kRoot / "data" / "locale";
	if (!fs::is_directory(dir))
		return out;
	for (const fs::directory_entry &e : fs::directory_iterator(dir))
		if (e.is_regular_file() && e.path().extension() == ".ini")
			out.push_back(parse_locale(e.path()));
	std::sort(out.begin(), out.end(), [](const Locale &a, const Locale &b) { return a.name < b.name; });
	return out;
}

const Locale *find_locale(const std::vector<Locale> &all, const std::string &name)
{
	for (const Locale &l : all)
		if (l.name == name)
			return &l;
	return nullptr;
}

/* What must survive translation: %n placeholders, '&' and \n, in order of
 * appearance for the placeholders. */
std::string shape(const std::string &value)
{
	std::vector<std::string> placeholders;
	int amps = 0;
	int breaks = 0;
	for (size_t i = 0; i < value.size(); ++i) {
		if (value[i] == '%' && i + 1 < value.size() && value[i + 1] >= '1' && value[i + 1] <= '9')
			placeholders.push_back(value.substr(i, 2));
		else if (value[i] == '&')
			++amps;
		else if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == 'n')
			++breaks;
	}
	std::sort(placeholders.begin(), placeholders.end());
	std::string s;
	for (const std::string &p : placeholders)
		s += p + " ";
	return s + "&x" + std::to_string(amps) + " \\nx" + std::to_string(breaks);
}

/* ---- sources ----------------------------------------------------------------- */

struct Literal {
	std::string text; /* as written between the quotes (escapes kept) */
	size_t begin;     /* offset of the opening quote in the stripped code */
	size_t end;       /* offset after the closing quote */
};

struct Source {
	std::string name;
	std::string code; /* comments replaced by spaces, line breaks kept */
	std::vector<Literal> literals;
};

/* Comments blanked out (strings, character literals and line numbers kept),
 * string literals collected. */
Source scan_source(const fs::path &file)
{
	Source src;
	src.name = file.filename().string();
	const std::string in = read_file(file);
	std::string &out = src.code;
	out.reserve(in.size());
	size_t i = 0;
	while (i < in.size()) {
		const char c = in[i];
		const char n = i + 1 < in.size() ? in[i + 1] : '\0';
		if (c == '/' && n == '/') {
			while (i < in.size() && in[i] != '\n') {
				out += ' ';
				++i;
			}
		} else if (c == '/' && n == '*') {
			out += "  ";
			i += 2;
			while (i < in.size() && !(in[i] == '*' && i + 1 < in.size() && in[i + 1] == '/')) {
				out += in[i] == '\n' ? '\n' : ' ';
				++i;
			}
			if (i < in.size()) {
				out += "  ";
				i += 2;
			}
		} else if (c == '"' || c == '\'') {
			const size_t start = out.size();
			std::string body;
			out += c;
			++i;
			while (i < in.size() && in[i] != c && in[i] != '\n') {
				if (in[i] == '\\' && i + 1 < in.size()) {
					body += in[i];
					out += in[i++];
				}
				body += in[i];
				out += in[i++];
			}
			if (i < in.size() && in[i] == c)
				out += in[i++];
			if (c == '"')
				src.literals.push_back({body, start, out.size()});
		} else {
			out += c;
			++i;
		}
	}
	return src;
}

std::vector<Source> all_sources()
{
	std::vector<Source> out;
	const fs::path dir = kRoot / "src";
	for (const fs::directory_entry &e : fs::directory_iterator(dir)) {
		const std::string ext = e.path().extension().string();
		if (e.is_regular_file() && (ext == ".cpp" || ext == ".hpp" || ext == ".h"))
			out.push_back(scan_source(e.path()));
	}
	std::sort(out.begin(), out.end(), [](const Source &a, const Source &b) { return a.name < b.name; });
	return out;
}

size_t line_of(const std::string &code, size_t offset)
{
	return size_t(std::count(code.begin(), code.begin() + std::ptrdiff_t(offset), '\n')) + 1;
}

/* Literals that hold letters but are not text for the user. Keep it short;
 * every entry needs a reason. */
const std::map<std::string, std::string> kAllowedLiterals = {
	{"Arial", "font family name"},
	{"Helvetica", "font family name"},
	{"Monospace", "font family name"},
	{"Sans Serif", "font family name"},
	{"^#?[0-9A-Fa-f]{0,6}$", "validator pattern of the hex colour field"},
	{R"(<a href=\"%1\">%1</a>)", "link markup around the repository address"},
	{"https", "URL scheme compared in the update check"},
	{"http", "URL scheme compared in the update check"},
};

bool has_word(const std::string &s)
{
	for (size_t i = 0; i + 1 < s.size(); ++i)
		if (is_letter(s[i]) && is_letter(s[i + 1]))
			return true;
	return false;
}

/* "ON AIR", "MUTE", "OK": the shape of a tile caption. */
bool all_caps_words(const std::string &s)
{
	bool letters = false;
	for (char c : s) {
		if (c >= 'A' && c <= 'Z')
			letters = true;
		else if (c != ' ')
			return false;
	}
	return letters && s.size() >= 2;
}

/* "Could not reach GitHub.", "Settings...": a sentence or a menu entry. */
bool looks_like_sentence(const std::string &s)
{
	if (s.find(' ') == std::string::npos || !has_word(s))
		return false;
	auto ends = [&](const std::string &tail) {
		return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
	};
	return ends(".") || ends("\xE2\x80\xA6") || ends("\\xE2\\x80\\xA6") || ends("?") || ends("!");
}

/* Identifier ending right before `pos` (the '(' of a call). */
std::string ident_before(const std::string &code, size_t pos)
{
	size_t end = pos;
	while (end > 0 && code[end - 1] == ' ')
		--end;
	size_t start = end;
	while (start > 0 && is_ident(code[start - 1]))
		--start;
	return code.substr(start, end - start);
}

/* Offset of the ')' that closes the '(' at `open`, or npos. */
size_t matching_paren(const std::string &code, size_t open)
{
	int depth = 0;
	for (size_t i = open; i < code.size(); ++i) {
		if (code[i] == '"' || code[i] == '\'') {
			const char q = code[i];
			for (++i; i < code.size() && code[i] != q; ++i)
				if (code[i] == '\\')
					++i;
			continue;
		}
		if (code[i] == '(')
			++depth;
		else if (code[i] == ')' && --depth == 0)
			return i;
	}
	return std::string::npos;
}

const std::set<std::string> kUiCalls = {
	"setText",        "setWindowTitle", "setToolTip",  "setStatusTip", "setWhatsThis", "setPlaceholderText",
	"addAction",      "addMenu",        "addTab",      "insertTab",    "setTabText",   "addRow",
	"addSection",     "QLabel",         "QPushButton", "QCheckBox",    "QRadioButton", "QGroupBox",
	"QAction",        "QMenu",          "drawText",    "showMessage",  "information",  "warning",
	"critical",       "question",       "obs_frontend_add_tools_menu_qaction",
};

const std::set<std::string> kQtStringMakers = {"QStringLiteral", "fromUtf8", "fromLatin1", "fromLocal8Bit",
					       "QLatin1String", "QLatin1StringView", "QString"};

const std::set<std::string> kTranslationCalls = {"tr", "translate", "qsTr", "QT_TR_NOOP", "QT_TRANSLATE_NOOP", "trUtf8"};

} // namespace

TEST_CASE("locale_files_parse")
{
	const std::vector<Locale> all = all_locales();
	REQUIRE_MESSAGE(find_locale(all, "en-US") != nullptr, "data/locale/en-US.ini is missing");
	REQUIRE_MESSAGE(find_locale(all, "it-IT") != nullptr, "data/locale/it-IT.ini is missing");
	std::vector<std::string> errors;
	for (const Locale &l : all)
		errors.insert(errors.end(), l.errors.begin(), l.errors.end());
	CHECK_MESSAGE(errors.empty(), join(errors));
	CHECK(find_locale(all, "en-US")->values.size() > 50);
}

TEST_CASE("locale_translations_match_english")
{
	const std::vector<Locale> all = all_locales();
	const Locale *en = find_locale(all, "en-US");
	REQUIRE(en != nullptr);
	std::vector<std::string> problems;
	for (const Locale &l : all) {
		if (&l == en)
			continue;
		for (const auto &kv : l.values) {
			const auto it = en->values.find(kv.first);
			if (it == en->values.end()) {
				problems.push_back(l.name + ": " + kv.first + " is not an en-US key");
				continue;
			}
			if (shape(kv.second) != shape(it->second))
				problems.push_back(l.name + ": " + kv.first + ": placeholders / & / \\n differ (" +
						   shape(kv.second) + " vs en-US " + shape(it->second) + ")");
		}
	}
	/* Italian is complete: a missing key would show English without a word. */
	if (const Locale *it = find_locale(all, "it-IT"))
		for (const auto &kv : en->values)
			if (!it->values.count(kv.first))
				problems.push_back("it-IT: " + kv.first + " is not translated");
	CHECK_MESSAGE(problems.empty(), join(problems));
}

TEST_CASE("locale_keys_used_in_sources_exist")
{
	const std::vector<Locale> all = all_locales();
	const Locale *en = find_locale(all, "en-US");
	REQUIRE(en != nullptr);
	std::set<std::string> used;
	std::vector<std::string> problems;
	for (const Source &s : all_sources())
		for (const Literal &lit : s.literals)
			if (looks_like_key(lit.text)) {
				used.insert(lit.text);
				if (!en->values.count(lit.text))
					problems.push_back(s.name + ":" + std::to_string(line_of(s.code, lit.begin)) + ": key " +
							   lit.text + " is not in en-US.ini");
			}
	for (const auto &kv : en->values)
		if (!used.count(kv.first))
			problems.push_back("en-US.ini: " + kv.first + " is not used in src/");
	CHECK_MESSAGE(problems.empty(), join(problems));
}

TEST_CASE("locale_no_user_text_in_sources")
{
	std::vector<std::string> problems;
	for (const Source &s : all_sources()) {
		const std::string &code = s.code;
		auto where = [&](size_t offset) { return s.name + ":" + std::to_string(line_of(code, offset)) + ": "; };
		auto allowed = [](const std::string &text) { return kAllowedLiterals.count(text) != 0; };

		for (size_t open = code.find('('); open != std::string::npos; open = code.find('(', open + 1)) {
			const std::string name = ident_before(code, open);
			if (name.empty())
				continue;
			if (kTranslationCalls.count(name)) {
				problems.push_back(where(open) + name + "(): Qt translation call; use T(\"Key\") (src/i18n.hpp)");
				continue;
			}
			const bool ui = kUiCalls.count(name) != 0;
			const bool maker = kQtStringMakers.count(name) != 0;
			if (!ui && !maker)
				continue;
			const size_t close = matching_paren(code, open);
			if (close == std::string::npos)
				continue;
			for (const Literal &lit : s.literals) {
				if (lit.begin < open || lit.end > close)
					continue;
				if (has_word(lit.text) && !looks_like_key(lit.text) && !allowed(lit.text))
					problems.push_back(where(lit.begin) + "\"" + lit.text + "\" passed to " + name +
							   "(): move it to data/locale/en-US.ini");
			}
		}
		/* Log lines (obs_log / blog) are developer-facing English and stay. */
		std::vector<std::pair<size_t, size_t>> log_calls;
		for (size_t open = code.find('('); open != std::string::npos; open = code.find('(', open + 1)) {
			const std::string name = ident_before(code, open);
			if (name == "obs_log" || name == "blog" || name == "static_assert") {
				const size_t close = matching_paren(code, open);
				if (close != std::string::npos)
					log_calls.emplace_back(open, close);
			}
		}
		auto in_log = [&](const Literal &lit) {
			for (const auto &call : log_calls)
				if (lit.begin > call.first && lit.end <= call.second)
					return true;
			return false;
		};
		for (const Literal &lit : s.literals) {
			if (allowed(lit.text) || in_log(lit))
				continue;
			if (all_caps_words(lit.text))
				problems.push_back(where(lit.begin) + "\"" + lit.text +
						   "\" looks like a tile caption: move it to data/locale/en-US.ini");
			else if (looks_like_sentence(lit.text))
				problems.push_back(where(lit.begin) + "\"" + lit.text +
						   "\" looks like a sentence or menu entry: move it to data/locale/en-US.ini");
		}
	}
	/* One message per literal even when several rules hit it. */
	std::sort(problems.begin(), problems.end());
	problems.erase(std::unique(problems.begin(), problems.end()), problems.end());
	CHECK_MESSAGE(problems.empty(), join(problems));
}
