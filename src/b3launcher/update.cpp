// update.cpp -- see update.h
#include "update.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace b3launcher::update {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

// `s` cut to at most `max` bytes, at a UTF-8 character's start
std::string cap(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t at = max;
    while (at > 0 && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) --at;
    s.resize(at);
    return s;
}

// Text from the network as one line: control characters (line breaks, tabs, escapes) become spaces, runs of spaces one,
// trimmed and capped
std::string clean(std::string_view in, std::size_t max) {
    std::string out;
    out.reserve(std::min(in.size(), max + 8));
    for (unsigned char c : in) {
        const bool ctl = c < 0x20 || c == 0x7F;
        if (ctl || c == ' ') {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += char(c);
        }
        if (out.size() > max + 4) break;
    }
    return cap(std::string(trim(out)), max);
}

bool is_digits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

bool parse_int(std::string_view s, int& out) {
    if (!is_digits(s) || s.size() > 9 || (s.size() > 1 && s[0] == '0')) return false;
    out = std::atoi(std::string(s).c_str());
    return true;
}

// ---- the JSON reader ----
struct Reader {
    std::string_view t;
    std::size_t at = 0;
    std::string err;

    bool fail(const std::string& what) {
        if (err.empty()) err = what + " at byte " + std::to_string(at);
        return false;
    }
    void ws() {
        while (at < t.size() && (t[at] == ' ' || t[at] == '\t' || t[at] == '\n' || t[at] == '\r')) ++at;
    }
    static void put_utf8(std::string& o, std::uint32_t cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) {
            o += char(0xC0 | (cp >> 6));
            o += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += char(0xE0 | (cp >> 12));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        } else {
            o += char(0xF0 | (cp >> 18));
            o += char(0x80 | ((cp >> 12) & 0x3F));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(std::uint32_t& v) {
        if (at + 4 > t.size()) return fail("a short \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t[at++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= std::uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= std::uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= std::uint32_t(c - 'A' + 10);
            else return fail("a bad \\u escape");
        }
        return true;
    }
    bool string(std::string& o) {
        ++at;   // the opening quote
        while (true) {
            if (at >= t.size()) return fail("an unterminated string");
            const unsigned char c = static_cast<unsigned char>(t[at]);
            if (c == '"') {
                ++at;
                return true;
            }
            if (c < 0x20) return fail("a control character in a string");
            if (c != '\\') {
                o += char(c);
                ++at;
                continue;
            }
            if (++at >= t.size()) return fail("an unterminated escape");
            const char e = t[at++];
            switch (e) {
            case '"': o += '"'; break;
            case '\\': o += '\\'; break;
            case '/': o += '/'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'n': o += '\n'; break;
            case 'r': o += '\r'; break;
            case 't': o += '\t'; break;
            case 'u': {
                std::uint32_t cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00) {   // a high surrogate: its low one should follow
                    std::uint32_t lo = 0;
                    if (at + 6 <= t.size() && t[at] == '\\' && t[at + 1] == 'u') {
                        const std::size_t keep = at;
                        at += 2;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else {
                            at = keep;
                            cp = 0xFFFD;
                        }
                    } else {
                        cp = 0xFFFD;
                    }
                } else if (cp >= 0xDC00 && cp < 0xE000) {
                    cp = 0xFFFD;   // a lone low surrogate
                }
                put_utf8(o, cp);
                break;
            }
            default: --at; return fail("an unknown escape");
            }
        }
    }
    bool number(double& n) {
        const std::size_t start = at;
        if (at < t.size() && t[at] == '-') ++at;
        if (at >= t.size()) return fail("a bad number");
        if (t[at] == '0') ++at;
        else if (t[at] >= '1' && t[at] <= '9')
            while (at < t.size() && std::isdigit(static_cast<unsigned char>(t[at]))) ++at;
        else return fail("a bad number");
        if (at < t.size() && t[at] == '.') {
            ++at;
            if (at >= t.size() || !std::isdigit(static_cast<unsigned char>(t[at]))) return fail("a bad number");
            while (at < t.size() && std::isdigit(static_cast<unsigned char>(t[at]))) ++at;
        }
        if (at < t.size() && (t[at] == 'e' || t[at] == 'E')) {
            ++at;
            if (at < t.size() && (t[at] == '+' || t[at] == '-')) ++at;
            if (at >= t.size() || !std::isdigit(static_cast<unsigned char>(t[at]))) return fail("a bad number");
            while (at < t.size() && std::isdigit(static_cast<unsigned char>(t[at]))) ++at;
        }
        n = std::strtod(std::string(t.substr(start, at - start)).c_str(), nullptr);
        return true;
    }
    bool literal(std::string_view word) {
        if (t.substr(at, word.size()) != word) return fail("an unexpected character");
        at += word.size();
        return true;
    }
    bool value(Json& v, int depth) {
        if (depth >= 64) return fail("nesting too deep");
        ws();
        if (at >= t.size()) return fail("the text ends early");
        const char c = t[at];
        if (c == '{') {
            v.kind = Json::Kind::kObject;
            ++at;
            ws();
            if (at < t.size() && t[at] == '}') {
                ++at;
                return true;
            }
            while (true) {
                ws();
                if (at >= t.size() || t[at] != '"') return fail("a member name expected");
                std::string key;
                if (!string(key)) return false;
                ws();
                if (at >= t.size() || t[at] != ':') return fail("':' expected");
                ++at;
                Json m;
                if (!value(m, depth + 1)) return false;
                v.members.emplace_back(std::move(key), std::move(m));
                ws();
                if (at < t.size() && t[at] == ',') {
                    ++at;
                    continue;
                }
                if (at < t.size() && t[at] == '}') {
                    ++at;
                    return true;
                }
                return fail("',' or '}' expected");
            }
        }
        if (c == '[') {
            v.kind = Json::Kind::kArray;
            ++at;
            ws();
            if (at < t.size() && t[at] == ']') {
                ++at;
                return true;
            }
            while (true) {
                Json m;
                if (!value(m, depth + 1)) return false;
                v.items.push_back(std::move(m));
                ws();
                if (at < t.size() && t[at] == ',') {
                    ++at;
                    continue;
                }
                if (at < t.size() && t[at] == ']') {
                    ++at;
                    return true;
                }
                return fail("',' or ']' expected");
            }
        }
        if (c == '"') {
            v.kind = Json::Kind::kString;
            return string(v.s);
        }
        if (c == 't' || c == 'f') {
            v.kind = Json::Kind::kBool;
            v.b = c == 't';
            return literal(v.b ? "true" : "false");
        }
        if (c == 'n') {
            v.kind = Json::Kind::kNull;
            return literal("null");
        }
        v.kind = Json::Kind::kNumber;
        return number(v.n);
    }
};

// A Markdown line in plain words: [text](url) / ![alt](url) as their text, ** __ ` taken out
std::string plain_markdown(std::string_view s) {
    std::string o;
    for (std::size_t k = 0; k < s.size();) {
        if ((s[k] == '[' || (s[k] == '!' && k + 1 < s.size() && s[k + 1] == '[')) ) {
            const std::size_t open = s[k] == '!' ? k + 1 : k;
            const std::size_t close = s.find(']', open);
            if (close != std::string_view::npos && close + 1 < s.size() && s[close + 1] == '(') {
                const std::size_t end = s.find(')', close + 2);
                if (end != std::string_view::npos) {
                    o.append(s.substr(open + 1, close - open - 1));
                    k = end + 1;
                    continue;
                }
            }
        }
        if ((s[k] == '*' || s[k] == '_') && k + 1 < s.size() && s[k + 1] == s[k]) {
            k += 2;
            continue;
        }
        if (s[k] == '`') {
            ++k;
            continue;
        }
        o += s[k++];
    }
    return o;
}

std::string escape_ini(std::string_view s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}

std::string unescape_ini(std::string_view s) {
    std::string o;
    for (std::size_t k = 0; k < s.size(); ++k) {
        if (s[k] == '\\' && k + 1 < s.size()) {
            ++k;
            o += s[k] == 'n' ? '\n' : s[k];
        } else {
            o += s[k];
        }
    }
    return o;
}

bool good_page_url(std::string_view u) {
    constexpr std::string_view kPrefix = "https://github.com/";
    if (u.size() <= kPrefix.size() || u.size() > 400 || u.substr(0, kPrefix.size()) != kPrefix) return false;
    for (unsigned char c : u)
        if (c <= 0x20 || c >= 0x7F || c == '"' || c == '<' || c == '>' || c == '\\') return false;
    return true;
}

}  // namespace

std::string user_agent(std::string_view version) { return "b3port-launcher/" + std::string(version.empty() ? "0.0.0" : version); }

// ---- versions ----

std::optional<Version> parse_version(std::string_view s) {
    s = trim(s);
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.remove_prefix(1);
    if (const std::size_t plus = s.find('+'); plus != std::string_view::npos) s = s.substr(0, plus);
    std::string_view core = s, pre;
    if (const std::size_t dash = s.find('-'); dash != std::string_view::npos) {
        core = s.substr(0, dash);
        pre = s.substr(dash + 1);
        if (pre.empty()) return std::nullopt;
    }
    Version v;
    int* parts[3] = {&v.major, &v.minor, &v.patch};
    std::size_t at = 0;
    for (int k = 0; k < 3; ++k) {
        const std::size_t dot = k < 2 ? core.find('.', at) : core.size();
        if (dot == std::string_view::npos) return std::nullopt;
        if (!parse_int(core.substr(at, dot - at), *parts[k])) return std::nullopt;
        at = dot + 1;
    }
    for (std::size_t p = 0; p <= pre.size() && !pre.empty();) {
        const std::size_t dot = std::min(pre.find('.', p), pre.size());
        const std::string_view id = pre.substr(p, dot - p);
        if (id.empty()) return std::nullopt;
        for (char c : id)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-')) return std::nullopt;
        v.pre.emplace_back(id);
        p = dot + 1;
    }
    return v;
}

int compare(const Version& a, const Version& b) {
    auto cmp = [](long long x, long long y) { return x < y ? -1 : x > y ? 1 : 0; };
    if (int c = cmp(a.major, b.major)) return c;
    if (int c = cmp(a.minor, b.minor)) return c;
    if (int c = cmp(a.patch, b.patch)) return c;
    if (a.pre.empty() || b.pre.empty()) return a.pre.empty() == b.pre.empty() ? 0 : a.pre.empty() ? 1 : -1;
    for (std::size_t k = 0; k < std::min(a.pre.size(), b.pre.size()); ++k) {
        const std::string &x = a.pre[k], &y = b.pre[k];
        const bool xn = is_digits(x), yn = is_digits(y);
        if (xn && yn) {   // numerically (as lengths first: no overflow on long numbers)
            const std::string_view xs = std::string_view(x).substr(std::min(x.find_first_not_of('0'), x.size() - 1));
            const std::string_view ys = std::string_view(y).substr(std::min(y.find_first_not_of('0'), y.size() - 1));
            if (xs.size() != ys.size()) return xs.size() < ys.size() ? -1 : 1;
            if (int c = xs.compare(ys)) return c < 0 ? -1 : 1;
        } else if (xn != yn) {
            return xn ? -1 : 1;
        } else if (int c = x.compare(y)) {
            return c < 0 ? -1 : 1;
        }
    }
    return cmp((long long)a.pre.size(), (long long)b.pre.size());
}

Version as_running(Version v) {
    if (!v.pre.empty()) {
        const std::string& p = v.pre[0];
        if (p == "test" || (p.size() > 4 && p.compare(0, 5, "test-") == 0)) v.pre.clear();
    }
    return v;
}

bool is_newer(std::string_view tag, std::string_view running) {
    const std::optional<Version> t = parse_version(tag), r = parse_version(running);
    if (!t || !r) return false;
    return compare(*t, as_running(*r)) > 0;
}

// ---- JSON ----

const Json* Json::get(std::string_view key) const {
    if (kind != Kind::kObject) return nullptr;
    for (const auto& [k, v] : members)
        if (k == key) return &v;
    return nullptr;
}

const std::string* Json::str(std::string_view key) const {
    const Json* v = get(key);
    return v && v->kind == Kind::kString ? &v->s : nullptr;
}

std::optional<Json> parse_json(std::string_view text, std::string* why) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);   // a UTF-8 byte order mark
    Reader r;
    r.t = text;
    Json v;
    if (!r.value(v, 0)) {
        if (why) *why = r.err;
        return std::nullopt;
    }
    r.ws();
    if (r.at != text.size()) {
        if (why) *why = "text after the value at byte " + std::to_string(r.at);
        return std::nullopt;
    }
    return v;
}

// ---- the release ----

std::vector<std::string> first_lines(std::string_view body, std::size_t n) {
    std::vector<std::string> out;
    std::size_t at = 0;
    bool in_comment = false;
    while (at <= body.size() && out.size() < n) {
        const std::size_t end = std::min(body.find('\n', at), body.size());
        std::string_view l = trim(body.substr(at, end - at));
        at = end + 1;
        if (in_comment) {   // an HTML comment over several lines
            if (l.find("-->") != std::string_view::npos) in_comment = false;
            continue;
        }
        if (l.substr(0, 4) == "<!--") {
            in_comment = l.find("-->") == std::string_view::npos;
            continue;
        }
        while (!l.empty() && l[0] == '#') l.remove_prefix(1);
        l = trim(l);
        if (l.size() >= 2 && (l[0] == '-' || l[0] == '*' || l[0] == '+' || l[0] == '>') && l[1] == ' ') l = trim(l.substr(2));
        if (l.find_first_not_of("-=*_ ") == std::string_view::npos) continue;   // empty, or a rule
        std::string s = clean(plain_markdown(l), 200);
        if (!s.empty()) out.push_back(std::move(s));
        if (end >= body.size()) break;
    }
    return out;
}

bool parse_release(std::string_view json, Release& out, std::string* why) {
    std::string err;
    const std::optional<Json> j = parse_json(json, &err);
    if (!j) {
        if (why) *why = "not JSON (" + err + ")";
        return false;
    }
    if (j->kind != Json::Kind::kObject) {
        if (why) *why = "not a release";
        return false;
    }
    const std::string* tag = j->str("tag_name");
    if (!tag || clean(*tag, 60).empty()) {
        if (why) *why = "no tag_name in the answer";
        return false;
    }
    Release r;
    r.tag = clean(*tag, 60);
    if (const std::string* s = j->str("name")) r.name = clean(*s, 120);
    if (r.name.empty()) r.name = r.tag;
    // The page the Download button opens: github.com only (else the repository's releases page)
    if (const std::string* s = j->str("html_url"); s && good_page_url(*s)) r.url = *s;
    else r.url = kReleasesPage;
    if (const std::string* s = j->str("published_at")) r.published = clean(*s, 40);
    if (const Json* p = j->get("prerelease"); p && p->kind == Json::Kind::kBool) r.prerelease = p->b;
    if (const std::string* s = j->str("body")) r.notes = first_lines(std::string_view(*s).substr(0, 64 * 1024), 3);
    if (const Json* as = j->get("assets"); as && as->kind == Json::Kind::kArray) {   // 1.1 one-click updates
        const std::string_view downloads = kDownloads;   // macintel1: the repository update.h checks
        for (const Json& a : as->items) {
            if (a.kind != Json::Kind::kObject || r.assets.size() >= 64) continue;
            const std::string* name = a.str("name");
            const std::string* url = a.str("browser_download_url");
            if (!name || !url || name->empty() || name->size() > 200 || url->compare(0, downloads.size(), downloads) != 0) continue;
            if (name->find_first_of("/\\") != std::string::npos || *name != clean(*name, 200)) continue;
            Release::Asset x;
            x.name = *name;
            x.url = *url;
            if (const Json* sz = a.get("size"); sz && sz->kind == Json::Kind::kNumber && sz->n >= 0) x.size = std::uint64_t(sz->n);
            r.assets.push_back(std::move(x));
        }
    }
    out = std::move(r);
    return true;
}

Platform this_platform() {
#if defined(_WIN32)
    return Platform::kWindows;
#elif defined(__APPLE__)
    return Platform::kMac;
#else
    return Platform::kLinux;
#endif
}

const Release::Asset* find_asset(const Release& r, std::string_view name) {
    for (const Release::Asset& a : r.assets)
        if (a.name == name) return &a;
    return nullptr;
}

// macintel1: an Intel Mac's launcher updates to the Intel package (tools/release/package_mac.sh names it by the CPU)
#if defined(__APPLE__) && defined(__x86_64__)
constexpr std::string_view kMacPackageTail = "-macos-x86_64.dmg";
#else
constexpr std::string_view kMacPackageTail = "-macos-arm64.dmg";
#endif

const Release::Asset* package_asset(const Release& r, Platform p) {
    const std::string_view tail = p == Platform::kWindows ? "-win64.zip" : p == Platform::kMac ? kMacPackageTail : "-linux-x86_64.tar.gz";
    const Release::Asset* found = nullptr;
    for (const Release::Asset& a : r.assets) {
        if (a.name.size() <= tail.size() || a.name.compare(a.name.size() - tail.size(), tail.size(), tail) != 0) continue;
        if (a.name.rfind("burnout3-native-", 0) != 0) continue;
        if (found) return nullptr;   // two candidates: none (the player picks on the page)
        found = &a;
    }
    return found;
}

std::string sum_for(std::string_view sums, std::string_view name) {
    std::string hit;
    std::size_t at = 0;
    while (at < sums.size()) {
        std::size_t nl = sums.find('\n', at);
        if (nl == std::string_view::npos) nl = sums.size();
        std::string_view line = sums.substr(at, nl - at);
        at = nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() < 66) continue;
        const std::string_view hash = line.substr(0, 64);
        bool hex = true;
        for (const char c : hash) hex = hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
        if (!hex || line[64] != ' ' || (line[65] != ' ' && line[65] != '*')) continue;
        if (line.substr(66) != name) continue;
        std::string h(hash);
        for (char& c : h) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (!hit.empty() && hit != h) return {};
        hit = h;
    }
    return hit;
}

Response response_from_file_text(std::string_view text) {
    Response r;
    r.ok = true;
    r.status = 200;
    if (text.substr(0, 5) == "HTTP/") {
        const std::size_t sp = text.find(' ');
        r.status = sp == std::string_view::npos ? 0 : std::atoi(std::string(text.substr(sp + 1, 3)).c_str());
        std::size_t body = text.find("\n\n");
        std::size_t skip = 2;
        if (const std::size_t crlf = text.find("\r\n\r\n"); crlf != std::string_view::npos && (body == std::string_view::npos || crlf < body)) {
            body = crlf;
            skip = 4;
        }
        r.body = body == std::string_view::npos ? std::string() : std::string(text.substr(body + skip));
        if (r.status < 100 || r.status > 599) {
            r.ok = false;
            r.status = 0;
            r.error = "a bad status line";
        }
        return r;
    }
    r.body = std::string(text);
    return r;
}

// ---- the cache ----

bool due(const Cache& c, std::int64_t now) {
    if (!c.enabled) return false;
    return c.checked_at <= 0 || now < c.checked_at || now - c.checked_at >= kInterval;
}

void apply(Cache& c, const Response& r, std::int64_t now) {
    c.checked_at = now;
    if (!r.ok) {
        c.error = clean(r.error.empty() ? "no answer" : r.error, 160);
        return;
    }
    if (r.status == 200) {
        Release rel;
        std::string why;
        if (parse_release(r.body, rel, &why)) {
            c.latest = std::move(rel);
            c.none = false;
            c.error.clear();
        } else {
            c.error = clean("an unexpected answer: " + why, 160);
        }
        return;
    }
    if (r.status == 404) {   // the repository has no release yet (or is not public yet)
        c.latest = Release{};
        c.none = true;
        c.error.clear();
        return;
    }
    if (r.status == 403 || r.status == 429) c.error = "GitHub refused the request (HTTP " + std::to_string(r.status) + "; too many requests from this address?)";
    else c.error = "HTTP " + std::to_string(r.status);
}

void write_ini(std::ostream& o, const Cache& c) {
    o << "# update check (Settings > Launcher): on / off, and the last answer from GitHub's releases\n";
    o << "update_check = " << (c.enabled ? 1 : 0) << "\n";
    o << "update_checked_at = " << c.checked_at << "\n";
    o << "update_error = " << escape_ini(c.error) << "\n";
    o << "update_none = " << (c.none ? 1 : 0) << "\n";
    o << "update_tag = " << escape_ini(c.latest.tag) << "\n";
    o << "update_name = " << escape_ini(c.latest.name) << "\n";
    o << "update_url = " << escape_ini(c.latest.url) << "\n";
    o << "update_published = " << escape_ini(c.latest.published) << "\n";
    o << "update_prerelease = " << (c.latest.prerelease ? 1 : 0) << "\n";
    std::string notes;
    for (const std::string& l : c.latest.notes) notes += (notes.empty() ? "" : "\n") + l;
    o << "update_notes = " << escape_ini(notes) << "\n";
}

bool read_ini_key(Cache& c, std::string_view key, std::string_view value) {
    const std::string v = unescape_ini(value);
    if (key == "update_check") c.enabled = v != "0";
    else if (key == "update_checked_at") c.checked_at = std::atoll(v.c_str());
    else if (key == "update_error") c.error = clean(v, 160);
    else if (key == "update_none") c.none = v == "1";
    else if (key == "update_tag") c.latest.tag = clean(v, 60);
    else if (key == "update_name") c.latest.name = clean(v, 120);
    else if (key == "update_url") c.latest.url = good_page_url(v) ? v : std::string(v.empty() ? "" : kReleasesPage);
    else if (key == "update_published") c.latest.published = clean(v, 40);
    else if (key == "update_prerelease") c.latest.prerelease = v == "1";
    else if (key == "update_notes") {
        c.latest.notes.clear();
        std::size_t at = 0;
        while (at < v.size() && c.latest.notes.size() < 3) {
            const std::size_t end = std::min(v.find('\n', at), v.size());
            std::string l = clean(std::string_view(v).substr(at, end - at), 200);
            if (!l.empty()) c.latest.notes.push_back(std::move(l));
            at = end + 1;
        }
    } else {
        return false;
    }
    return true;
}

State state(const Cache& c, std::string_view running, bool checking) {
    if (!c.enabled) return State::kOff;
    if (!c.latest.tag.empty() && is_newer(c.latest.tag, running)) return State::kAvailable;
    if (checking) return State::kChecking;
    if (c.checked_at <= 0) return State::kNotChecked;
    if (!c.error.empty()) return State::kError;
    if (c.none) return State::kNoRelease;
    if (!c.latest.tag.empty()) return State::kUpToDate;
    return State::kNotChecked;
}

std::string status_text(const Cache& c, State st) {
    switch (st) {
    case State::kOff: return "Update check off (Settings > Launcher)";
    case State::kNotChecked: return "Not checked for updates yet";
    case State::kChecking: return "Checking for updates...";
    case State::kAvailable: {
        std::string s = "Update available: " + c.latest.tag;
        if (!c.latest.name.empty() && c.latest.name != c.latest.tag) s += " — " + c.latest.name;
        if (c.latest.prerelease) s += " (pre-release)";
        return s;
    }
    case State::kUpToDate: return "Up to date (" + c.latest.tag + " is the latest release)";
    case State::kNoRelease: return "Up to date (no release published yet)";
    case State::kError: return "Couldn't check for updates (" + c.error + ")";
    }
    return {};
}

std::string published_date(std::string_view p) {
    if (p.size() < 10) return {};
    for (std::size_t k = 0; k < 10; ++k)
        if (k == 4 || k == 7 ? p[k] != '-' : !std::isdigit(static_cast<unsigned char>(p[k]))) return {};
    return std::string(p.substr(0, 10));
}

}  // namespace b3launcher::update
