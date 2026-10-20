// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace {

struct PresetName {
	Preset preset;
	const char *name;
};

/* Stable file names. Never renumber / rename: files written by older
 * versions must keep loading. */
constexpr PresetName kPresetNames[] = {
	{Preset::Scenes4, "scenes4"},
	{Preset::Scenes8, "scenes8"},
	{Preset::Scenes16, "scenes16"},
	{Preset::Scenes24, "scenes24"},
	{Preset::Scenes32, "scenes32"},
	{Preset::Scenes8Status, "scenes8_status"},
	{Preset::Scenes16Status, "scenes16_status"},
	{Preset::Scenes24Status, "scenes24_status"},
	{Preset::Scenes32Status, "scenes32_status"},
};

/* ---- UTF-8 ---------------------------------------------------------------- */

void append_utf8(std::string &out, uint32_t cp)
{
	if (cp < 0x80) {
		out.push_back(static_cast<char>(cp));
	} else if (cp < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

/* Length of the well-formed UTF-8 sequence starting at s[i] (no overlongs, no
 * surrogates, <= U+10FFFF), or 0 when it is not well formed. */
size_t utf8_sequence_length(const char *s, size_t i, size_t n)
{
	const unsigned char c = static_cast<unsigned char>(s[i]);
	if (c < 0x80)
		return 1;
	size_t len;
	uint32_t cp;
	uint32_t min;
	if (c >= 0xC2 && c <= 0xDF) {
		len = 2;
		cp = c & 0x1F;
		min = 0x80;
	} else if (c >= 0xE0 && c <= 0xEF) {
		len = 3;
		cp = c & 0x0F;
		min = 0x800;
	} else if (c >= 0xF0 && c <= 0xF4) {
		len = 4;
		cp = c & 0x07;
		min = 0x10000;
	} else {
		return 0;
	}
	if (n - i < len)
		return 0;
	for (size_t k = 1; k < len; ++k) {
		const unsigned char cc = static_cast<unsigned char>(s[i + k]);
		if ((cc & 0xC0) != 0x80)
			return 0;
		cp = (cp << 6) | (cc & 0x3F);
	}
	if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		return 0;
	return len;
}

/* ---- Writer --------------------------------------------------------------- */

void write_string(std::string &out, const std::string &s)
{
	static const char hex[] = "0123456789abcdef";
	out.push_back('"');
	size_t i = 0;
	while (i < s.size()) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		if (c >= 0x80) {
			const size_t len = utf8_sequence_length(s.data(), i, s.size());
			if (len == 0) {
				append_utf8(out, 0xFFFD); /* keep the file readable */
				++i;
			} else {
				out.append(s, i, len);
				i += len;
			}
			continue;
		}
		++i;
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\b':
			out += "\\b";
			break;
		case '\f':
			out += "\\f";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if (c < 0x20) {
				out += "\\u00";
				out.push_back(hex[c >> 4]);
				out.push_back(hex[c & 0xF]);
			} else {
				out.push_back(static_cast<char>(c));
			}
		}
	}
	out.push_back('"');
}

/* ---- Parser --------------------------------------------------------------- */

constexpr int kMaxDepth = 32;

struct JValue {
	enum Type { Null, Bool, Number, String, Array, Object } type = Null;
	bool b = false;
	std::string s; /* string value, or the literal text of a number */
	std::vector<JValue> items;       /* Array elements / Object values */
	std::vector<std::string> keys;   /* Object keys, parallel to items */

	/* Last occurrence wins (duplicate keys). */
	const JValue *find(const char *key) const
	{
		for (size_t i = keys.size(); i-- > 0;)
			if (keys[i] == key)
				return &items[i];
		return nullptr;
	}
};

class Parser {
public:
	Parser(const char *data, size_t size) : m_p(data), m_end(data + size) {}

	bool parse_document(JValue &out)
	{
		skip_ws();
		if (!parse_value(out, 0))
			return false;
		skip_ws();
		return m_p == m_end;
	}

private:
	void skip_ws()
	{
		while (m_p < m_end && (*m_p == ' ' || *m_p == '\t' || *m_p == '\n' || *m_p == '\r'))
			++m_p;
	}

	bool literal(const char *word)
	{
		const char *q = m_p;
		while (*word) {
			if (q >= m_end || *q != *word)
				return false;
			++q;
			++word;
		}
		m_p = q;
		return true;
	}

	bool parse_value(JValue &out, int depth)
	{
		if (depth > kMaxDepth || m_p >= m_end)
			return false;
		switch (*m_p) {
		case '{':
			return parse_object(out, depth);
		case '[':
			return parse_array(out, depth);
		case '"':
			out.type = JValue::String;
			return parse_string(out.s);
		case 't':
			out.type = JValue::Bool;
			out.b = true;
			return literal("true");
		case 'f':
			out.type = JValue::Bool;
			out.b = false;
			return literal("false");
		case 'n':
			out.type = JValue::Null;
			return literal("null");
		default:
			out.type = JValue::Number;
			return parse_number(out.s);
		}
	}

	static bool is_digit(char c) { return c >= '0' && c <= '9'; }

	bool parse_number(std::string &text)
	{
		const char *start = m_p;
		const char *q = m_p;
		if (q < m_end && *q == '-')
			++q;
		if (q >= m_end || !is_digit(*q))
			return false;
		if (*q == '0') {
			++q;
		} else {
			while (q < m_end && is_digit(*q))
				++q;
		}
		if (q < m_end && *q == '.') {
			++q;
			if (q >= m_end || !is_digit(*q))
				return false;
			while (q < m_end && is_digit(*q))
				++q;
		}
		if (q < m_end && (*q == 'e' || *q == 'E')) {
			++q;
			if (q < m_end && (*q == '+' || *q == '-'))
				++q;
			if (q >= m_end || !is_digit(*q))
				return false;
			while (q < m_end && is_digit(*q))
				++q;
		}
		text.assign(start, static_cast<size_t>(q - start));
		m_p = q;
		return true;
	}

	bool parse_hex4(uint32_t &out)
	{
		if (m_end - m_p < 4)
			return false;
		uint32_t v = 0;
		for (int i = 0; i < 4; ++i) {
			const char c = m_p[i];
			v <<= 4;
			if (c >= '0' && c <= '9')
				v |= static_cast<uint32_t>(c - '0');
			else if (c >= 'a' && c <= 'f')
				v |= static_cast<uint32_t>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				v |= static_cast<uint32_t>(c - 'A' + 10);
			else
				return false;
		}
		m_p += 4;
		out = v;
		return true;
	}

	/* m_p is on the opening quote. */
	bool parse_string(std::string &out)
	{
		out.clear();
		++m_p;
		while (true) {
			if (m_p >= m_end)
				return false;
			const unsigned char c = static_cast<unsigned char>(*m_p);
			if (c == '"') {
				++m_p;
				return true;
			}
			if (c < 0x20)
				return false;
			if (c >= 0x80) {
				const size_t len = utf8_sequence_length(m_p, 0, static_cast<size_t>(m_end - m_p));
				if (len == 0)
					return false;
				out.append(m_p, len);
				m_p += len;
				continue;
			}
			if (c != '\\') {
				out.push_back(static_cast<char>(c));
				++m_p;
				continue;
			}
			++m_p;
			if (m_p >= m_end)
				return false;
			const char e = *m_p++;
			switch (e) {
			case '"':
				out.push_back('"');
				break;
			case '\\':
				out.push_back('\\');
				break;
			case '/':
				out.push_back('/');
				break;
			case 'b':
				out.push_back('\b');
				break;
			case 'f':
				out.push_back('\f');
				break;
			case 'n':
				out.push_back('\n');
				break;
			case 'r':
				out.push_back('\r');
				break;
			case 't':
				out.push_back('\t');
				break;
			case 'u': {
				uint32_t cp;
				if (!parse_hex4(cp))
					return false;
				if (cp >= 0xD800 && cp <= 0xDBFF) {
					/* A high surrogate needs an immediately following \uDC00-DFFF. */
					uint32_t lo = 0;
					const char *save = m_p;
					if (m_end - m_p >= 2 && m_p[0] == '\\' && m_p[1] == 'u') {
						m_p += 2;
						if (parse_hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						} else {
							m_p = save;
							cp = 0xFFFD;
						}
					} else {
						cp = 0xFFFD;
					}
				} else if (cp >= 0xDC00 && cp <= 0xDFFF) {
					cp = 0xFFFD;
				}
				append_utf8(out, cp);
				break;
			}
			default:
				return false;
			}
		}
	}

	bool parse_array(JValue &out, int depth)
	{
		out.type = JValue::Array;
		++m_p; /* [ */
		skip_ws();
		if (m_p < m_end && *m_p == ']') {
			++m_p;
			return true;
		}
		while (true) {
			skip_ws();
			JValue v;
			if (!parse_value(v, depth + 1))
				return false;
			out.items.push_back(std::move(v));
			skip_ws();
			if (m_p >= m_end)
				return false;
			if (*m_p == ',') {
				++m_p;
				continue;
			}
			if (*m_p == ']') {
				++m_p;
				return true;
			}
			return false;
		}
	}

	bool parse_object(JValue &out, int depth)
	{
		out.type = JValue::Object;
		++m_p; /* { */
		skip_ws();
		if (m_p < m_end && *m_p == '}') {
			++m_p;
			return true;
		}
		while (true) {
			skip_ws();
			if (m_p >= m_end || *m_p != '"')
				return false;
			std::string key;
			if (!parse_string(key))
				return false;
			skip_ws();
			if (m_p >= m_end || *m_p != ':')
				return false;
			++m_p;
			skip_ws();
			JValue v;
			if (!parse_value(v, depth + 1))
				return false;
			out.keys.push_back(std::move(key));
			out.items.push_back(std::move(v));
			skip_ws();
			if (m_p >= m_end)
				return false;
			if (*m_p == ',') {
				++m_p;
				continue;
			}
			if (*m_p == '}') {
				++m_p;
				return true;
			}
			return false;
		}
	}

	const char *m_p;
	const char *m_end;
};

/* "0".."99", canonical decimal (no sign, no leading zero), or -1. */
int parse_instance_key(const std::string &k)
{
	if (k.empty() || k.size() > 2)
		return -1;
	for (char c : k)
		if (c < '0' || c > '9')
			return -1;
	if (k.size() == 2 && k[0] == '0')
		return -1;
	const int v = std::stoi(k);
	return v < kMaxInstances ? v : -1;
}

WindowSettings window_from_value(const JValue &obj)
{
	WindowSettings w;
	if (const JValue *v = obj.find("preset"); v && v->type == JValue::String)
		w.preset = preset_from_name(v->s);
	if (const JValue *v = obj.find("safe_areas"); v && v->type == JValue::Bool)
		w.safe_areas = v->b;
	if (const JValue *v = obj.find("meters"); v && v->type == JValue::Bool)
		w.meters = v->b;
	if (const JValue *cols = obj.find("collections"); cols && cols->type == JValue::Object) {
		for (size_t i = 0; i < cols->keys.size(); ++i) {
			const JValue &list = cols->items[i];
			if (list.type != JValue::Array)
				continue;
			std::vector<std::string> slots;
			for (const JValue &e : list.items) {
				if (slots.size() >= kMaxSlotsStored)
					break;
				/* null / wrong type / a name with a NUL: empty slot. */
				if (e.type == JValue::String && e.s.find('\0') == std::string::npos)
					slots.push_back(e.s);
				else
					slots.emplace_back();
			}
			w.collections[cols->keys[i]] = std::move(slots);
		}
	}
	return w;
}

/* A JSON number as a percentage: rounded, clamped to [0, 100]. Any other type
 * keeps `fallback`. */
int percent_from_value(const JValue *v, int fallback)
{
	if (!v || v->type != JValue::Number)
		return fallback;
	const double d = std::strtod(v->s.c_str(), nullptr);
	if (!(d > 0.0))
		return 0; /* also NaN */
	if (d >= 100.0)
		return 100;
	return clamp_percent(static_cast<long long>(d + 0.5));
}

Appearance appearance_from_value(const JValue &obj)
{
	Appearance a;
	if (const JValue *v = obj.find("border_color"); v && v->type == JValue::String) {
		uint32_t rgb;
		if (parse_color(v->s, rgb))
			a.border_rgb = rgb;
	}
	a.label_opacity = percent_from_value(obj.find("label_opacity"), a.label_opacity);
	a.meter_opacity = percent_from_value(obj.find("meter_opacity"), a.meter_opacity);
	return a;
}

} // namespace

const char *preset_name(Preset preset)
{
	for (const PresetName &p : kPresetNames)
		if (p.preset == preset)
			return p.name;
	return "scenes8";
}

Preset preset_from_name(const std::string &name)
{
	for (const PresetName &p : kPresetNames)
		if (name == p.name)
			return p.preset;
	return Preset::Scenes8;
}

std::string to_json(const SettingsFile &file)
{
	const Appearance app = normalized(file.appearance);
	std::string out = "{\n  \"version\": 1,\n  \"global\": {\n    \"appearance\": {\n";
	out += "      \"border_color\": \"" + format_color(app.border_rgb) + "\",\n";
	out += "      \"label_opacity\": " + std::to_string(app.label_opacity) + ",\n";
	out += "      \"meter_opacity\": " + std::to_string(app.meter_opacity) + "\n";
	out += "    }\n  },\n  \"instances\": {";
	bool first_instance = true;
	for (const auto &inst : file.instances) {
		if (inst.first < 0 || inst.first >= kMaxInstances)
			continue;
		const WindowSettings &w = inst.second;
		out += first_instance ? "\n" : ",\n";
		first_instance = false;
		out += "    \"" + std::to_string(inst.first) + "\": {\n";
		out += std::string("      \"preset\": \"") + preset_name(w.preset) + "\",\n";
		out += std::string("      \"safe_areas\": ") + (w.safe_areas ? "true" : "false") + ",\n";
		out += std::string("      \"meters\": ") + (w.meters ? "true" : "false") + ",\n";
		out += "      \"collections\": {";
		bool first_col = true;
		for (const auto &col : w.collections) {
			out += first_col ? "\n" : ",\n";
			first_col = false;
			out += "        ";
			write_string(out, col.first);
			out += ": [";
			const size_t n = col.second.size() < kMaxSlotsStored ? col.second.size() : kMaxSlotsStored;
			for (size_t i = 0; i < n; ++i) {
				if (i)
					out.push_back(',');
				if (col.second[i].empty())
					out += "null";
				else
					write_string(out, col.second[i]);
			}
			out.push_back(']');
		}
		out += first_col ? "}\n" : "\n      }\n";
		out += "    }";
	}
	out += first_instance ? "}\n}\n" : "\n  }\n}\n";
	return out;
}

SettingsFile from_json(const std::string &json, ParseStatus *status)
{
	auto fail = [status](ParseStatus st) {
		if (status)
			*status = st;
		return SettingsFile();
	};
	if (json.empty()) {
		if (status)
			*status = ParseStatus::Ok;
		return SettingsFile();
	}
	if (json.size() > kMaxSettingsBytes)
		return fail(ParseStatus::Corrupt);

	JValue root;
	Parser parser(json.data(), json.size());
	if (!parser.parse_document(root) || root.type != JValue::Object)
		return fail(ParseStatus::Corrupt);

	const JValue *version = root.find("version");
	if (!version || version->type != JValue::Number || version->s != "1")
		return fail(ParseStatus::WrongVersion);

	SettingsFile file;
	if (const JValue *global = root.find("global"); global && global->type == JValue::Object) {
		if (const JValue *app = global->find("appearance"); app && app->type == JValue::Object)
			file.appearance = appearance_from_value(*app);
	}
	if (const JValue *insts = root.find("instances"); insts && insts->type == JValue::Object) {
		for (size_t i = 0; i < insts->keys.size(); ++i) {
			const int idx = parse_instance_key(insts->keys[i]);
			if (idx < 0 || insts->items[i].type != JValue::Object)
				continue;
			file.instances[idx] = window_from_value(insts->items[i]);
		}
	}
	if (status)
		*status = ParseStatus::Ok;
	return file;
}

LoadAction decide_load(LoadOutcome outcome, ParseStatus status)
{
	switch (outcome) {
	case LoadOutcome::Missing:
		return LoadAction::UseDefaults;
	case LoadOutcome::Unreadable:
		return LoadAction::BlockWrites;
	case LoadOutcome::Read:
		return status == ParseStatus::Ok ? LoadAction::UseParsed : LoadAction::BackupAndUseDefaults;
	}
	return LoadAction::BlockWrites;
}

void note_collection_saved(WindowSettings &w, const std::string &name, size_t max_collections)
{
	/* Normalised order: unlisted names first (oldest), then the listed ones,
	 * each once, and `name` last. */
	std::vector<std::string> order;
	order.reserve(w.collections.size() + 1);
	for (const auto &c : w.collections) {
		if (c.first != name && std::find(w.collection_order.begin(), w.collection_order.end(), c.first) ==
					       w.collection_order.end())
			order.push_back(c.first);
	}
	for (const std::string &n : w.collection_order)
		if (n != name && w.collections.count(n) && std::find(order.begin(), order.end(), n) == order.end())
			order.push_back(n);
	if (w.collections.count(name))
		order.push_back(name);

	size_t drop = 0;
	while (order.size() - drop > max_collections && drop < order.size()) {
		w.collections.erase(order[drop]);
		++drop;
	}
	order.erase(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(drop));
	w.collection_order = std::move(order);
}

void merge_settings(SettingsFile &base, const SettingsFile &overlay, bool overlay_appearance_touched)
{
	if (overlay_appearance_touched)
		base.appearance = overlay.appearance;
	for (const auto &inst : overlay.instances) {
		WindowSettings &dst = base.instances[inst.first];
		if (inst.second.options_touched) {
			dst.preset = inst.second.preset;
			dst.safe_areas = inst.second.safe_areas;
			dst.meters = inst.second.meters;
			dst.options_touched = true;
		}
		for (const auto &col : inst.second.collections)
			dst.collections[col.first] = col.second;
	}
}

bool may_save_assignment(bool switching, const std::string &window_collection, const std::string &current_collection)
{
	return !switching && window_collection == current_collection;
}
