// update1: the launcher's update check without the network (src/b3launcher/update.h): version comparison, the JSON
// reader, GitHub's releases/latest answer (samples written here, never fetched), the test hook's file answers, the
// once-a-day rule, launcher.ini's update_* keys and the words shown.
#include "test.h"

#include "../src/b3launcher/update.h"

#include <sstream>

using namespace b3launcher::update;

namespace {

// A trimmed sample of GitHub's answer (the fields the launcher reads and some it ignores)
const char* kSample = R"({
  "url": "https://api.github.com/repos/u0rhs/burnout3-native/releases/1",
  "html_url": "https://github.com/u0rhs/burnout3-native/releases/tag/v0.2.0",
  "id": 123456789,
  "author": {"login": "u0rhs", "id": 1, "site_admin": false},
  "tag_name": "v0.2.0",
  "target_commitish": "main",
  "name": "Takedown at 60 \u2014 caf\u00e9",
  "draft": false,
  "prerelease": false,
  "created_at": "2026-12-01T17:55:00Z",
  "published_at": "2026-12-01T18:00:00Z",
  "assets": [
    {"name": "burnout3-native-0.2.0-windows.zip", "size": 31457280, "download_count": 0},
    {"name": "burnout3-native-0.2.0-linux.tar.gz", "size": 29360128, "download_count": 0}
  ],
  "tarball_url": null,
  "body": "## What's new\r\n\r\n- **Split screen** for two players\r\n- Fixed the [menu font](https://example.com) at 4K\r\n- Third\r\n- Fourth\r\n"
})";

int cmp(const char* a, const char* b) { return compare(*parse_version(a), *parse_version(b)); }

}  // namespace

TEST(versions_parse) {
    auto v = parse_version("v0.2.0");
    CHECK(v && v->major == 0 && v->minor == 2 && v->patch == 0 && v->pre.empty());
    v = parse_version("0.1.0-test+1a2b3c4d");
    CHECK(v && v->minor == 1 && v->pre.size() == 1 && v->pre[0] == "test");
    v = parse_version("V10.20.30-rc.1+build.5");
    CHECK(v && v->major == 10 && v->minor == 20 && v->patch == 30 && v->pre.size() == 2 && v->pre[1] == "1");
    v = parse_version("0.1.0-test+1a2b3c4d-dirty");
    CHECK(v && v->pre.size() == 1);
    for (const char* bad : {"", "v", "1", "1.2", "1.2.3.4", "1.2.x", "01.2.3", "1.2.3-", "1.2.3-a..b", "1.2.3-a$", "-1.2.3", "latest", "1.2.3 4"})
        CHECK(!parse_version(bad));
}

TEST(versions_compare) {
    CHECK(cmp("1.0.0", "1.0.0") == 0);
    CHECK(cmp("1.0.0", "2.0.0") < 0 && cmp("2.0.0", "1.0.0") > 0);
    CHECK(cmp("0.10.0", "0.9.0") > 0);   // numbers, not text
    CHECK(cmp("0.1.10", "0.1.9") > 0);
    // SemVer 2.0's own example order
    const char* order[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"};
    for (std::size_t k = 0; k + 1 < std::size(order); ++k) {
        CHECK(cmp(order[k], order[k + 1]) < 0);
        CHECK(cmp(order[k + 1], order[k]) > 0);
    }
    CHECK(cmp("1.0.0+a", "1.0.0+b") == 0);   // build metadata ignored
    CHECK(cmp("1.0.0-rc.99999999999999999999", "1.0.0-rc.100000000000000000000") < 0);   // long numbers
}

TEST(newer_than_the_running_build) {
    const char* running = "0.1.0-test+1a2b3c4d";   // a test build counts as 0.1.0
    CHECK(is_newer("v0.2.0", running));
    CHECK(is_newer("v0.1.1", running));
    CHECK(!is_newer("v0.1.0", running));             // the same release: up to date
    CHECK(!is_newer("v0.0.9", running));             // a build newer than the latest release says nothing
    CHECK(!is_newer("v0.1.0", "0.1.0-test"));
    CHECK(is_newer("v0.1.1", "0.1.0-test.3+abc-dirty"));
    CHECK(is_newer("v0.2.0-beta.1", running));       // a pre-release of a later version
    CHECK(!is_newer("v0.1.0-rc.1", running));        // ... but not one before the build's base
    CHECK(is_newer("v0.2.0", "0.2.0-rc.1"));         // a release candidate is older than its release
    CHECK(!is_newer("v0.2.0-rc.1", "0.2.0-rc.2"));
    CHECK(!is_newer("nightly", running) && !is_newer("v0.2.0", "unknown") && !is_newer("v0.2.0", ""));
    CHECK(as_running(*parse_version("0.1.0-test")).pre.empty() && as_running(*parse_version("0.1.0-test-2")).pre.empty());
    CHECK(as_running(*parse_version("0.1.0-testing")).pre.size() == 1);
    CHECK(user_agent("0.1.0-test+1a2b3c4d") == "b3port-launcher/0.1.0-test+1a2b3c4d");
}

TEST(json_reader) {
    std::string why;
    auto j = parse_json(R"( {"a": [1, -2.5e2, true, false, null, "x\"y\\z\/\n\t"], "b": {}, "c": [], "d": "\u00e9\ud83d\ude97"} )", &why);
    CHECK(j && j->kind == Json::Kind::kObject && j->members.size() == 4);
    if (j) {
        const Json* a = j->get("a");
        CHECK(a && a->kind == Json::Kind::kArray && a->items.size() == 6);
        if (a && a->items.size() == 6) {
            CHECK(a->items[0].n == 1 && a->items[1].n == -250 && a->items[2].b && !a->items[3].b);
            CHECK(a->items[4].kind == Json::Kind::kNull && a->items[5].s == "x\"y\\z/\n\t");
        }
        CHECK(j->get("b") && j->get("b")->kind == Json::Kind::kObject && j->get("b")->members.empty());
        CHECK(j->str("d") && *j->str("d") == "\xC3\xA9\xF0\x9F\x9A\x97");   // e acute, a car (a surrogate pair)
        CHECK(!j->str("a") && !j->get("missing"));
    }
    auto lone = parse_json(R"("\ud83dx")");
    CHECK(lone && lone->s == "\xEF\xBF\xBDx");   // a lone surrogate: U+FFFD
    CHECK(parse_json("\xEF\xBB\xBF{}"));         // a byte order mark
    for (const char* bad : {"", "{", "[1,]", "{\"a\" 1}", "{\"a\":1,}", "tru", "01", "1.", "-", "\"a", "\"\\x\"", "\"a\nb\"", "{} x", "[1 2]", "{1:2}"})
        CHECK(!parse_json(bad, &why));
    std::string deep(65, '[');
    deep += std::string(65, ']');
    CHECK(!parse_json(deep, &why) && why.find("deep") != std::string::npos);
    std::string ok_deep(60, '[');
    ok_deep += std::string(60, ']');
    CHECK(parse_json(ok_deep));
}

TEST(release_answer) {
    Release r;
    std::string why;
    CHECK(parse_release(kSample, r, &why));
    CHECK(r.tag == "v0.2.0");
    CHECK(r.name == "Takedown at 60 \xE2\x80\x94 caf\xC3\xA9");
    CHECK(r.url == "https://github.com/u0rhs/burnout3-native/releases/tag/v0.2.0");
    CHECK(r.published == "2026-12-01T18:00:00Z" && published_date(r.published) == "2026-12-01");
    CHECK(!r.prerelease);
    CHECK(r.notes.size() == 3);
    if (r.notes.size() == 3) {
        CHECK(r.notes[0] == "What's new");
        CHECK(r.notes[1] == "Split screen for two players");
        CHECK(r.notes[2] == "Fixed the menu font at 4K");
    }
    // a pre-release, no name, another site's page, control characters in the name
    CHECK(parse_release(R"({"tag_name":"v0.3.0-rc.1","prerelease":true,"html_url":"https://evil.example/x","name":""})", r));
    CHECK(r.tag == "v0.3.0-rc.1" && r.name == "v0.3.0-rc.1" && r.prerelease && r.url == kReleasesPage && r.notes.empty());
    CHECK(parse_release(R"({"tag_name":"v1.0.0","name":"a\nb\u0007c","html_url":"https://github.com/x\" onclick"})", r));
    CHECK(r.name == "a b c" && r.url == kReleasesPage);
    // not a release
    CHECK(!parse_release(R"({"message":"Not Found","documentation_url":"https://docs.github.com/rest"})", r, &why) && why.find("tag_name") != std::string::npos);
    CHECK(!parse_release("<html>rate limited</html>", r, &why) && why.find("JSON") != std::string::npos);
    CHECK(!parse_release("[]", r) && !parse_release(R"({"tag_name":5})", r));
}

TEST(release_notes_lines) {
    const auto l = first_lines("<!-- a comment\nover lines -->\n# Title\n\n---\n* item one\n> quoted `code`\n![shot](x.png) after\nmore", 4);
    CHECK(l.size() == 4);
    if (l.size() == 4) CHECK(l[0] == "Title" && l[1] == "item one" && l[2] == "quoted code" && l[3] == "shot after");
    CHECK(first_lines("", 3).empty() && first_lines("\n\n   \n", 3).empty());
    CHECK(first_lines(std::string(500, 'a'), 1)[0].size() == 200);
}

TEST(file_answers) {
    Response r = response_from_file_text(kSample);
    CHECK(r.ok && r.status == 200 && r.body == kSample);
    r = response_from_file_text("HTTP/1.1 404 Not Found\nContent-Type: application/json\n\n{\"message\":\"Not Found\"}");
    CHECK(r.ok && r.status == 404 && r.body == "{\"message\":\"Not Found\"}");
    r = response_from_file_text("HTTP/2 403\r\n\r\n{}");
    CHECK(r.ok && r.status == 403 && r.body == "{}");
    r = response_from_file_text("HTTP/1.1 abc\n\n");
    CHECK(!r.ok);
}

TEST(once_a_day_and_the_cache) {
    Cache c;
    const std::int64_t t0 = 1800000000;
    CHECK(due(c, t0));                    // never checked
    c.checked_at = t0;
    CHECK(!due(c, t0 + 3600) && !due(c, t0 + kInterval - 1));
    CHECK(due(c, t0 + kInterval) && due(c, t0 - 10));   // a day later; the clock went back
    c.enabled = false;
    CHECK(!due(c, t0 + 10 * kInterval));  // off: never
    c.enabled = true;

    const std::string running = "0.1.0-test+1a2b3c4d";
    Cache u;
    CHECK(state(u, running, false) == State::kNotChecked && state(u, running, true) == State::kChecking);
    apply(u, Response{true, 200, kSample, {}}, t0);
    CHECK(u.checked_at == t0 && u.error.empty() && !u.none && u.latest.tag == "v0.2.0");
    CHECK(state(u, running, false) == State::kAvailable && state(u, running, true) == State::kAvailable);
    CHECK(status_text(u, State::kAvailable) == "Update available: v0.2.0 \xE2\x80\x94 Takedown at 60 \xE2\x80\x94 caf\xC3\xA9");
    CHECK(state(u, "0.2.0+abc", false) == State::kUpToDate);
    CHECK(state(u, "0.3.0-test+abc", false) == State::kUpToDate);   // newer than the latest release: nothing to say
    CHECK(status_text(u, State::kUpToDate) == "Up to date (v0.2.0 is the latest release)");
    // a later failure keeps the release seen
    apply(u, Response{false, 0, {}, "timed out"}, t0 + kInterval);
    CHECK(u.error == "timed out" && u.latest.tag == "v0.2.0" && state(u, running, false) == State::kAvailable);
    CHECK(state(u, "0.2.0", false) == State::kError && status_text(u, State::kError) == "Couldn't check for updates (timed out)");
    // 404: no release yet; 403: refused; a 200 that is not a release
    apply(u, Response{true, 404, "{\"message\":\"Not Found\"}", {}}, t0);
    CHECK(u.none && u.latest.tag.empty() && u.error.empty() && state(u, running, false) == State::kNoRelease);
    CHECK(status_text(u, State::kNoRelease) == "Up to date (no release published yet)");
    apply(u, Response{true, 403, "{}", {}}, t0);
    CHECK(state(u, running, false) == State::kError && u.error.find("403") != std::string::npos);
    apply(u, Response{true, 200, "{\"oops\":1}", {}}, t0);
    CHECK(state(u, running, false) == State::kError && u.error.find("unexpected") != std::string::npos);
    u.enabled = false;
    CHECK(state(u, running, false) == State::kOff && status_text(u, State::kOff).find("off") != std::string::npos);
}

TEST(cache_in_launcher_ini) {
    Cache c;
    apply(c, Response{true, 200, kSample, {}}, 1800000000);
    c.error = "a \\ back\nslash";
    std::ostringstream o;
    write_ini(o, c);
    const std::string text = o.str();
    CHECK(text.find("update_check = 1\n") != std::string::npos && text.find("update_tag = v0.2.0\n") != std::string::npos);
    // read back line by line (as core.cpp's config_from_ini does: key = value, trimmed)
    Cache back;
    back.enabled = false;
    std::istringstream in(text);
    std::string line;
    int keys = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::size_t eq = line.find(" = ");
        CHECK(eq != std::string::npos);
        if (eq != std::string::npos && read_ini_key(back, line.substr(0, eq), line.substr(eq + 3))) ++keys;
    }
    CHECK(keys == 10);
    CHECK(back.latest == c.latest && back.enabled && back.checked_at == c.checked_at && back.none == c.none);
    CHECK(back.error == "a \\ back slash");   // one line (the line break cleaned)
    CHECK(!read_ini_key(back, "game_dir", "x"));
    read_ini_key(back, "update_url", "https://elsewhere.example/");
    CHECK(back.latest.url == kReleasesPage);
    read_ini_key(back, "update_check", "0");
    CHECK(!back.enabled);
}

TEST(release_assets_and_sums) {
    // 1.1 one-click updates: the release's files, this system's package, the hash list
    const char* json = R"({"tag_name": "v1.2.0", "html_url": "https://github.com/u0rhs/burnout3-native/releases/tag/v1.2.0",
        "assets": [
          {"name": "burnout3-native-1.2.0-abc-win64.zip", "size": 1000,
           "browser_download_url": "https://github.com/u0rhs/burnout3-native/releases/download/v1.2.0/burnout3-native-1.2.0-abc-win64.zip"},
          {"name": "burnout3-native-1.2.0-abc-linux-x86_64.tar.gz", "size": 900,
           "browser_download_url": "https://github.com/u0rhs/burnout3-native/releases/download/v1.2.0/burnout3-native-1.2.0-abc-linux-x86_64.tar.gz"},
          {"name": "SHA256SUMS", "size": 10, "browser_download_url": "https://github.com/u0rhs/burnout3-native/releases/download/v1.2.0/SHA256SUMS"},
          {"name": "evil-win64.zip", "browser_download_url": "https://example.com/evil-win64.zip"},
          {"name": "../x", "browser_download_url": "https://github.com/u0rhs/burnout3-native/releases/download/v1.2.0/x"}
        ]})";
    Release r;
    CHECK(parse_release(json, r));
    CHECK(r.assets.size() == 3);   // another site's download and a path-like name are dropped
    const Release::Asset* w = package_asset(r, Platform::kWindows);
    CHECK(w && w->name == "burnout3-native-1.2.0-abc-win64.zip" && w->size == 1000);
    const Release::Asset* l = package_asset(r, Platform::kLinux);
    CHECK(l && l->name == "burnout3-native-1.2.0-abc-linux-x86_64.tar.gz");
    CHECK(package_asset(r, Platform::kMac) == nullptr);
    CHECK(find_asset(r, "SHA256SUMS") && !find_asset(r, "SHA256SUMS.sig"));
    const std::string a(64, 'a'), b(64, 'B');
    const std::string sums = a + "  burnout3-native-1.2.0-abc-win64.zip\n" + b + " *burnout3-native-1.2.0-abc-linux-x86_64.tar.gz\r\n";
    CHECK(sum_for(sums, "burnout3-native-1.2.0-abc-win64.zip") == a);
    CHECK(sum_for(sums, "burnout3-native-1.2.0-abc-linux-x86_64.tar.gz") == std::string(64, 'b'));   // lower case
    CHECK(sum_for(sums, "other.zip").empty());
    CHECK(sum_for(sums + std::string(64, 'c') + "  burnout3-native-1.2.0-abc-win64.zip\n", "burnout3-native-1.2.0-abc-win64.zip").empty());   // two different
}

TEST(mac_package_follows_the_cpu) {
    // macintel1: with both Mac packages in a release, an Intel Mac's launcher takes the x86_64 one, every other build
    // the arm64 one
    const char* json = R"({"tag_name": "v1.2.0", "html_url": "https://github.com/u0rhs/burnout3-native/releases/tag/v1.2.0",
        "assets": [
          {"name": "burnout3-native-1.2.0-abc-macos-arm64.dmg", "size": 700,
           "browser_download_url": "https://github.com/u0rhs/burnout3-native/releases/download/v1.2.0/burnout3-native-1.2.0-abc-macos-arm64.dmg"},
          {"name": "burnout3-native-1.2.0-abc-macos-x86_64.dmg", "size": 800,
           "browser_download_url": "https://github.com/u0rhs/burnout3-native/releases/download/v1.2.0/burnout3-native-1.2.0-abc-macos-x86_64.dmg"}
        ]})";
    Release r;
    CHECK(parse_release(json, r));
    const Release::Asset* m = package_asset(r, Platform::kMac);
#if defined(__APPLE__) && defined(__x86_64__)
    CHECK(m && m->name == "burnout3-native-1.2.0-abc-macos-x86_64.dmg");
#else
    CHECK(m && m->name == "burnout3-native-1.2.0-abc-macos-arm64.dmg");
#endif
}

TEST_MAIN("test_launcher_update: the launcher's update check (src/b3launcher/update.h), canned answers only")
