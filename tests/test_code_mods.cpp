// The code-mod SDK (mod kit Phase 2b, docs/modding/DESIGN.md §2): the ABI's layout and version, the loader's refusals,
// replace / replace conflicts (the loser not loaded, its arena memory, rows and writes gone), the API on a synthetic hook
// table (hooks, settings, overlay, data files), and the settings' recording and replay logic (include/b3mod/codemod.h).
// The test mods are tests/codemods/testmod.c built per MODE from the SDK header alone (build/testmods). No game data.
#include "../sdk/include/b3mod_api.h"
#include "b3host/call.h"
#include "b3host/native_backend.h"
#include "b3mod/codemod.h"
#include "code_mods.h"
#include "hook_table.h"
#include "test.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace b3host;
using b3native::CodeMods;
using b3native::GuestArena;
using b3native::HookTable;
using b3native::TableEntry;
namespace fs = std::filesystem;

#ifndef B3_TESTMODS_DIR
#define B3_TESTMODS_DIR "build/testmods"
#endif
#if defined(_WIN32)
#define B3_HOST_SYSTEM_NAME "windows"
#elif defined(__APPLE__)
#define B3_HOST_SYSTEM_NAME "macos"
#else
#define B3_HOST_SYSTEM_NAME "linux"
#endif

namespace {

constexpr gaddr kA = 0x00100000u, kB = 0x00100010u, kPlat = 0x00100020u;
int gen_a(int x) { return x * 3; }
int gen_b(int x) { return x + 100; }
int plat(int x) { return x; }
std::atomic<bool> g_on{true};
std::atomic<std::uint64_t> g_nested{0};
const NativeOverride kRows[] = {
    {kA, &traced_entry<kA, &native_entry<&gen_a>>, 4, &g_on, &g_nested},
    {kB, &traced_entry<kB, &native_entry<&gen_b>>, 4, &g_on, &g_nested},
    {kPlat, &traced_entry<kPlat, &native_entry<&plat>>, 0, &g_on, &g_nested},
};
const TableEntry kEntries[] = {
    {kA, "leaf_a", "game:matched", 'i'},
    {kB, "leaf_b", "game:matched", 'i'},
    {kPlat, "sceThing", "platform", 'i'},
};
constexpr std::size_t kN = sizeof kRows / sizeof kRows[0];

struct Fixture {   // guest RAM (a NativeBackend), a private hook table, a fresh arena, a loader
    NativeBackend nb;
    BackendScope scope{nb};
    HookTable table;
    GuestArena arena;
    std::vector<std::string> log;
    std::uint64_t vb = 0;
    std::map<std::string, std::map<std::string, std::string>> saved;
    std::unique_ptr<CodeMods> mods;
    Fixture() {
        nb.set_stack_pointer(0x01F00000u);
        table.attach_rows(kRows, kEntries, kN, false);
        CodeMods::Host h;
        h.port_version = "test";
        h.vblank = [this] { return vb; };
        h.disc_size = [](const std::string& p) -> std::int64_t { return p == "DATA/TEST.BIN" ? 3 : -1; };
        h.disc_read = [](const std::string& p, std::uint64_t off, void* dst, std::uint64_t n) -> std::int64_t {
            if (p != "DATA/TEST.BIN" || off > 3) return -1;
            const std::uint64_t k = std::min<std::uint64_t>(n, 3 - off);
            std::memcpy(dst, "abc" + off, k);
            return std::int64_t(k);
        };
        h.saved_values = [this](const std::string& id) { return saved[id]; };
        h.log = [this](const std::string& l) { log.push_back(l); };
        mods = std::make_unique<CodeMods>(table, arena, h);
    }
    CodeMods::Candidate mod(const std::string& id, const std::string& lib, bool platform = false) {
        CodeMods::Candidate c;
        c.id = id;
        c.version = "1";
        c.dir = fs::path(B3_TESTMODS_DIR) / id;
        c.library = lib.empty() ? fs::path() : fs::absolute(fs::path(B3_TESTMODS_DIR) / (lib + ".so"));
        c.platform = platform;
        return c;
    }
    bool logged(const std::string& part) const {
        for (const std::string& l : log)
            if (l.find(part) != std::string::npos) return true;
        return false;
    }
};

int call(HookTable& t, gaddr a, std::uint32_t a0) {
    CallArgs c;
    c.gpr[0] = detail::gpr64(a0);
    c.n_gpr = c.n_fpr = 8;
    c.sp = 0x01F00000u;
    CallResult r;
    t.top(a)(c, r);
    return int(r.v0.w[0]);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------- the ABI
TEST(abi_struct_layout_is_frozen) {
    CHECK(B3MOD_API_VERSION == 1u && B3MOD_MANIFEST_API == 2 && b3mod::kApiVersion == 3 && b3mod::kCodeApi == 2 && b3mod::kScriptApi == 3);
    // ABI version 1 on a 64-bit host: two u32, then 23 function pointers. A change here is a new B3MOD_API_VERSION.
    CHECK(sizeof(void*) == 8);
    CHECK(offsetof(B3ModApi, size) == 0 && offsetof(B3ModApi, version) == 4);
    const std::size_t at[] = {offsetof(B3ModApi, port_version), offsetof(B3ModApi, vblank),       offsetof(B3ModApi, mod_id),
                              offsetof(B3ModApi, mod_dir),      offsetof(B3ModApi, log),          offsetof(B3ModApi, replace),
                              offsetof(B3ModApi, wrap),         offsetof(B3ModApi, next),         offsetof(B3ModApi, call),
                              offsetof(B3ModApi, has_function), offsetof(B3ModApi, read),         offsetof(B3ModApi, write),
                              offsetof(B3ModApi, alloc),        offsetof(B3ModApi, add_setting),  offsetof(B3ModApi, get_setting),
                              offsetof(B3ModApi, texture),      offsetof(B3ModApi, quad),         offsetof(B3ModApi, text),
                              offsetof(B3ModApi, on_vblank),    offsetof(B3ModApi, on_frame),     offsetof(B3ModApi, on_shutdown),
                              offsetof(B3ModApi, data_size),    offsetof(B3ModApi, read_data)};
    for (std::size_t k = 0; k < std::size(at); ++k) CHECK(at[k] == 8 + 8 * k);
    CHECK(sizeof(B3ModApi) == 8 + 8 * std::size(at));
    CHECK(sizeof(B3ModRegs) == 168 && offsetof(B3ModRegs, gpr_hi) == 64 && offsetof(B3ModRegs, fpr) == 128 && offsetof(B3ModRegs, sp) == 160);
    CHECK(sizeof(B3ModResult) == 32 && offsetof(B3ModResult, v1) == 16 && offsetof(B3ModResult, f0) == 24);
    CHECK(sizeof(B3ModSetting) == 48 && offsetof(B3ModSetting, key) == 8 && offsetof(B3ModSetting, values) == 32 &&
          offsetof(B3ModSetting, count) == 40 && offsetof(B3ModSetting, default_value) == 44);
    CHECK(B3MOD_RGBA(1, 2, 3, 4) == 0x04030201u);
    CHECK(B3MOD_SETTING_TOGGLE == 0 && B3MOD_SETTING_CHOICE == 1 && B3MOD_SETTING_SLIDER == 2);
}

// ---------------------------------------------------------------------------------------------------- the loader
TEST(a_good_mod_loads_and_its_api_works) {
    Fixture f;
    f.saved["good"] = {{"flag", "On"}, {"speed", "Nonsense"}};
    const auto r = f.mods->load({f.mod("good", "good")}, false);
    CHECK(r.size() == 1 && r[0].loaded && r[0].hooks == 2 && r[0].rows == 2);
    CHECK(f.mods->any() && f.mods->loaded_ids() == std::vector<std::string>{"good"});
    CHECK(f.logged("mod good: good mod loaded"));
    CHECK(f.logged("speed = \"Nonsense\" is not one of its values"));
    // replace (a0 * 10 + the flag, saved On) and wrap (a0 + 1 down, + 5 up)
    CHECK(call(f.table, kA, 3) == 31);
    CHECK(call(f.table, kB, 3) == 3 + 1 + 100 + 5);
    const auto chain = f.table.chain(kA);
    CHECK(chain.size() == 2 && chain[1].owner == "mod:good" && chain[1].layer == b3native::HookLayer::kReplace);
    // the arena (the first mod address) and the write made in b3mod_main, applied once it loaded
    CHECK(f.arena.allocations().size() == 1 && f.arena.allocations()[0].address == GuestArena::kPortEnd &&
          f.arena.allocations()[0].owner == "mod:good");
    char buf[8] = {};
    copy_from_guest(buf, GuestArena::kPortEnd, 6);
    CHECK(std::string(buf) == "hello");
    // settings: the saved value, and the default where the saved one was not a value
    const b3mod::LiveSettings& s = f.mods->settings();
    CHECK(s.rows().size() == 2 && s.value(s.find("good", "flag")) == 1 && s.value(s.find("good", "speed")) == 1);
    // events and the overlay
    f.mods->vblank();
    f.mods->vblank();
    f.mods->frame();
    std::vector<std::uint8_t> rec = f.mods->overlay_record();
    b3mod::OverlayList l;
    CHECK(!rec.empty() && b3mod::overlay_parse(rec.data(), rec.size(), l) && l.serial == 1 && l.items.size() == 2);
    CHECK(l.items.size() == 2 && l.items[0].kind == 1 && l.items[0].x == 10 && l.items[0].h == 40 && l.items[1].kind == 2 && l.items[1].text == "V2");
    f.mods->frame();   // the same items again: nothing new to publish
    CHECK(f.mods->overlay_record().empty());
    f.mods->vblank();
    f.mods->frame();
    rec = f.mods->overlay_record();
    CHECK(b3mod::overlay_parse(rec.data(), rec.size(), l) && l.serial == 2 && l.items[1].text == "V3");
    f.mods->shutdown();
}

TEST(replace_replace_conflict_leaves_the_second_mod_out_entirely) {
    {
        Fixture f;
        const auto r = f.mods->load({f.mod("good", "good"), f.mod("clash", "conflict")}, false);
        CHECK(r.size() == 2 && r[0].loaded && !r[1].loaded);
        CHECK(r[1].reason == "clash conflicts with good: both replace 0x00100000 leaf_a");
        CHECK(f.logged("native_play: mods: clash: not loaded: clash conflicts with good: both replace 0x00100000 leaf_a"));
        // nothing of it stays: its arena memory (given back), its row, its deferred write, its hooks
        CHECK(f.arena.allocations().size() == 1 && f.arena.next() == GuestArena::kPortEnd + 64);
        CHECK(f.mods->settings().rows().size() == 2 && f.mods->settings().find("clash", "flag") < 0);
        std::uint8_t b = 0xFF;
        copy_from_guest(&b, 0x000C9000u, 1);
        CHECK(b == 0);
        CHECK(f.table.chain(kA).size() == 2 && f.mods->loaded_ids() == std::vector<std::string>{"good"});
    }
    {   // the order decides: the first in the load order wins
        Fixture f;
        const auto r = f.mods->load({f.mod("clash", "conflict"), f.mod("good", "good")}, false);
        CHECK(r[0].loaded && !r[1].loaded && r[1].reason == "good conflicts with clash: both replace 0x00100000 leaf_a");
        CHECK(f.mods->loaded_ids() == std::vector<std::string>{"clash"});
    }
}

TEST(loader_refusals) {
    Fixture f;
    const auto r = f.mods->load({f.mod("nolib", ""), f.mod("missing", "does-not-exist"), f.mod("failmain", "failmain"),
                                 f.mod("badaddr", "badaddr"), f.mod("newer", "newer"), f.mod("nobuilt", "nobuilt"),
                                 f.mod("plat", "platform"), f.mod("plat2", "platform", true), f.mod("memory", "memory"),
                                 f.mod("badrow", "badrow")},
                                false);
    CHECK(r.size() == 10);
    for (const auto& x : r) CHECK(!x.loaded);
    CHECK(r[0].reason == std::string("[code] names no library for ") + b3mod::host_system());
    CHECK(r[1].reason.rfind("the library did not load", 0) == 0);
    CHECK(r[2].reason == "b3mod_main returned 5");
    CHECK(r[3].reason == "wrap 0x00100030: no native function there (not native, or inlined in the decomp)");
    CHECK(r[4].reason == "built for SDK API version 99; this port has version 1: a newer port is needed");
    CHECK(r[5].reason.find("no b3mod_built_for") != std::string::npos);
    CHECK(r[6].reason == "0x00100020 sceThing is the platform tier (needs [code] platform = yes and Developer mods)");
    CHECK(r[7].reason == r[6].reason);   // platform = yes alone is not enough
    CHECK(r[8].reason.rfind("alloc: mod:memory: arena: zero bytes", 0) == 0);   // (the bounds checks before it held)
    CHECK(r[9].reason.find("add_setting key \"Bad Key\"") == 0);
    CHECK(!f.mods->any() && f.arena.allocations().empty() && !f.table.hooked(kB) && !f.table.hooked(kPlat));
    // Developer mods and the mod's own platform = yes: allowed
    Fixture g;
    const auto p = g.mods->load({g.mod("plat2", "platform", true)}, true);
    CHECK(p[0].loaded && g.table.hooked(kPlat));
}

TEST(registration_after_b3mod_main_is_refused) {
    Fixture f;
    const auto r = f.mods->load({f.mod("late", "late")}, false);
    CHECK(r[0].loaded);
    f.mods->vblank();   // its callback tries to wrap: refused, logged once, the table unchanged
    f.mods->vblank();
    CHECK(f.logged("mod late: wrap after b3mod_main returned: refused"));
    CHECK(!f.table.hooked(kB));
}

TEST(data_files_through_the_mod_folder_and_the_disc) {
    const fs::path dir = fs::path(B3_TESTMODS_DIR) / "files";
    fs::create_directories(dir / "data");
    std::ofstream(dir / "data" / "hello.txt", std::ios::binary) << "hello";
    Fixture f;
    const auto r = f.mods->load({f.mod("files", "files")}, false);
    CHECK(r[0].loaded);
    if (!r[0].loaded) std::printf("  (%s)\n", r[0].reason.c_str());
}

// ---------------------------------------------------------------------------------------------------- settings
namespace {
b3mod::SettingRow row(const std::string& mod, const std::string& key, b3mod::SettingKind kind, unsigned count, unsigned def,
                      std::vector<std::string> values = {}) {
    b3mod::SettingRow r;
    r.mod = mod;
    r.key = key;
    r.label = "Label " + key;
    r.kind = kind;
    r.count = count;
    r.def = def;
    r.value = int(def);
    r.values = kind == b3mod::SettingKind::kToggle ? std::vector<std::string>{"Off", "On"} : std::move(values);
    return r;
}
}  // namespace

TEST(settings_rows_values_and_files) {
    const b3mod::SettingRow t = row("m", "flag", b3mod::SettingKind::kToggle, 2, 0);
    const b3mod::SettingRow c = row("m", "speed", b3mod::SettingKind::kChoice, 3, 1, {"Slow", "Fast", "Faster | ; \\ x"});
    const b3mod::SettingRow s = row("m", "level", b3mod::SettingKind::kSlider, 11, 5);
    CHECK(b3mod::check_row(t).empty() && b3mod::check_row(c).empty() && b3mod::check_row(s).empty());
    CHECK(!b3mod::check_row(row("m", "Bad", b3mod::SettingKind::kToggle, 2, 0)).empty());
    CHECK(!b3mod::check_row(row("m", "s", b3mod::SettingKind::kSlider, 1, 0)).empty());
    CHECK(!b3mod::check_row(row("m", "s", b3mod::SettingKind::kChoice, 2, 2, {"a", "b"})).empty());
    CHECK(b3mod::value_text(t, 1) == "On" && b3mod::value_from_text(t, "on") == 1 && b3mod::value_from_text(t, "1") == 1);
    CHECK(b3mod::value_from_text(t, "maybe") == -1 && b3mod::value_from_text(t, "2") == -1);
    CHECK(b3mod::value_text(s, 7) == "7" && b3mod::value_from_text(s, "10") == 10 && b3mod::value_from_text(s, "11") == -1);
    // the rows as text (the renderer's page, mods-state.ini), escapes included
    const std::vector<b3mod::SettingRow> rows = {t, c, s};
    const std::vector<b3mod::SettingRow> back = b3mod::rows_from_text(b3mod::rows_to_text(rows));
    CHECK(back.size() == 3 && back[1].values == c.values && back[1].def == 1 && back[2].count == 11 && back[0].label == "Label flag");
    // the values file
    const fs::path pref = fs::path(B3_TESTMODS_DIR) / "pref";
    fs::remove_all(pref);
    std::vector<b3mod::SettingRow> v = rows;
    v[0].value = 1;
    v[2].value = 3;
    CHECK(b3mod::save_values(b3mod::settings_file(pref, "m"), "m", v));
    const auto loaded = b3mod::load_values(b3mod::settings_file(pref, "m"));
    CHECK(loaded.size() == 3 && loaded.at("flag") == "On" && loaded.at("level") == "3" && loaded.at("speed") == "Fast");
    // the last start's report
    std::vector<b3mod::ModState> st(2);
    st[0].id = "m";
    st[0].loaded = true;
    st[0].digest = std::string(64, 'a');
    st[0].rows = v;
    st[1].id = "x";
    st[1].reason = "x conflicts with m: both replace 0x003FBC20 func_003FBC20";
    CHECK(b3mod::save_state(pref, st));
    const auto st2 = b3mod::load_state(pref);
    CHECK(st2.size() == 2 && st2[0].loaded && st2[0].rows.size() == 3 && st2[0].rows[0].value == 1 && !st2[1].loaded && st2[1].reason == st[1].reason);
    // trust
    const fs::path lib = pref / "lib.bin";
    std::ofstream(lib, std::ios::binary) << "abc";
    const std::string d = b3mod::library_digest(lib);
    CHECK(d == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");   // SHA-256("abc")
    CHECK(!b3mod::trusted(pref, "m", d) && b3mod::add_trust(pref, d, "m", "1.0") && b3mod::trusted(pref, "m", d));
    CHECK(!b3mod::trusted(pref, "m", std::string(64, '0')) && b3mod::add_trust(pref, d, "m", "1.0"));
    CHECK(!b3mod::trusted(pref, "other", d));   // the record is the mod id's: another mod with the same bytes is asked
}

TEST(trust_covers_every_library_in_the_mod) {
    const fs::path dir = fs::path(B3_TESTMODS_DIR) / "trust-set" / "m";
    fs::remove_all(dir.parent_path());
    fs::create_directories(dir / "bin" / "deps");
    auto put = [&](const std::string& rel, const std::string& text) { std::ofstream(dir / rel, std::ios::binary) << text; };
    put("bin/m.so", "ELF main");
    put("bin/deps/libz.SO", "ELF dep");
    put("bin/libq.so.1", "ELF versioned");
    put("bin/helper.dll", "MZ helper");
    put("bin/x.dylib", "macho");
    put("readme.txt", "words");
    b3mod::CodeSpec code;
    code.present = true;
    code.linux_path = "bin/m.so";
    code.windows_path = "bin/m.dll";   // (not there: only this system's named library is looked for)
    std::vector<b3mod::TrustFile> files;
    CHECK(b3mod::trust_files(dir, code, {}, files));
    std::vector<std::string> names;
    for (const auto& f : files) names.push_back(f.rel);
    CHECK((names == std::vector<std::string>{"bin/deps/libz.SO", "bin/helper.dll", "bin/libq.so.1", "bin/m.so", "bin/x.dylib"}));
    CHECK(b3mod::library_file_name("a.so.12") && !b3mod::library_file_name("a.so.x") && !b3mod::library_file_name("a.sox") && !b3mod::library_file_name(".so"));
    const std::string d0 = b3mod::trust_digest(dir, code, {});
    CHECK(d0.size() == 64 && d0 == b3mod::trust_digest(files));
    put("readme.txt", "other words");   // not code: the same set
    CHECK(b3mod::trust_digest(dir, code, {}) == d0);
    put("bin/deps/libz.SO", "ELF dop");   // a dependency only, the same size: asked again
    const std::string d1 = b3mod::trust_digest(dir, code, {});
    CHECK(d1.size() == 64 && d1 != d0);
    put("bin/new.so", "x");   // a library added: asked again
    CHECK(b3mod::trust_digest(dir, code, {}) != d1);
    fs::remove(dir / "bin" / "new.so");
    CHECK(b3mod::trust_digest(dir, code, {}) == d1);
    b3mod::CodeSpec none;
    CHECK(!b3mod::needs_trust(none, {}) && b3mod::trust_digest(dir, none, {}).empty());
    fs::remove_all(dir.parent_path());
}

TEST(the_loader_checks_the_allowed_set_again_before_loading) {
    Fixture f;
    CodeMods::Candidate c = f.mod("good", "good");
    c.dir = fs::absolute(fs::path(B3_TESTMODS_DIR));   // the folder of the test libraries: every one of them in the set
    c.code.present = true;
    c.code.linux_path = "good.so";
    c.trust_digest = std::string(64, '0');   // not what is there now (a file changed after the check)
    auto r = f.mods->load({c}, false);
    CHECK(r.size() == 1 && !r[0].loaded && r[0].reason.find("changed after they were checked") != std::string::npos);
    c.trust_digest = b3mod::trust_digest(c.dir, c.code, {});
    CHECK(c.trust_digest.size() == 64);
    r = f.mods->load({c}, false);
    CHECK(r.size() == 1 && r[0].loaded);
}

TEST(settings_change_at_vblank_boundaries_and_replay_the_same) {
    auto make = [] {
        b3mod::LiveSettings l;
        l.add(row("m", "flag", b3mod::SettingKind::kToggle, 2, 0));
        l.add(row("m", "level", b3mod::SettingKind::kSlider, 11, 5));
        l.add(row("n", "flag", b3mod::SettingKind::kToggle, 2, 1));
        return l;
    };
    // recording: start values, a scripted change, the page's requests between VBlanks
    b3mod::LiveSettings rec = make();
    std::vector<b3mod::SettingSet> script;
    CHECK(b3mod::parse_set_spec("m.level=7, 40:n.flag=Off,60:m.flag=On", script));
    CHECK(script.size() == 3 && script[0].vblank == 0 && script[1].vblank == 40 && script[1].mod == "n" && script[2].value == "On");
    std::vector<b3mod::SettingSet> bad;
    CHECK(!b3mod::parse_set_spec("x:m.flag=On", bad) && !b3mod::parse_set_spec("m=On", bad) && !b3mod::parse_set_spec("M.flag=On", bad));
    rec.schedule(script);
    std::vector<std::string> recording;   // the pad recording's lines
    auto record = [&](const std::vector<b3mod::SettingSet>& sets) { for (const auto& s : sets) recording.push_back(b3mod::set_line(s)); };
    record(rec.boundary(0));
    record(rec.snapshot(0));   // the whole state at the start
    std::vector<std::array<int, 3>> seen;   // the values each VBlank's callbacks would see
    for (std::uint64_t v = 1; v <= 100; ++v) {
        if (v == 10) { CHECK(rec.request("m", "flag", "On")); }   // between VBlanks 9 and 10: from VBlank 10
        if (v == 25) { CHECK(rec.request("m", "flag", "Off")); CHECK(rec.request("m", "level", "2")); }
        if (v == 26) { CHECK(!rec.request("m", "nope", "On")); CHECK(!rec.request("m", "flag", "maybe")); }
        if (v == 70) CHECK(rec.request("m", "flag", "On"));   // the value it already has: not recorded
        record(rec.boundary(v));
        seen.push_back({rec.value(0), rec.value(1), rec.value(2)});
    }
    CHECK(recording.size() == 1 + 3 + 5);   // level 7 at 0, the snapshot (3 rows), then flag On@10, flag Off + level 2 @25, n.flag Off @40, flag On @60
    CHECK(recording[0] == "set 0 m.level 7" && recording[4] == "set 10 m.flag On" && recording[5] == "set 25 m.flag Off" &&
          recording[6] == "set 25 m.level 2" && recording.back() == "set 60 m.flag On");
    CHECK(seen[8][0] == 0 && seen[9][0] == 1 && seen[24][1] == 2 && seen[39][2] == 0 && seen[38][2] == 1);
    // playback: the recording's lines back, the window's requests ignored; the same values on the same VBlanks
    std::vector<b3mod::SettingSet> events;
    for (const std::string& l : recording) {
        b3mod::SettingSet s;
        CHECK(b3mod::set_from_line(l, s));
        events.push_back(s);
    }
    b3mod::LiveSettings play = make();
    play.play(events);
    CHECK(play.playing() && !play.request("m", "flag", "On"));
    play.schedule({{50, "m", "flag", "On"}});   // a script during playback: ignored
    play.boundary(0);
    for (std::uint64_t v = 1; v <= 100; ++v) {
        play.boundary(v);
        CHECK(play.value(0) == seen[v - 1][0] && play.value(1) == seen[v - 1][1] && play.value(2) == seen[v - 1][2]);
    }
    b3mod::SettingSet x;
    CHECK(!b3mod::set_from_line("set x m.flag On", x) && !b3mod::set_from_line("rumble 3 1", x));
}

// ---------------------------------------------------------------------------------------------------- manifest, order, hash, record
TEST(code_section_of_the_manifest) {
    auto parse = [](const std::string& t) { return b3mod::parse_manifest(t); };
    const auto ok = parse("[mod]\nid = c\nversion = 1\napi = 2\n[code]\nwindows = bin/c.dll\nlinux = bin/c.so\ngameplay = yes\nplatform = no\n");
    CHECK(ok.errors.empty() && ok.warnings.empty() && ok.m.code.present && ok.m.code.linux_path == "bin/c.so" && ok.m.code.gameplay &&
          !ok.m.code.platform && ok.m.code.path_for("windows") == "bin/c.dll" && ok.m.code.path_for("macos").empty());
    const auto api1 = parse("[mod]\nid = c\nversion = 1\napi = 1\n[code]\nlinux = c.so\n");
    CHECK(api1.errors.size() == 1 && api1.errors[0].find("[code] needs api = 2") == 0);
    CHECK(!parse("[mod]\nid = c\nversion = 1\napi = 2\n[code]\ngameplay = no\n").errors.empty());
    CHECK(!parse("[mod]\nid = c\nversion = 1\napi = 2\n[code]\nlinux = ../c.so\n").errors.empty());
    CHECK(!parse("[mod]\nid = c\nversion = 1\napi = 2\n[code]\nlinux = c.so\ngameplay = perhaps\n").errors.empty());
    CHECK(parse("[mod]\nid = d\nversion = 1\napi = 2\n").errors.empty());   // a data mod may say api 2
    CHECK(parse("[mod]\nid = d\nversion = 1\napi = 3\n").errors.empty());   // lua6: api 3 (script mods)
    CHECK(!parse("[mod]\nid = d\nversion = 1\napi = 4\n").errors.empty());
    CHECK(std::string(b3mod::host_system()) == B3_HOST_SYSTEM_NAME);
}

TEST(mods_ini_developer_option) {   // tune4 merge: Developer mods is pc_settings.ini's; mods.ini's [options] is ignored
    b3mod::Order o;
    o.entries = {{"a", true}, {"developer_mods", false}};
    CHECK(b3mod::order_from_ini(b3mod::order_to_ini(o)) == o);
    CHECK(b3mod::order_to_ini(o).find("[options]") == std::string::npos);
    const b3mod::Order back = b3mod::order_from_ini(b3mod::order_to_ini(o) + "[options]\ndeveloper_mods = on\n");
    CHECK(back == o && back.entries.size() == 2);
}

TEST(mod_hash_covers_every_library) {
    const fs::path root = fs::path(B3_TESTMODS_DIR) / "hashmods";
    fs::remove_all(root);
    fs::create_directories(root / "c" / "bin");
    // macintel1: a macos line too (the same file: nothing is loaded here), so the mod has a library on a Mac as well
    std::ofstream(root / "c" / "mod.ini") << "[mod]\nid = c\nversion = 1\napi = 2\n[code]\nlinux = bin/c.so\nwindows = bin/c.dll\nmacos = bin/c.so\n";
    std::ofstream(root / "c" / "bin" / "c.so", std::ios::binary) << "ELF one";
    auto build = [&] {
        const auto found = b3mod::discover(root);
        const auto sel = b3mod::select(found, {"c"});
        return b3mod::Overlay::build(sel.mods);
    };
    const b3mod::Overlay a = build();
    CHECK(a.mods().size() == 1 && a.mods()[0].code.present && !a.mods()[0].library.empty());
    CHECK(a.mods()[0].code_lines.find("code windows bin/c.dll missing\n") != std::string::npos &&
          a.mods()[0].code_lines.find("code linux bin/c.so 7 ") != std::string::npos);
    std::ofstream(root / "c" / "bin" / "c.so", std::ios::binary) << "ELF two";   // the same size, other bytes
    const b3mod::Overlay b = build();
    CHECK(a.set_hash() != b.set_hash() && a.mods()[0].hash != b.mods()[0].hash);
    CHECK(b3mod::set_hash_text(b.mods()).rfind("b3mods api 1\n", 0) == 0);   // the hash text keeps its version
    CHECK(b3mod::describe(b).find("(0 files, code)") != std::string::npos);
}

TEST(overlay_record_round_trip_and_bounds) {
    std::vector<b3mod::OverlayItem> items(2);
    items[0].kind = 1;
    items[0].x = 1.5f;
    items[0].texture = 7;
    items[1].kind = 2;
    items[1].text = "LAP 1/3";
    items[1].h = 12;
    const auto rec = b3mod::overlay_record(9, items);
    b3mod::OverlayList l;
    CHECK(b3mod::overlay_parse(rec.data(), rec.size(), l) && l.serial == 9 && l.items == items);
    CHECK(!b3mod::overlay_parse(rec.data(), rec.size() - 4, l));
    std::vector<b3mod::OverlayItem> many(4000, items[1]);   // past kOverlayMax: cut
    const auto big = b3mod::overlay_record(1, many);
    CHECK(big.size() <= b3mod::kOverlayMax && b3mod::overlay_parse(big.data(), big.size(), l) && l.items.size() < many.size() && !l.items.empty());
}

TEST_MAIN("test_code_mods: the code-mod SDK (mod kit 2b): ABI, loader, conflicts, settings recording and replay")
