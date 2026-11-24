// update.h -- update1: the launcher tells the player when a newer release of the port is out (no SDL; the request
// itself is update_net.h, tests/test_launcher_update.cpp).
//
// Each time the launcher starts (1.2.1; before: once a day), unless it checked under kInterval ago, and on "Check now"
// in the launcher's Settings, when the player has not turned it off, the launcher asks GitHub's API for the repository's latest release:
//   GET https://api.github.com/repos/u0rhs/burnout3-native/releases/latest
//   User-Agent: b3port-launcher/<version>, Accept: application/vnd.github+json (nothing else is sent)
// and compares its tag ("v0.2.0") with the running build's version (b3report/version.h: "0.1.0-test+1a2b3c4d"). The
// answer is remembered in launcher.ini (the update_* keys), so a quick restart (within kInterval) shows the same result
// without asking again. The check itself downloads nothing. The Play page's banner opens the release page in the system
// browser; with a release key built in (update_key.h) it also offers the one-click update, which downloads the
// release's package, checks it against the signed hash list and installs it (oneclick.h, installer.h).
#ifndef B3LAUNCHER_UPDATE_H
#define B3LAUNCHER_UPDATE_H

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b3launcher::update {

// macintel1 (the Intel Mac fork only): an Intel Mac's build checks the fork's releases, which carry the Intel package
// (upstream's have none). The fork's tags and VERSION carry the fork's release ("1.3.0-intel.2"), so a fork-only release
// of the same upstream version is newer than the last one.
#if defined(__APPLE__) && defined(__x86_64__)
#define B3_UPDATE_REPO "vorno-crole/burnout3-native"
#else
#define B3_UPDATE_REPO "u0rhs/burnout3-native"
#endif
inline constexpr const char* kLatestUrl = "https://api.github.com/repos/" B3_UPDATE_REPO "/releases/latest";
inline constexpr const char* kReleasesPage = "https://github.com/" B3_UPDATE_REPO "/releases";
// where a release's files are downloaded from: parse_release keeps only these
inline constexpr const char* kDownloads = "https://github.com/" B3_UPDATE_REPO "/releases/download/";
inline constexpr std::int64_t kInterval = 5 * 60;   // 1.2.1: each launcher start checks, unless the last check was under 5 minutes ago
// "b3port-launcher/0.1.0-test+1a2b3c4d": the request's User-Agent (GitHub's API refuses a request without one)
std::string user_agent(std::string_view version);

// ---- versions (Semantic Versioning 2.0: MAJOR.MINOR.PATCH[-pre.release][+build]) ----
struct Version {
    int major = 0, minor = 0, patch = 0;
    std::vector<std::string> pre;   // the pre-release identifiers ("rc", "1"); empty: a release
};
// "v0.2.0", "0.1.0-test+1a2b3c4d", "0.2.0-rc.1"; a leading 'v' / 'V' and the build metadata ("+...") are ignored.
// nullopt: not a version.
std::optional<Version> parse_version(std::string_view s);
// SemVer precedence: -1 (a before b), 0, +1. A pre-release comes before its release; identifiers compare numerically
// when both are numbers, else as text, and a number before text.
int compare(const Version& a, const Version& b);
// The running build as compared with releases: a test build ("-test", "-test.2", "-test-...") counts as its base version
// ("0.1.0-test+1a2b" is 0.1.0); any other pre-release stays one.
Version as_running(Version v);
// Whether the release tagged `tag` is newer than the running build `running` (b3port::version()). False when either
// is not a version: an unknown build never nags.
bool is_newer(std::string_view tag, std::string_view running);

// ---- JSON (a small reader for the API's answer: UTF-8, objects, arrays, strings with escapes, numbers, literals) ----
struct Json {
    enum class Kind : std::uint8_t { kNull, kBool, kNumber, kString, kArray, kObject };
    Kind kind = Kind::kNull;
    bool b = false;
    double n = 0;
    std::string s;                                       // kString (UTF-8, escapes resolved)
    std::vector<Json> items;                             // kArray
    std::vector<std::pair<std::string, Json>> members;   // kObject, in the text's order
    const Json* get(std::string_view key) const;         // an object's member (the first of that name); nullptr: none
    const std::string* str(std::string_view key) const;  // ... when it is a string
};
// nullopt (why: where and what) on a syntax error, nesting deeper than 64 or trailing text
std::optional<Json> parse_json(std::string_view text, std::string* why = nullptr);

// ---- the release ----
struct Release {
    std::string tag;          // tag_name: "v0.2.0"
    std::string name;         // name: "Takedown at 60" (the tag when empty)
    std::string url;          // html_url: the release's page on github.com
    std::string published;    // published_at: "2026-12-01T18:00:00Z"
    bool prerelease = false;
    std::vector<std::string> notes;   // the body's first lines (Markdown marks taken off)
    // 1.1 one-click updates: the release's files (assets: name, browser_download_url, size); only downloads from this
    // repository's releases on github.com are kept. Not remembered in launcher.ini (the install asks again).
    struct Asset {
        std::string name, url;
        std::uint64_t size = 0;
        bool operator==(const Asset&) const = default;
    };
    std::vector<Asset> assets;
    bool operator==(const Release&) const = default;
};
// 1.1: the package of this system in a release (Windows "...-win64.zip", Linux "...-linux-x86_64.tar.gz", macOS
// "...-macos-arm64.dmg", an Intel Mac's build "...-macos-x86_64.dmg"), and its signed hash list ("SHA256SUMS" and "SHA256SUMS.sig"); nullptr when it has none
enum class Platform : std::uint8_t { kWindows, kLinux, kMac };
Platform this_platform();
const Release::Asset* package_asset(const Release& r, Platform p);
const Release::Asset* find_asset(const Release& r, std::string_view name);
// `sha256sum` output ("<64 hex>  <name>" or "<64 hex> *<name>" per line) -> the hash (lower case) listed for `name`;
// empty when it is not listed (or listed twice with different hashes)
std::string sum_for(std::string_view sums, std::string_view name);
// GitHub's releases/latest answer. False (why) when it is not one (not JSON, no tag_name). The launcher opens only pages
// on https://github.com/: another html_url (or none) becomes the repository's releases page. Text is cleaned of
// control characters and capped.
bool parse_release(std::string_view json, Release& r, std::string* why = nullptr);
// The first `n` non-empty lines of a release body (Markdown): headings' #, list marks, ** / __ / ` and [text](url)
// links reduced to their words, each line capped at 200 bytes
std::vector<std::string> first_lines(std::string_view body, std::size_t n = 3);

// ---- the answer of one request ----
struct Response {
    bool ok = false;          // an HTTP answer came (any status); false: no connection, TLS failure, timeout ...
    int status = 0;
    std::string body;
    std::string error;        // !ok: why, in a few words
};
// The test hook's file (B3LAUNCHER_UPDATE_FILE, the --screenshot checks): the JSON body alone is a 200 answer;
// a file starting "HTTP/1.1 404 Not Found" has its status there and the body after the first empty line.
Response response_from_file_text(std::string_view text);

// ---- what launcher.ini remembers ----
struct Cache {
    bool enabled = true;            // update_check: Settings > Launcher > CHECK FOR UPDATES (off: no request at all)
    std::int64_t checked_at = 0;    // update_checked_at: the last attempt (Unix seconds; 0: never)
    std::string error;              // update_error: the last attempt failed (why); empty: it got an answer
    bool none = false;              // update_none: the last answer was "no release yet" (404)
    Release latest;                 // update_tag, update_name, ...: the last release seen (kept when a later attempt fails)
    bool operator==(const Cache&) const = default;
};
// Whether a check is due at `now`: on, and never checked, or a day since the last, or the clock went back
bool due(const Cache& c, std::int64_t now);
// One request's result into the cache: the time, and the release (200), "none" (404) or the error (anything else)
void apply(Cache& c, const Response& r, std::int64_t now);
// launcher.ini: the update_* lines, and one key read back (false: not one of them)
void write_ini(std::ostream& o, const Cache& c);
bool read_ini_key(Cache& c, std::string_view key, std::string_view value);

enum class State : std::uint8_t { kOff, kNotChecked, kChecking, kAvailable, kUpToDate, kNoRelease, kError };
// What the launcher shows for this cache and running version (`checking`: a request is under way)
State state(const Cache& c, std::string_view running, bool checking);
// One line in words: "Update available: v0.2.0 — Takedown at 60", "Up to date (v0.1.0 is the latest release)",
// "Couldn't check for updates (timed out)", "Update check off", "Checking for updates...", ...
std::string status_text(const Cache& c, State st);
// "2026-12-01" from published_at (empty when it is not a date)
std::string published_date(std::string_view published_at);

}  // namespace b3launcher::update

#endif
