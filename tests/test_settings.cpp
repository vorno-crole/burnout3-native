// settings1: the PC settings model, ini, HSET record and the page's menu model (include/b3settings).
#include "test.h"
#include <sstream>
#include "b3port/user_dir.h"

#include "b3settings/i18n.h"
#include "b3settings/page.h"
#include "b3settings/safemode.h"
#include "b3settings/settings.h"
#include "input_bindings.h"   // 1.2.1: the CONTROLS page's shortcut rows (kPort)

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <map>

using namespace b3settings;

// settingsui: every tab's rows in one list (rows found by label wherever they are; their order: the tabs' order)
Tab all_rows(const Content& c) {
    Tab t{"ALL", {}};
    for (const Tab& x : c.tabs) t.rows.insert(t.rows.end(), x.rows.begin(), x.rows.end());
    return t;
}
// the index of the row (not a section) labelled `label` in `t`, or t.rows.size()
std::size_t row_at(const Tab& t, const std::string& label) {
    for (std::size_t i = 0; i < t.rows.size(); ++i)
        if (t.rows[i].kind != RowKind::kSection && t.rows[i].label == label) return i;
    return t.rows.size();
}

// settingsui: the tab holding a (non-section) row with this label, or none
const Tab* holding(const Content& c, const std::string& label) {
    for (const Tab& t : c.tabs)
        for (const Row& r : t.rows)
            if (r.kind != RowKind::kSection && r.label == label) return &t;
    return nullptr;
}

TEST(defaults_are_main_and_the_high_preset) {
    const Settings d{};
    // main before settings1: MSAA 4, anisotropy 16, interpolation, wide HUD, every effect, radial 0.5, PC post, game LODs
    CHECK(d.msaa == 4 && d.filter == Filter::kAniso16 && d.interpolation && d.hud_wide && !d.vsync && d.frame_cap == 0);
    CHECK(d.motion_blur && d.bloom && d.post_pc && d.radial_blur == 50 && !d.car_detail_highest && d.damage && d.glass_damage);
    CHECK(d.fov == 0 && d.fps_view == 0 && d.fps_corner == 0 && !d.fullscreen);
    CHECK(d.fe_layout == 2 && d.movie_fill && d.latency == 0 && d.lead == -1 && d.frames_in_flight == -1);   // fe3 / lat1 defaults, auto
    CHECK(d.whole_track && d.pause_unfocused);   // mac5: the whole track drawn; the native game holds unfocused
    CHECK(d.skip_hint);   // mac5: the video skip hint shows
    CHECK(matching_preset(d) == Preset::kHigh);
    CHECK(with_preset(d, Preset::kHigh) == d);
    CHECK(!with_preset(d, Preset::kPs2).whole_track && with_preset(d, Preset::kLow).whole_track);   // PS2 streaming: PS2 only
}

TEST(car_reflections_default_enhanced_and_original_in_the_ps2_and_low_presets) {
    const Settings d{};
    CHECK(d.car_reflections == 2);   // reflect1: the one remaster feature on by default
    CHECK(with_preset(d, Preset::kPs2).car_reflections == 1 && with_preset(d, Preset::kLow).car_reflections == 1);
    CHECK(with_preset(d, Preset::kMedium).car_reflections == 2 && with_preset(d, Preset::kUltra).car_reflections == 2);
    CHECK(matching_preset(with_preset(d, Preset::kPs2)) == Preset::kPs2);
    Settings s = d;
    s.car_reflections = 0;
    CHECK(from_ini(to_ini(s)).car_reflections == 0);
    CHECK(from_ini("car_reflections = 1\n").car_reflections == 1);
    CHECK(from_ini("car_reflections = 7\n").car_reflections == 2);   // out of range: the default
    CHECK(matching_preset(s) == Preset::kCustom);
}

TEST(presets_round_trip_and_leave_the_other_rows) {
    Settings s;
    s.fov = 100;
    s.fps_view = 3; s.fps_corner = 3;
    s.frame_cap = 144;
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        const Settings q = with_preset(s, p);
        CHECK(matching_preset(q) == p);
        CHECK(q.fov == 100 && q.fps_view == 3 && q.fps_corner == 3 && q.frame_cap == 144);   // presets touch quality and effects only
    }
    Settings c = with_preset(s, Preset::kUltra);
    c.sparks = kLevelOff;
    CHECK(matching_preset(c) == Preset::kCustom);
    CHECK(with_preset(s, Preset::kPs2).msaa == 1 && with_preset(s, Preset::kPs2).filter == Filter::kBilinear);
}

TEST(ini_round_trip_and_bad_values) {
    Settings s = with_preset(Settings{}, Preset::kMedium);
    s.fov = 105;
    s.radial_blur = 30;
    s.vsync = true;
    s.filter = Filter::kPoint;
    CHECK(from_ini(to_ini(s)) == s);
    std::vector<std::string> w;
    const Settings b = from_ini("msaa = 3\nfov = 200\nbloom = maybe\nnope = 1\nradial_blur = 70\n[x]\n# c\n", &w);
    CHECK(b.msaa == 4 && b.fov == 0 && b.bloom && b.radial_blur == 70);
    CHECK(w.size() == 4);
    CHECK(from_ini("") == Settings{});
}

TEST(hset_record) {
    Settings s;
    s.bloom = false;
    const auto r = hset_encode(s, 7);
    CHECK(r.size() % 16 == 0 && r.size() <= kHsetMax);
    std::uint32_t n = 0;
    const auto serial = hset_serial(r.data(), &n);
    CHECK(serial && *serial == 7);
    CHECK(from_ini(std::string_view(reinterpret_cast<const char*>(r.data() + 16), n)) == s);
    std::uint8_t zero[16] = {};
    CHECK(!hset_serial(zero));
}

TEST(fov_scale) {
    CHECK(fov_tangent_scale(0) == 1.0f && fov_tangent_scale(90) == 1.0f);
    CHECK_NEAR(fov_tangent_scale(120), 1.7320508, 1e-5);
    CHECK(fov_tangent_scale(60) < 1.0f);
}

TEST(every_row_applies) {
    const Content c = build_content(Settings{}, Env{true});
    CHECK(c.tabs.size() == 8);   // settingsui: DISPLAY GRAPHICS EFFECTS CAR CONTROLS AUDIO GAME MODS (SAVES with a card)
    int rows = 0;
    for (const Tab& t : c.tabs)
        for (const Row& r : t.rows) {
            if (r.kind == RowKind::kSection || r.disabled) continue;
            if (r.kind == RowKind::kAction) continue;   // mac5: the host's buttons (HD pack folder), not settings values
            if (r.id == kRowLanguage) continue;   // 1.0.1: the host's (language_edit: its values are the imported languages)
            ++rows;
            Settings s;
            const std::size_t n = r.kind == RowKind::kToggle ? 2 : r.kind == RowKind::kSlider ? r.levels : r.values.size();
            CHECK(n >= 2);
            CHECK(r.value < n);
            CHECK(apply_edit(s, Edit{r.id, r.value, 0, false}));
            CHECK(r.id == 1 || s == Settings{});   // the current value changes nothing (the preset row re-applies High)
        }
    CHECK(rows >= 25);
    // no HD row without a pack
    const Content n = build_content(Settings{}, Env{false});
    int hd = 0;
    for (const Tab& t : n.tabs) for (const Row& r : t.rows) hd += r.label == "HD TEXTURES";
    CHECK(hd == 0);
}

TEST(controls_section) {
    // remap1: the CONTROLS section (INTERFACE, last) only for a host that remaps; its rows are the host's
    const Content plain = build_content(Settings{}, Env{true});
    Env e{true};
    e.controls.on = true;
    e.controls.actions = {{"CROSS", "ENTER, Z", "Driving: accelerate."}, {"CIRCLE", "X, H", ""}};
    const Content c = build_content(Settings{}, e);
    CHECK(c.tabs.size() == plain.tabs.size());
    const Tab& t = c.tabs[4];
    CHECK(t.label == "CONTROLS");   // settingsui: BINDINGS after FEEDBACK and DRIVING
    const std::size_t before = plain.tabs[4].rows.size();
    for (std::size_t k = 0; k < before; ++k) CHECK(t.rows[k].id == plain.tabs[4].rows[k].id && t.rows[k].label == plain.tabs[4].rows[k].label);
    // the section, PLAYER, DEVICE, KEYS FOR (the keyboard), the two actions, RESET
    CHECK(t.rows.size() == before + 8);   // (rebind2: and RESET ALL CONTROLS after RESET)
    CHECK(t.rows[before].kind == RowKind::kSection && t.rows[before].label == "BINDINGS");
    CHECK(t.rows[before + 1].id == kRowCtlPlayer && t.rows[before + 2].id == kRowCtlDevice && t.rows[before + 3].id == kRowCtlView);
    CHECK(t.rows[before + 4].id == kActionCtlFirst && t.rows[before + 4].values[0] == "ENTER, Z" && !t.rows[before + 4].disabled);
    CHECK(t.rows[before + 6].id == kActionCtlReset);
    for (std::size_t k = before; k < t.rows.size(); ++k) {
        Settings s;
        CHECK(!apply_edit(s, Edit{t.rows[k].id, t.rows[k].value, 0, t.rows[k].kind == RowKind::kAction}));
    }
    // a controller: CONTROLLER LAYOUT and BUTTONS FOR (padlayout) in KEYS FOR's place; a status row once there is one
    e.controls.device = 1;
    e.controls.status = "PLAYER 1 CROSS: B";
    const Content g = build_content(Settings{}, e);
    CHECK(g.tabs[4].rows.size() == before + 10);
    CHECK(g.tabs[4].rows[before + 3].id == kRowCtlLayout && g.tabs[4].rows[before + 3].values.size() == 5);   // padpaint: Nintendo
    CHECK(g.tabs[4].rows[before + 3].values[3] == "Nintendo" && g.tabs[4].rows[before + 3].values[4] == "Custom");
    CHECK(g.tabs[4].rows[before + 4].id == kRowCtlView && g.tabs[4].rows[before + 4].label == "BUTTONS FOR");
    CHECK(g.tabs[4].rows.back().id == kRowCtlStatus);
    for (std::size_t k = before; k < g.tabs[4].rows.size(); ++k) {   // the host's rows, all of them
        Settings s;
        CHECK(!apply_edit(s, Edit{g.tabs[4].rows[k].id, g.tabs[4].rows[k].value, 0, g.tabs[4].rows[k].kind == RowKind::kAction}));
    }
}

TEST(controls_shortcuts_and_reset) {
    // rebind2: PLAYER's third value is the shortcuts (no KEYS FOR row, no gyro rows even with a controller), RESET ALL
    // CONTROLS follows RESET; SQUARE (Input::reset) on a control row asks the host for that one back (direction 2),
    // and only there
    Env e{true};
    e.controls.on = true;
    e.controls.player = 2;
    e.controls.actions = {{"INSTANT RESTART", "F5", ""}, {"FULLSCREEN", "F11", ""}};
    const Content c = build_content(Settings{}, e);
    const Tab* t = nullptr;
    for (const Tab& x : c.tabs) if (x.label == "CONTROLS") t = &x;
    CHECK(t != nullptr);
    if (!t) return;
    bool view = false, reset = false, all = false, player3 = false;
    std::size_t first = 0;
    for (std::size_t i = 0; i < t->rows.size(); ++i) {
        const Row& r = t->rows[i];
        if (r.id == kRowCtlView) view = true;
        if (r.id == kActionCtlReset) reset = true;
        if (r.id == kActionCtlResetAll) all = r.kind == RowKind::kAction && !r.disabled;
        if (r.id == kRowCtlPlayer) player3 = r.values.size() == 3 && r.values[2] == "Shortcuts" && r.value == 2;
        if (r.id == kActionCtlFirst) first = i;
    }
    CHECK(!view && reset && all && player3 && first > 0);
    // SQUARE on the first control row
    Menu m;
    m.open(std::size_t(t - c.tabs.data()));
    m.update(c, Input{});
    for (int guard = 0; guard < 600 && m.focus() != first; ++guard) {   // (the hold repeats walk there)
        Input d;
        d.down = true;
        m.update(c, d);
    }
    Input sq;
    sq.reset = true;
    Frame f = m.update(c, sq);
    CHECK(m.focus() == first);
    bool got = false;
    for (const Edit& ed : f.edits) got = got || (ed.id == kActionCtlFirst && ed.direction == 2 && ed.action);
    CHECK(got);
    // held: no second edit
    f = m.update(c, sq);
    CHECK(f.edits.empty());
}

TEST(saves_tab) {
    // save1 (remaster-features §8): the SAVES tab only with a card; its rows are the host's (apply_edit leaves them)
    for (const auto& t : build_content(Settings{}, Env{true}).tabs) CHECK(t.label != "SAVES");
    Env e;
    e.profile = 2;
    e.next_profile = 3;
    e.profile_used = {true, true, false, false, false};
    e.saves_folder = "/saves";
    const Content c = build_content(Settings{}, e);
    CHECK(c.tabs.size() == 9 && c.tabs[7].label == "SAVES" && c.tabs.back().label == "MODS");   // settingsui: SAVES, then MODS
    const Tab& t = c.tabs[7];
    bool file = false, ex = false, im = false, folder = false;
    for (const Row& r : t.rows) {
        Settings s;
        CHECK(!apply_edit(s, Edit{r.id, r.value, 0, r.kind == RowKind::kAction}));   // (SAVE SCREENS: GAME, settingsui)
        if (r.id == kRowSaveFile) file = r.kind == RowKind::kChoice && r.values.size() == 5 && r.value == 2 && r.values[2] == "File 3 (new)" && r.values[1] == "File 2";
        if (r.id == kActionExportSave) ex = r.kind == RowKind::kAction && !r.disabled;
        if (r.id == kActionImportSave) im = r.kind == RowKind::kAction && !r.disabled;
        if (r.id == kActionOpenSaveFolder) folder = r.description == "/saves";
    }
    CHECK(file && ex && im && folder);
    // the last result shows as a row once there is one
    e.save_status = "IMPORT FAILED: no Burnout 3 save";
    bool status = false;
    const Content with_status = build_content(Settings{}, e);
    for (const Row& r : with_status.tabs[7].rows) status = status || (r.label == "LAST RESULT" && r.values[0] == "Failed");
    CHECK(status);
}

TEST(save_screens_row) {
    // autosave1 (tools/native/save_screens.h): SAVE SCREENS is Hide by default, in the file, a live choice on the SAVES tab, and
    // no preset or safe mode touches it
    CHECK(Settings{}.hide_save_screens);
    Settings s;
    s.hide_save_screens = false;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("hide_save_screens = false") != std::string::npos);
    CHECK(!from_ini("hide_save_screens = off\n").hide_save_screens && from_ini("").hide_save_screens);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(!with_preset(s, p).hide_save_screens);
    CHECK(!safe_settings(s).hide_save_screens && safe_settings(Settings{}).hide_save_screens);
    Env e;
    e.profile = 1;
    e.profile_used = {true, false, false, false, false};
    bool found = false;
    for (const auto& t : build_content(Settings{}, e).tabs)
        for (const auto& r : t.rows)
            if (r.label == "SAVE SCREENS") {
                found = t.label == "GAME" && r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "Hide" && r.value == 0;
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && !x.hide_save_screens);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.hide_save_screens);
            }
    CHECK(found);
}

TEST(dither_row) {
    // perf1 (docs/native-renderer.md §33.8): DITHER is off by default, in the file, on the GRAPHICS tab, and no preset sets it
    CHECK(!Settings{}.dither);
    Settings s;
    s.dither = true;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("dither = true") != std::string::npos || to_ini(s).find("dither = 1") != std::string::npos);
    CHECK(with_preset(s, Preset::kPs2).dither && with_preset(s, Preset::kUltra).dither);
    CHECK(matching_preset(s) == Preset::kHigh);
    bool found = false;
    for (const auto& t : build_content(Settings{}, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "DITHER") {
                found = t.label == "GRAPHICS";
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.dither);
            }
    CHECK(found);
}

TEST(bloom_level_row) {
    // bloom2: BLOOM is Off / Soft / Standard (row 260), Soft by default; the PS2 preset is Standard (the game's own glow)
    const Settings d{};
    CHECK(d.bloom && d.bloom_level == 1);
    CHECK(with_preset(d, Preset::kPs2).bloom_level == 2 && with_preset(d, Preset::kHigh).bloom_level == 1);
    CHECK(matching_preset(with_preset(d, Preset::kPs2)) == Preset::kPs2);
    CHECK(from_ini("bloom_level = 2\n").bloom_level == 2 && from_ini("bloom_level = 9\n").bloom_level == 1);
    Settings s = d;
    s.bloom_level = 2;
    CHECK(from_ini(to_ini(s)) == s);
    bool found = false;
    for (const auto& t : build_content(d, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "BLOOM") {
                found = t.label == "EFFECTS" && r.id == 260 && r.value == 1 && r.values.size() == 3;
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 2, 0, false}) && x.bloom && x.bloom_level == 2);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && !x.bloom && x.bloom_level == 2);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.bloom && x.bloom_level == 1);
                Settings off = d;
                off.bloom = false;
                for (const auto& t2 : build_content(off, Env{true}).tabs)
                    for (const auto& r2 : t2.rows)
                        if (r2.id == 260) CHECK(r2.value == 0);
            }
    CHECK(found);
}

TEST(skip_hint_row) {
    // mac5 (remaster-features §2): VIDEO SKIP HINT is on by default, in the file, a live toggle on the INTERFACE tab, and
    // no preset touches it
    CHECK(Settings{}.skip_hint);
    Settings s;
    s.skip_hint = false;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("skip_hint = false") != std::string::npos);
    CHECK(!from_ini("skip_hint = off\n").skip_hint && from_ini("").skip_hint);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(!with_preset(s, p).skip_hint);
    bool found = false;
    for (const auto& t : build_content(Settings{}, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "VIDEO SKIP HINT") {
                found = t.label == "GAME" && r.kind == RowKind::kToggle && !r.disabled && r.value == 1;
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && !x.skip_hint);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.skip_hint);
            }
    CHECK(found);
}

TEST(fast_loading_row) {
    // fastload1 (tools/native/fast_load.h): FAST LOADING is on by default, in the file, a live toggle on the DISPLAY tab
    // (WINDOW section, beside PAUSE WHEN UNFOCUSED), and no preset or safe mode touches it
    CHECK(Settings{}.fast_loading);
    Settings s;
    s.fast_loading = false;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("fast_loading = false") != std::string::npos);
    CHECK(!from_ini("fast_loading = off\n").fast_loading && from_ini("").fast_loading);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(!with_preset(s, p).fast_loading);
    CHECK(!safe_settings(s).fast_loading && safe_settings(Settings{}).fast_loading);
    bool found = false;
    for (const auto& t : build_content(Settings{}, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "FAST LOADING") {
                found = t.label == "GAME" && r.kind == RowKind::kToggle && !r.disabled && r.value == 1;
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && !x.fast_loading);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.fast_loading);
            }
    CHECK(found);
}

TEST(button_prompts_row) {
    // mac5 (remaster-features §3): BUTTON PROMPTS is Auto by default, in the file (0-5), a live choice on GAME > INTERFACE
    // and no preset touches it
    CHECK(Settings{}.button_prompts == 0);
    Settings s;
    s.button_prompts = 2;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("button_prompts = 2") != std::string::npos);
    CHECK(from_ini("button_prompts = 9\n").button_prompts == 0 && from_ini("button_prompts = 5\n").button_prompts == 5);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).button_prompts == 2);
    bool found = false;
    for (const auto& t : build_content(Settings{}, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "BUTTON PROMPTS") {
                found = t.label == "GAME" && r.kind == RowKind::kChoice && !r.disabled && r.value == 0 && r.values.size() == 7 &&
                        r.values[1] == "PlayStation" && r.values[2] == "PS4 / PS5" && r.values[3] == "Xbox" && r.values[6] == "Keyboard";
                // the page lists PS4 / PS5 beside PlayStation; the ini keeps its values (PS4 / PS5 = 6, Xbox = 2)
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 3, 0, false}) && x.button_prompts == 2);
                CHECK(apply_edit(x, Edit{r.id, 2, 0, false}) && x.button_prompts == 6);
            }
    CHECK(found);
    Settings m;
    m.button_prompts = 6;
    CHECK(from_ini(to_ini(m)).button_prompts == 6);
    for (const auto& t : build_content(m, Env{true}).tabs)
        for (const auto& r : t.rows)
            if (r.label == "BUTTON PROMPTS") CHECK(r.value == 2);
}

TEST(menu_edits_tabs_and_close) {
    Settings s;
    Menu m;
    m.open(1);   // settingsui: GRAPHICS, QUALITY PRESET its first row
    Input idle;
    const Content c0 = build_content(s, Env{});
    CHECK(m.update(c0, idle).edits.empty());
    // the opening press is ignored until released: confirm held at open does nothing
    Input held;
    held.confirm = true;
    CHECK(m.update(c0, held).edits.empty());
    // right on the preset row (focused first): High -> Ultra
    Input right;
    right.right = true;
    Frame f = m.update(c0, right);
    CHECK(f.edits.size() == 1);
    for (const Edit& e : f.edits) apply_edit(s, e);
    CHECK(matching_preset(s) == Preset::kUltra);
    // held: the next step comes after the repeat delay, in the caller's time
    CHECK(m.update(build_content(s, Env{}), right).edits.empty());
    Input r2 = right;
    r2.seconds = 0.4f;
    CHECK(m.update(build_content(s, Env{}), r2).edits.size() == 1);   // Ultra -> Custom
    m.update(build_content(s, Env{}), idle);
    // next tab: EFFECTS, focus on its first selectable row
    Input next;
    next.next_tab = true;
    m.update(build_content(s, Env{}), next);
    CHECK(m.tab() == 2);
    m.update(build_content(s, Env{}), idle);
    Input back;
    back.back = true;
    CHECK(m.update(build_content(s, Env{}), back).close);
}

// online-det follow-up: an offscreen shot sets the animation clock from the shot's poll, so the page it draws must
// not depend on how the host's frames split the time before it (the focused row's pulse)
TEST(menu_set_clock_makes_the_picture_independent_of_frame_timing) {
    const Content c = build_content(Settings{}, Env{});
    Menu a, b;
    a.open(1);
    b.open(1);
    Input in;
    in.seconds = 1.0f;
    a.update(c, in);
    in.seconds = 0.37f;
    b.update(c, in);
    in.seconds = 0.91f;
    b.update(c, in);
    auto same = [](const std::vector<Item>& x, const std::vector<Item>& y) {
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (x[i].kind != y[i].kind || x[i].flags != y[i].flags || x[i].x != y[i].x || x[i].y != y[i].y ||
                x[i].w != y[i].w || x[i].h != y[i].h || x[i].colour != y[i].colour || x[i].text != y[i].text)
                return false;
        return true;
    };
    CHECK(!same(a.items("f", "a", nullptr), b.items("f", "a", nullptr)));   // the pulse differs at 1.00 s and 1.28 s
    a.set_clock(10400.0f / 60.0f);
    b.set_clock(10400.0f / 60.0f);
    CHECK(same(a.items("f", "a", nullptr), b.items("f", "a", nullptr)));
}

TEST(menu_items_inside_the_picture) {
    Menu m;
    m.open(2);
    Input in;
    in.seconds = 1.0f;
    m.update(build_content(Settings{}, Env{true}), in);
    const auto items = m.items("fonts", "art", nullptr);
    CHECK(items.size() > 50);
    CHECK(items.front().kind == ItemKind::kFontDirectory);
    bool title = false;
    for (const Item& i : items) {
        if (i.kind == ItemKind::kFontDirectory) continue;
        CHECK(i.x >= -1 && i.y >= -1 && i.x + i.w <= 641 && i.y + i.h <= 449);
        if (i.kind == ItemKind::kText && i.text == "SETTINGS") title = true;
        if (i.kind == ItemKind::kImage) CHECK(i.text.rfind("art/", 0) == 0);
    }
    CHECK(title);
}

// ---- the menu model's rules (docs/specs/settings-page.md §2), positions found through items()

namespace {

Row mrow(std::uint32_t id, RowKind kind, std::string label, std::size_t n = 0, std::size_t value = 0, bool disabled = false) {
    Row r;
    r.id = id;
    r.kind = kind;
    r.label = std::move(label);
    if (kind == RowKind::kToggle) r.values = {"Off", "On"};
    if (kind == RowKind::kChoice)
        for (std::size_t k = 0; k < n; ++k) r.values.push_back("V" + std::to_string(k));
    if (kind == RowKind::kSlider) {
        r.levels = n;
        r.level_text = std::to_string(value);
    }
    if (kind == RowKind::kAction) r.values = {"Go"};
    r.value = value;
    r.disabled = disabled;
    r.description = r.label + " description";
    return r;
}
// Tab A: a section, a toggle (off), a choice (3 values, at 1), a slider (5 levels, at 2), an action, a disabled choice;
// tab B: 24 rows, a section before every 6th; tab C: one toggle.
Content mcontent() {
    Content c;
    Tab a{"TAB-A", {}};
    a.rows.push_back(mrow(900, RowKind::kSection, "SECTION A"));
    a.rows.push_back(mrow(1, RowKind::kToggle, "TOGGLE"));
    a.rows.push_back(mrow(2, RowKind::kChoice, "CHOICE", 3, 1));
    a.rows.push_back(mrow(3, RowKind::kSlider, "SLIDER", 5, 2));
    a.rows.push_back(mrow(4, RowKind::kAction, "ACTION"));
    a.rows.push_back(mrow(5, RowKind::kChoice, "DISABLED", 3, 1, true));
    c.tabs.push_back(a);
    Tab b{"TAB-B", {}};
    for (std::uint32_t k = 0; k < 24; ++k)
        b.rows.push_back(k % 6 == 0 ? mrow(1000 + k, RowKind::kSection, "HEAD " + std::to_string(k))
                                    : mrow(100 + k, RowKind::kToggle, "ROW " + std::to_string(k)));
    c.tabs.push_back(b);
    Tab t{"TAB-C", {}};
    t.rows.push_back(mrow(50, RowKind::kToggle, "ONLY"));
    c.tabs.push_back(t);
    return c;
}
const Row& focused(const Menu& m, const Content& c) { return c.tabs[m.tab()].rows[m.focus()]; }
Input pressed(bool Input::*b, float seconds = 1.0f / 60.0f) {
    Input in;
    in.*b = true;
    in.seconds = seconds;
    return in;
}
// a fresh menu on mcontent(), past the opening press, focus on `id` of tab `tab`
Menu mopen(const Content& c, std::size_t tab = 0) {
    Menu m;
    m.open(tab);
    m.update(c, Input{});
    return m;
}
const Item* text_item(const std::vector<Item>& items, const std::string& text) {
    for (const Item& i : items)
        if (i.kind == ItemKind::kText && i.text == text) return &i;
    return nullptr;
}
std::vector<std::string> shown_rows(const Menu& m, const Content& c) {
    std::vector<std::string> o;
    const auto items = m.items("", "", nullptr);
    for (const Row& r : c.tabs[m.tab()].rows)
        if (text_item(items, r.label)) o.push_back(r.label);
    return o;
}
// The arrow images on the row whose label is `label` (left, right)
std::pair<const Item*, const Item*> arrows(const std::vector<Item>& items, const std::string& label) {
    const Item* l = text_item(items, label);
    const Item* a = nullptr;
    const Item* b = nullptr;
    for (const Item& i : items)
        if (l && i.kind == ItemKind::kImage && i.y < l->y + l->h && i.y + i.h > l->y) {
            if (i.text.find("left") != std::string::npos) a = &i;
            if (i.text.find("right") != std::string::npos) b = &i;
        }
    return {a, b};
}
Input click_at(float x, float y) {
    Input in;
    in.pointer = std::array<float, 2>{x, y};
    in.pointer_moved = true;
    in.click = in.click_held = true;
    return in;
}

}  // namespace

TEST(menu_pointer) {
    // a click on the centre of the third tab's label (EFFECTS) selects it
    Menu m;
    m.open();
    const Content c = build_content(Settings{}, Env{});
    m.update(c, Input{});
    const auto items = m.items("", "", nullptr);
    const Item* t = text_item(items, "EFFECTS");
    CHECK(t != nullptr);
    if (!t) return;
    m.update(c, click_at(t->x + t->w * 0.5f, t->y + t->h * 0.5f));
    CHECK(m.tab() == 2);
}

TEST(menu_opening_press_and_edges) {
    const Content c = mcontent();
    Menu m;
    m.open();
    Input held = pressed(&Input::confirm);
    held.back = true;
    CHECK(m.update(c, held).edits.empty());   // the press that opened the page: not an edge
    CHECK(!m.update(c, held).close);
    CHECK(focused(m, c).id == 1);              // the first selectable row (the section skipped)
    CHECK(m.update(c, Input{}).edits.empty()); // released
    Frame f = m.update(c, pressed(&Input::confirm));
    CHECK(f.edits.size() == 1 && f.edits[0].id == 1 && f.edits[0].value == 1 && !f.edits[0].action);   // the toggle flips
    CHECK(m.update(c, pressed(&Input::confirm)).edits.empty());   // held: an edge only
    // a direction held as the page opens acts only after its release
    Menu d;
    d.open();
    CHECK(d.update(c, pressed(&Input::down)).edits.empty() && focused(d, c).id == 1);
    d.update(c, Input{});
    d.update(c, pressed(&Input::down));
    CHECK(focused(d, c).id == 2);
    // back closes, and nothing else happens in that poll
    Input bd = pressed(&Input::back);
    bd.down = true;
    d.update(c, Input{});
    Frame b = d.update(c, bd);
    CHECK(b.close && b.edits.empty() && focused(d, c).id == 2);
}

TEST(menu_hold_repeat) {
    const Content c = mcontent();
    Menu m = mopen(c);
    m.update(c, pressed(&Input::down));
    m.update(c, Input{});
    m.update(c, pressed(&Input::down));
    m.update(c, Input{});
    CHECK(focused(m, c).id == 3);   // the slider at 2 of 0..4
    // right: at once, then nothing before 0.35 s held, then one step every 0.07 s (the content stays at 2: the caller
    // applies no edit here, so each step is 2 -> 3)
    CHECK(m.update(c, pressed(&Input::right)).edits.size() == 1);
    CHECK(m.update(c, pressed(&Input::right, 0.30f)).edits.empty());
    CHECK(m.update(c, pressed(&Input::right, 0.04f)).edits.empty());   // 0.34 s held
    Frame f = m.update(c, pressed(&Input::right, 0.02f));             // 0.36
    CHECK(f.edits.size() == 1 && f.edits[0].direction == 1 && f.edits[0].value == 3);
    CHECK(m.update(c, pressed(&Input::right, 0.04f)).edits.empty());   // 0.40 (the next step at 0.42)
    CHECK(m.update(c, pressed(&Input::right, 0.03f)).edits.size() == 1);   // 0.43
    CHECK(m.update(c, pressed(&Input::right, 0.03f)).edits.empty());       // 0.46 (next at 0.49)
    CHECK(m.update(c, pressed(&Input::right, 0.04f)).edits.size() == 1);   // 0.50
    // a long poll is still one step at most
    Menu s = mopen(c);
    for (int k = 0; k < 2; ++k) { s.update(c, pressed(&Input::down)); s.update(c, Input{}); }
    CHECK(s.update(c, pressed(&Input::left)).edits.size() == 1);         // 2 -> 1
    Frame one = s.update(c, pressed(&Input::left, 1.0f));               // a whole second in one poll: one step
    CHECK(one.edits.size() == 1 && one.edits[0].value == 1);
    CHECK(s.update(c, pressed(&Input::left, 1.0f)).edits.size() == 1);
    Content zero = c;                                                    // at 0: nothing at the end (no wrap)
    zero.tabs[0].rows[3].value = 0;
    CHECK(s.update(zero, pressed(&Input::left, 1.0f)).edits.empty());
    // left and right together: neither
    Menu lr = mopen(c);
    Input both = pressed(&Input::left);
    both.right = true;
    for (int k = 0; k < 2; ++k) { lr.update(c, pressed(&Input::down)); lr.update(c, Input{}); }
    CHECK(lr.update(c, both).edits.empty());
    CHECK(lr.update(c, both).edits.empty());
}

// devnav1: PageUp / PageDown move by sections (a long tab: DEVELOPER); up / down held go faster after 1.5 s and 3 s
TEST(menu_sections_and_long_holds) {
    const Content c = mcontent();
    Menu m = mopen(c, 1);   // TAB-B: headings at 0, 6, 12, 18
    CHECK(focused(m, c).id == 101);
    auto tap = [&](bool Input::*b) {
        m.update(c, pressed(b));
        m.update(c, Input{});
        return focused(m, c).id;
    };
    CHECK(tap(&Input::next_section) == 107);
    CHECK(tap(&Input::next_section) == 113);
    CHECK(tap(&Input::down) == 114);
    CHECK(tap(&Input::previous_section) == 113);   // the section's first row
    CHECK(tap(&Input::previous_section) == 107);   // then the one before
    CHECK(tap(&Input::next_section) == 113);
    CHECK(tap(&Input::next_section) == 119);
    CHECK(tap(&Input::next_section) == 123);        // no section after: the last row
    CHECK(tap(&Input::next_section) == 123);
    CHECK(tap(&Input::previous_section) == 119);
    for (int k = 0; k < 4; ++k) tap(&Input::previous_section);
    CHECK(focused(m, c).id == 101);                 // the first row at most
    // a tab without sections: a window of rows
    Content flat;
    Tab t{"FLAT", {}};
    for (std::uint32_t k = 0; k < 40; ++k) t.rows.push_back(mrow(200 + k, RowKind::kToggle, "R" + std::to_string(k)));
    flat.tabs.push_back(t);
    Menu f = mopen(flat);
    f.update(flat, pressed(&Input::next_section));
    CHECK(focused(f, flat).id == 200 + 10);   // the window: 10 rows (menu.cpp kVisible)
    // down held 4 s at 60 polls a second: 1 + (1.5 - 0.35) / 0.07 steps by 1.5 s, then every 0.035 s, then every poll
    Content tall;
    Tab l{"TALL", {}};
    for (std::uint32_t k = 0; k < 1000; ++k) l.rows.push_back(mrow(k + 1, RowKind::kToggle, "R"));
    tall.tabs.push_back(l);
    Menu h = mopen(tall);
    int steps_at_1_5 = 0, steps_at_3 = 0;
    for (int p = 0; p < 240; ++p) {
        h.update(tall, pressed(&Input::down));
        const int at = int(h.focus());
        if (p == 89) steps_at_1_5 = at;
        if (p == 179) steps_at_3 = at;
    }
    const int end = int(h.focus());
    CHECK(steps_at_1_5 >= 16 && steps_at_1_5 <= 18);           // the normal rate first
    CHECK(steps_at_3 - steps_at_1_5 >= 40 && steps_at_3 - steps_at_1_5 <= 46);   // x2
    CHECK(end - steps_at_3 >= 59 && end - steps_at_3 <= 60);   // every poll
    // left / right (values) never speed up
    Menu v = mopen(c);
    for (int k = 0; k < 2; ++k) { v.update(c, pressed(&Input::down)); v.update(c, Input{}); }
    int edits = 0;
    for (int p = 0; p < 240; ++p) edits += int(v.update(c, pressed(&Input::right)).edits.size());
    CHECK(edits >= 52 && edits <= 54);   // 1 + (4 - 0.35) / 0.07
}

TEST(menu_popup) {
    Content c = mcontent();
    Content p = c;
    p.popup = "WORKING...";
    Menu m = mopen(c);
    CHECK(m.update(p, pressed(&Input::confirm)).edits.empty());   // nothing acts while the popup shows
    CHECK(!m.update(p, pressed(&Input::back)).close);
    // the popup is drawn over the page
    CHECK(text_item(m.items("", "", nullptr), "WORKING...") != nullptr);
    // a press held across the popup does not act when it closes
    Input r = pressed(&Input::down);
    m.update(p, r);
    CHECK(m.update(c, r).edits.empty() && focused(m, c).id == 1);
    Input cb = pressed(&Input::confirm);
    m.update(p, cb);
    CHECK(m.update(c, cb).edits.empty());
    m.update(c, Input{});
    CHECK(m.update(c, pressed(&Input::confirm)).edits.size() == 1);   // a new press does
}

TEST(menu_focus_tabs_and_ends) {
    const Content c = mcontent();
    Menu m = mopen(c);
    m.update(c, pressed(&Input::up));   // already the first selectable row: no wrap
    m.update(c, Input{});
    CHECK(focused(m, c).id == 1);
    for (int k = 0; k < 8; ++k) { m.update(c, pressed(&Input::down)); m.update(c, Input{}); }
    CHECK(focused(m, c).id == 5);       // the disabled row is selectable, and the last: no wrap
    // disabled: nothing changes it
    CHECK(m.update(c, pressed(&Input::right)).edits.empty());
    m.update(c, Input{});
    CHECK(m.update(c, pressed(&Input::confirm)).edits.empty());
    m.update(c, Input{});
    // tabs: no wrap at either end; a tab move does nothing else in that poll
    m.update(c, pressed(&Input::previous_tab));
    m.update(c, Input{});
    CHECK(m.tab() == 0);
    Input nt = pressed(&Input::next_tab);
    nt.confirm = true;
    CHECK(m.update(c, nt).edits.empty() && m.tab() == 1);
    CHECK(focused(m, c).id == 101);     // the first selectable row of a tab not visited
    m.update(c, Input{});
    m.update(c, pressed(&Input::down));
    m.update(c, Input{});
    CHECK(focused(m, c).id == 102);
    m.update(c, pressed(&Input::next_tab));
    m.update(c, Input{});
    m.update(c, pressed(&Input::next_tab));   // the last tab: stays
    m.update(c, Input{});
    CHECK(m.tab() == 2);
    // each tab remembers its row while the page is open
    m.update(c, pressed(&Input::previous_tab));
    m.update(c, Input{});
    CHECK(m.tab() == 1 && focused(m, c).id == 102);
    m.update(c, pressed(&Input::previous_tab));
    m.update(c, Input{});
    CHECK(m.tab() == 0 && focused(m, c).id == 5);
    // open forgets
    m.open(1);
    m.update(c, Input{});
    CHECK(m.tab() == 1 && focused(m, c).id == 101);
    // a tab index past the content resets to tab 0
    Menu z;
    z.open(9);
    z.update(c, Input{});
    CHECK(z.tab() == 0);
}

TEST(menu_actions_and_values) {
    const Content c = mcontent();
    Menu m = mopen(c);
    for (int k = 0; k < 3; ++k) { m.update(c, pressed(&Input::down)); m.update(c, Input{}); }
    CHECK(focused(m, c).id == 4);
    Frame f = m.update(c, pressed(&Input::left));
    CHECK(f.edits.size() == 1 && f.edits[0].action && f.edits[0].direction == -1 && f.edits[0].id == 4);
    m.update(c, Input{});
    f = m.update(c, pressed(&Input::confirm));
    CHECK(f.edits.size() == 1 && f.edits[0].action && f.edits[0].direction == 0);
    // the toggle: left at Off does nothing, right turns it on, right again (On) nothing
    Menu t = mopen(c);
    CHECK(t.update(c, pressed(&Input::left)).edits.empty());
    t.update(c, Input{});
    f = t.update(c, pressed(&Input::right));
    CHECK(f.edits.size() == 1 && f.edits[0].value == 1);
    Content on = c;
    on.tabs[0].rows[1].value = 1;
    t.update(on, Input{});
    CHECK(t.update(on, pressed(&Input::right)).edits.empty());
    // the choice: steps within 0..2, no wrap
    Menu ch = mopen(c);
    ch.update(c, pressed(&Input::down));
    ch.update(c, Input{});
    f = ch.update(c, pressed(&Input::right));
    CHECK(f.edits.size() == 1 && f.edits[0].id == 2 && f.edits[0].value == 2);
    Content at2 = c;
    at2.tabs[0].rows[2].value = 2;
    ch.update(at2, Input{});
    CHECK(ch.update(at2, pressed(&Input::right)).edits.empty());
}

TEST(menu_focus_kept_by_row_id) {
    Content c = mcontent();
    Menu m = mopen(c);
    for (int k = 0; k < 2; ++k) { m.update(c, pressed(&Input::down)); m.update(c, Input{}); }
    CHECK(focused(m, c).id == 3);
    Content more = c;   // a row appears above it: the focus stays on the slider
    more.tabs[0].rows.insert(more.tabs[0].rows.begin() + 1, mrow(7, RowKind::kToggle, "NEW"));
    m.update(more, Input{});
    CHECK(m.focus() == 4 && focused(m, more).id == 3);
    Content less = c;   // the focused row disappears: the first selectable row
    less.tabs[0].rows.erase(less.tabs[0].rows.begin() + 3);
    m.update(less, Input{});
    CHECK(focused(m, less).id == 1);
}

TEST(menu_scroll_window) {
    const Content c = mcontent();
    Menu m = mopen(c, 1);
    std::vector<std::string> s = shown_rows(m, c);
    CHECK(!s.empty() && s.front() == "HEAD 0" && s.size() < 24);
    const std::size_t window = s.size();
    const auto has = [](const std::vector<Item>& items, const char* name) {
        for (const Item& i : items)
            if (i.kind == ItemKind::kImage && i.text.find(name) != std::string::npos) return true;
        return false;
    };
    CHECK(has(m.items("", "", nullptr), "arrow-down") && !has(m.items("", "", nullptr), "arrow-up"));
    // down to the last row: the focus always in the window, which ends at the last row
    for (int k = 0; k < 30; ++k) {
        m.update(c, pressed(&Input::down));
        m.update(c, Input{});
        const std::vector<std::string> v = shown_rows(m, c);
        CHECK(std::find(v.begin(), v.end(), focused(m, c).label) != v.end());
    }
    CHECK(focused(m, c).id == 123);
    s = shown_rows(m, c);
    CHECK(s.size() == window && s.back() == "ROW 23");
    CHECK(has(m.items("", "", nullptr), "arrow-up") && !has(m.items("", "", nullptr), "arrow-down"));
    // up to the top: entering a row just below its section header shows the header too
    for (int k = 0; k < 30; ++k) {
        m.update(c, pressed(&Input::up));
        m.update(c, Input{});
        const Row& r = focused(m, c);
        const std::vector<std::string> v = shown_rows(m, c);
        CHECK(std::find(v.begin(), v.end(), r.label) != v.end());
        if (r.id == 113) CHECK(v.front() == "HEAD 12");
    }
    CHECK(focused(m, c).id == 101 && shown_rows(m, c).front() == "HEAD 0");
    // the wheel: down one step scrolls one row, clamped at the end; the focus follows into the window
    Input w;
    w.pointer = std::array<float, 2>{320.0f, 200.0f};
    w.wheel = -1;
    m.update(c, w);
    CHECK(shown_rows(m, c).front() == "ROW 1");
    w.wheel = -100;
    m.update(c, w);
    s = shown_rows(m, c);
    CHECK(s.back() == "ROW 23" && std::find(s.begin(), s.end(), focused(m, c).label) != s.end());
    w.wheel = 100;
    m.update(c, w);
    CHECK(shown_rows(m, c).front() == "HEAD 0");
    // a click on the down arrow scrolls one row
    const auto items = m.items("", "", nullptr);
    for (const Item& i : items)
        if (i.kind == ItemKind::kImage && i.text.find("arrow-down") != std::string::npos) m.update(c, click_at(i.x + i.w * 0.5f, i.y + i.h * 0.5f));
    CHECK(shown_rows(m, c).front() == "ROW 1");
}

TEST(menu_pointer_rows_and_controls) {
    const Content c = mcontent();
    Menu m = mopen(c);
    auto items = m.items("", "", nullptr);
    // hovering a row focuses it; a section row ignores the pointer
    const Item* choice = text_item(items, "CHOICE");
    const Item* section = text_item(items, "SECTION A");
    CHECK(choice && section);
    if (!choice || !section) return;
    Input hover;
    hover.pointer = std::array<float, 2>{choice->x + 4, choice->y + choice->h * 0.5f};
    hover.pointer_moved = true;
    CHECK(m.update(c, hover).edits.empty() && focused(m, c).id == 2);
    hover.pointer = std::array<float, 2>{section->x + 4, section->y + section->h * 0.5f};
    m.update(c, hover);
    CHECK(focused(m, c).id == 2);
    // the choice's arrows step it
    auto [l, r] = arrows(items, "CHOICE");
    CHECK(l && r);
    if (!l || !r) return;
    Frame f = m.update(c, click_at(r->x + r->w * 0.5f, r->y + r->h * 0.5f));
    CHECK(f.edits.size() == 1 && f.edits[0].id == 2 && f.edits[0].value == 2);
    m.update(c, Input{});
    f = m.update(c, click_at(l->x + l->w * 0.5f, l->y + l->h * 0.5f));
    CHECK(f.edits.size() == 1 && f.edits[0].value == 0);
    // the toggle's control flips it (a click on another row focuses that row first)
    const Item* toggle = text_item(items, "TOGGLE");
    CHECK(toggle != nullptr);
    m.update(c, Input{});
    f = m.update(c, click_at(r->x - 4, toggle->y + toggle->h * 0.5f));
    CHECK(focused(m, c).id == 1 && f.edits.size() == 1 && f.edits[0].id == 1 && f.edits[0].value == 1);
    // the disabled row: a click focuses it and changes nothing
    const Item* dis = text_item(items, "DISABLED");
    auto [dl, dr] = arrows(items, "DISABLED");
    CHECK(dis && dr);
    if (dr) {
        m.update(c, Input{});
        f = m.update(c, click_at(dr->x + dr->w * 0.5f, dr->y + dr->h * 0.5f));
        CHECK(f.edits.empty() && focused(m, c).id == 5);
    }
    // the action's button activates it
    const Item* act = text_item(items, "Go");
    CHECK(act != nullptr);
    if (act) {
        m.update(c, Input{});
        f = m.update(c, click_at(act->x + act->w * 0.5f, act->y + act->h * 0.5f));
        CHECK(f.edits.size() == 1 && f.edits[0].id == 4 && f.edits[0].action);
    }
}

TEST(menu_slider_drag) {
    const Content c = mcontent();
    Menu m = mopen(c);
    const auto items = m.items("", "", nullptr);
    auto [l, r] = arrows(items, "SLIDER");
    CHECK(l && r);
    if (!l || !r) return;
    // the bar lies between the arrows: a click at its left end sets level 0 and starts a drag
    const float y = l->y + l->h * 0.5f;
    const float x0 = l->x + l->w + 12, x1 = r->x - 12;
    Frame f = m.update(c, click_at(x0, y));
    CHECK(f.edits.size() == 1 && f.edits[0].id == 3 && f.edits[0].value == 0);
    // held and moved: the nearest level under the pointer, even off the row
    Input drag;
    drag.pointer = std::array<float, 2>{x1 + 40, y + 60};
    drag.pointer_moved = true;
    drag.click_held = true;
    f = m.update(c, drag);
    CHECK(f.edits.size() == 1 && f.edits[0].value == 4 && focused(m, c).id == 3);
    drag.pointer = std::array<float, 2>{x0 + 0.25f * (x1 - x0), y + 60};   // (the content stays at 2)
    f = m.update(c, drag);
    CHECK(f.edits.size() == 1 && f.edits[0].value == 1);
    // released: the drag ends (moving no longer sets it; it hovers instead)
    Input up;
    up.pointer = std::array<float, 2>{x1, y};
    up.pointer_moved = true;
    CHECK(m.update(c, up).edits.empty());
    drag.pointer = std::array<float, 2>{x0, y};
    CHECK(m.update(c, drag).edits.empty());
    // no pointer this poll ends a drag too
    m.update(c, click_at(x1, y));
    Input none;
    none.click_held = true;
    m.update(c, none);
    drag.pointer = std::array<float, 2>{x0, y};
    CHECK(m.update(c, drag).edits.empty());
}

TEST(menu_animation_is_a_function_of_the_polls) {
    // the same polls give the same items, and once settled every item stays in the picture
    const Content c = build_content(Settings{}, Env{true});
    Menu a, b;
    a.open(1);
    b.open(1);
    for (int k = 0; k < 20; ++k) {
        Input in;
        in.seconds = 0.013f * float(k % 3 + 1);
        in.next_tab = k == 10;
        a.update(c, in);
        b.update(c, in);
        const auto ia = a.items("", "", nullptr), ib = b.items("", "", nullptr);
        CHECK(ia.size() == ib.size());
        for (std::size_t i = 0; i < std::min(ia.size(), ib.size()); ++i)
            CHECK(ia[i].x == ib[i].x && ia[i].y == ib[i].y && ia[i].colour == ib[i].colour && ia[i].text == ib[i].text);
        for (const Item& i : ia)
            if (i.kind != ItemKind::kFontDirectory) CHECK(i.x >= -1 && i.y >= -1 && i.x + i.w <= 641 && i.y + i.h <= 449);
    }
}

TEST(safe_mode_guard_policy) {
    // hostfix: safe mode asked for turns the guest RAM guards off; the automatic one keeps them on unless the previous
    // run's log shows a guest memory overrun (its crash line, or its own guards-off line for that reason)
    StartDecision asked = start_run(clean_end(), true), autom, none = start_run(clean_end(), false);
    autom.safe = true;
    CHECK(safe_mode_guards_off(asked, false) == "safe mode" && safe_mode_guards_off(asked, true) == "safe mode");
    CHECK(safe_mode_guards_off(autom, false).empty());
    CHECK(safe_mode_guards_off(autom, true) == "safe mode after a guest memory overrun");
    CHECK(safe_mode_guards_off(none, true).empty());
    const std::string a = "build/test_guard_log_a.log", b = "build/test_guard_log_b.log", c = "build/test_guard_log_c.log";
    std::ofstream(a) << "native_play: guest RAM guards: 4096 MB each, scratchpad 64 KB\ncrash: SIGSEGV at 0x0\n";
    std::ofstream(b) << "native_play: started\ncrash: guest memory overrun: host address 0x7f0000000000 is 0x10 bytes past the end of guest RAM\n";
    std::ofstream(c) << "native_play: guest RAM guards: off (safe mode after a guest memory overrun)\ncrash: SIGSEGV at 0x0\n";
    CHECK(!log_shows_guard_overrun(a) && log_shows_guard_overrun(b) && log_shows_guard_overrun(c));
    CHECK(!log_shows_guard_overrun("build/no-such-run.log"));
    std::remove(a.c_str()); std::remove(b.c_str()); std::remove(c.c_str());
}

TEST(safe_mode_after_two_unclean_runs) {
    // mac5 (remaster-features §8): a run that never reached its clean end counts; two in a row start in safe mode
    RunState st;                                   // first start ever
    StartDecision d = start_run(st, false);
    CHECK(d.now.running && d.now.unclean == 0 && !d.safe);
    d = start_run(d.now, false);                   // the run before crashed
    CHECK(d.now.unclean == 1 && !d.safe);
    d = start_run(d.now, false);                   // and the next one too
    CHECK(d.now.unclean == 2 && d.safe && !d.forced);
    d = start_run(clean_end(), false);             // a clean end clears the count
    CHECK(d.now.unclean == 0 && !d.safe);
    d = start_run(clean_end(), true);              // --safe-mode
    CHECK(d.safe && d.forced && d.now.unclean == 0);
    // the file round trip; junk keeps the defaults
    RunState r; r.running = true; r.unclean = 3;
    CHECK(run_state_from_ini(run_state_to_ini(r)) == r);
    CHECK(run_state_from_ini("running = 7\nunclean = x\n") == RunState{});
    const std::string path = "build/test_run_state.ini";
    CHECK(save_run_state(path, r) && load_run_state(path) == r);
    CHECK(save_run_state(path, clean_end()) && load_run_state(path) == RunState{});
    CHECK(load_run_state("build/no-such-run-state.ini") == RunState{});
}

TEST(safe_settings_lower_display_and_quality_only) {
    Settings u{};
    u.fullscreen = true; u.msaa = 8; u.hd_textures = true; u.fov = 100; u.button_prompts = 2; u.fps_view = 2; u.frame_cap = 240;
    const Settings s = safe_settings(u);
    CHECK(s.msaa == 1 && !s.hd_textures && !s.fullscreen && s.vsync && s.frame_cap == 0 && s.filter == Filter::kBilinear);
    CHECK(!s.motion_blur && !s.specular);                                 // the Low preset's effects
    CHECK(s.fov == 100 && s.button_prompts == 2 && s.fps_view == 2);         // camera and interface kept
    CHECK(s.latency == 0 && s.lead == -1 && s.frames_in_flight == -1);    // latency back to auto
}

TEST(hd_pack_folder_in_the_ini) {
    // mac5: the HD pack folder chosen on the page / in the launcher: a path with spaces survives the ini
    Settings s{};
    s.hd_pack_dir = "C:/Users/Some One/PCSX2/textures/SLUS-21050";
    const Settings b = from_ini(to_ini(s));
    CHECK(b.hd_pack_dir == s.hd_pack_dir && b == s);
    CHECK(from_ini(to_ini(Settings{})).hd_pack_dir.empty());
    // the QUALITY tab: choose, and remove only when one is chosen
    auto has = [](const Content& c, std::uint32_t id) {
        for (const Tab& t : c.tabs) for (const Row& r : t.rows) if (r.id == id) return true;
        return false;
    };
    CHECK(has(build_content(Settings{}, Env{}), kActionChooseHdPack) && !has(build_content(Settings{}, Env{}), kActionClearHdPack));
    CHECK(has(build_content(s, Env{}), kActionClearHdPack));
}

TEST(xbox_textures_row_and_ini) {
    // xboximport (1.3.0): XBOX TEXTURES, off by default, in the ini; the row (id 383) only with an import there, and
    // HD TEXTURES with it (the import is switched by it too) even with no folder chosen
    CHECK(!Settings{}.xbox_textures);
    Settings s{};
    s.xbox_textures = true;
    const Settings b = from_ini(to_ini(s));
    CHECK(b.xbox_textures && b == s);
    auto row = [](const Content& c, const char* label) -> const Row* {
        for (const Tab& t : c.tabs) for (const Row& r : t.rows) if (r.label == label) return &r;
        return nullptr;
    };
    CHECK(!row(build_content(Settings{}, Env{}), "XBOX TEXTURES") && !row(build_content(Settings{}, Env{}), "HD TEXTURES"));
    Env env{};
    env.xbox_textures = true;
    const Content c = build_content(Settings{}, env);
    const Row* r = row(c, "XBOX TEXTURES");
    CHECK(r && r->id == 383 && r->value == 0 && row(c, "HD TEXTURES"));
    Settings t{};
    Edit e;
    e.id = 383;
    e.value = 1;
    CHECK(apply_edit(t, e) && t.xbox_textures);
    CHECK(!safe_settings(s).hd_textures);   // safe mode: no pack, the import's neither
}

TEST(music_folder_and_playlist) {
    // the custom soundtrack (remaster-features §6): the folder survives the ini, the mode is 0..2, default EA Trax only
    CHECK(Settings{}.playlist_mode == 0 && Settings{}.music_folder.empty());
    Settings s{};
    s.music_folder = "D:/My Music/Punk = Rock";
    s.playlist_mode = 2;
    const Settings b = from_ini(to_ini(s));
    CHECK(b.music_folder == s.music_folder && b.playlist_mode == 2 && b == s);
    std::vector<std::string> w;
    CHECK(from_ini("playlist_mode = 3\n", &w).playlist_mode == 0 && !w.empty());
    // the MUSIC section (INTERFACE tab) only for a host that plays the folder (native_play; not the launcher), and no
    // seventh tab (the tab bar's width)
    auto tab = [](const Content& c) -> const Tab* {
        for (const Tab& t : c.tabs)
            for (const Row& r : t.rows)
                if (r.id == kActionChooseMusic) return &t;
        return nullptr;
    };
    CHECK(tab(build_content(Settings{}, Env{})) == nullptr);
    {
        Env both;
        both.music = true;
        both.profile = 1;
        CHECK(build_content(Settings{}, both).tabs.size() == 9);
    }
    Env e;
    e.music = true;
    const Content c = build_content(s, e);
    const Tab* t = tab(c);
    CHECK(t != nullptr);
    if (!t) return;
    bool pick = false, clear = false, mode = false;
    for (const Row& r : t->rows) {
        if (r.id == kActionChooseMusic) pick = r.kind == RowKind::kAction && !r.disabled && r.values[0] == "Punk = Rock";
        if (r.id == kActionClearMusic) clear = !r.disabled;
        if (r.label == "PLAYLIST") {
            mode = r.kind == RowKind::kChoice && r.values.size() == 3 && r.value == 2;
            Settings x;
            CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.playlist_mode == 1);
        }
    }
    CHECK(pick && clear && mode);
    // macintel1: the page in a variable (a range-for over tab(build_content(...))->rows read the rows after the temporary
    // was freed: AddressSanitizer heap-use-after-free; glibc kept the bytes, macOS's allocator did not)
    const Content plain = build_content(Settings{}, e);
    for (const Row& r : tab(plain)->rows) CHECK(r.id != kActionClearMusic);
    // musicvol1: CUSTOM MUSIC VOLUME, 0..200 % in steps of 10 (default 100), right after PLAYLIST, in the ini
    CHECK(Settings{}.custom_music_volume == 100);
    Settings v{};
    v.custom_music_volume = 170;
    CHECK(from_ini(to_ini(v)).custom_music_volume == 170 && from_ini(to_ini(v)) == v);
    w.clear();
    CHECK(from_ini("custom_music_volume = 250\n", &w).custom_music_volume == 100 && !w.empty());
    CHECK(from_ini("custom_music_volume = 0\n").custom_music_volume == 0);
    bool after_playlist = false, vol = false;
    for (const Row& r : t->rows) {
        if (r.label == "CUSTOM MUSIC VOLUME") {
            vol = after_playlist && r.kind == RowKind::kSlider && r.levels == 21 && r.value == 10 && r.level_text == "100%";
            Settings x;
            CHECK(apply_edit(x, Edit{r.id, 20, 0, false}) && x.custom_music_volume == 200);
            CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.custom_music_volume == 0);
            CHECK(apply_edit(x, Edit{r.id, 15, 0, false}) && x.custom_music_volume == 150);
        }
        after_playlist = r.label == "PLAYLIST";
    }
    CHECK(vol);
}

TEST(accessibility_rows) {
    // remaster-features "Accessibility": SCREEN FLASHES, HUD COLOUR FILTER and BOOST in the INTERFACE tab's ACCESSIBILITY
    // section (no seventh tab); the defaults are the game's (Full, Off, Hold), in the file, untouched by the presets
    const Settings d{};
    CHECK(d.screen_flashes == 0 && d.hud_colour_filter == 0 && d.boost_mode == 0 && d.hud_size == 0);
    Settings s;
    s.screen_flashes = 1;
    s.hud_colour_filter = 3;
    s.boost_mode = 1;
    s.hud_size = 2;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("screen_flashes = 1") != std::string::npos && to_ini(s).find("hud_colour_filter = 3") != std::string::npos &&
          to_ini(s).find("boost_mode = 1") != std::string::npos && to_ini(s).find("hud_size = 2") != std::string::npos);
    std::vector<std::string> w;
    const Settings bad = from_ini("screen_flashes = 2\nhud_colour_filter = 4\nboost_mode = -1\nhud_size = 4\n", &w);
    CHECK(bad.screen_flashes == 0 && bad.hud_colour_filter == 0 && bad.boost_mode == 0 && bad.hud_size == 0 && w.size() == 4);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        const Settings q = with_preset(s, p);
        CHECK(q.screen_flashes == 1 && q.hud_colour_filter == 3 && q.boost_mode == 1 && q.hud_size == 2);
    }
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {   // (the launcher's Env too: no music, no card)
        const Content c = build_content(Settings{}, env);
        CHECK(c.tabs.size() == 8);
        const Tab* in = nullptr;
        for (const Tab& t : c.tabs) if (t.label == "GAME") in = &t;
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t sec = 0, flashes = 0, colour = 0, size = 0, boost = 0;
        // settingsui: BOOST in CONTROLS > DRIVING; ACCESSIBILITY ends with a pointer row to it
        const Tab* controls = holding(c, "BOOST");
        CHECK(controls && controls->label == "CONTROLS");
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            if (r.kind == RowKind::kSection && r.label == "ACCESSIBILITY") sec = i;
            Settings x;
            if (r.label == "SCREEN FLASHES") {
                flashes = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 2 && r.value == 0 && !r.disabled);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.screen_flashes == 1);
            }
            if (r.label == "HUD COLOUR FILTER") {
                colour = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 4 && r.value == 0 && r.values[1] == "Deuteranopia" &&
                      r.values[2] == "Protanopia" && r.values[3] == "Tritanopia");
                for (std::size_t v = 0; v < 4; ++v) CHECK(apply_edit(x, Edit{r.id, v, 0, false}) && x.hud_colour_filter == int(v));
            }
            if (r.label == "HUD SIZE") {   // textsize1: the race HUD only (the row says so), 100 % = the game's
                size = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 4 && r.value == 0 && r.values[0] == "100%" && r.values[3] == "150%");
                for (std::size_t v = 0; v < 4; ++v) CHECK(apply_edit(x, Edit{r.id, v, 0, false}) && x.hud_size == int(v));
                CHECK(r.description.find("Menus keep their size") != std::string::npos);
            }
            if (r.label == "BOOST HOLD OR TOGGLE") {
                boost = i;
                CHECK(r.disabled && r.description.find("CONTROLS > DRIVING > BOOST") != std::string::npos);
            }
        }
        for (std::size_t i = 0; controls && i < controls->rows.size(); ++i) {
            const Row& r = controls->rows[i];
            Settings x;
            if (r.label == "BOOST") {
                CHECK(i > 0 && controls->rows[i - 1].label == "ANALOG TRIGGERS");
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "Hold" && r.values[1] == "Toggle" && r.value == 0);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.boost_mode == 1);
                CHECK(r.description.find("input") != std::string::npos);   // says it changes the game's input
            }
        }
        CHECK(sec > 0 && flashes == sec + 1 && colour == sec + 2 && size == sec + 3 && boost == sec + 4);
        Settings set;   // shown as set
        set.hud_colour_filter = 2;
        for (const Tab& t : build_content(set, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "HUD COLOUR FILTER") CHECK(r.value == 2);
    }
}

TEST(ghost_row) {
    // ghost1 (Time Attack ghosts, src/b3replay/ghost.h): GHOST Off / Best lap in the INTERFACE tab's TIME ATTACK section,
    // before ACCESSIBILITY; Off by default (the frames byte-identical), in the file, untouched by the presets
    const Settings d{};
    CHECK(d.ghost == 0);
    Settings s;
    s.ghost = 1;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("ghost = 1") != std::string::npos && to_ini(d).find("ghost = 0") != std::string::npos);
    std::vector<std::string> w;
    CHECK(from_ini("ghost = 2\n", &w).ghost == 0 && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).ghost == 1);
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        const Tab* in = nullptr;
        for (const Tab& t : c.tabs) if (t.label == "GAME") in = &t;
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t sec = 0, row = 0, access = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            if (r.kind == RowKind::kSection && r.label == "MODES") sec = i;   // settingsui: GAME > MODES
            if (r.kind == RowKind::kSection && r.label == "ACCESSIBILITY") access = i;
            if (r.label == "GHOST") {
                row = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "Best lap" && r.value == 0 &&
                      !r.disabled);
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.ghost == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.ghost == 0);
            }
        }
        CHECK(sec > 0 && row == sec + 1 && access > row);
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "GHOST") CHECK(r.value == 1);
    }
}

TEST(speedrun_rows) {
    // speedrun1 (src/b3replay/speedrun.h): SPEEDRUN TIMER Off / Event / Event + splits and INSTANT RESTART Off / On in the
    // INTERFACE tab's SPEEDRUN section, after TIME ATTACK and before ACCESSIBILITY; both Off by default, in the file,
    // untouched by the presets
    const Settings d{};
    CHECK(d.speedrun_timer == 0 && d.instant_restart == 0);
    Settings s;
    s.speedrun_timer = 2;
    s.instant_restart = 1;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("speedrun_timer = 2") != std::string::npos && to_ini(d).find("instant_restart = 0") != std::string::npos);
    std::vector<std::string> w;
    CHECK(from_ini("speedrun_timer = 3\ninstant_restart = 2\n", &w).speedrun_timer == 0 && w.size() == 2);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        CHECK(with_preset(s, p).speedrun_timer == 2);
        CHECK(with_preset(s, p).instant_restart == 1);
    }
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        const Tab all = all_rows(c);
        const Tab* in = &all;
        std::size_t ta = 0, sec = 0, timer = 0, restart = 0, access = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            if (r.kind == RowKind::kSection && r.label == "DRIVING") ta = i;
            if (r.kind == RowKind::kSection && r.label == "MODES") sec = i;
            if (r.kind == RowKind::kSection && r.label == "ACCESSIBILITY") access = i;
            Settings x;
            if (r.label == "SPEEDRUN TIMER") {
                timer = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 3 && r.values[0] == "Off" && r.values[2] == "Event + splits" && r.value == 0);
                CHECK(apply_edit(x, Edit{r.id, 2, 0, false}) && x.speedrun_timer == 2);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.speedrun_timer == 0);
            }
            if (r.label == "INSTANT RESTART") {
                restart = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[1] == "On" && r.value == 0);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.instant_restart == 1);
            }
        }
        // settingsui: SPEEDRUN TIMER in GAME > MODES after GHOST; INSTANT RESTART in CONTROLS > DRIVING after BOOST
        CHECK(ta > 0 && restart == ta + 3 && sec > restart && timer == sec + 2 && access > timer);
        CHECK(holding(c, "SPEEDRUN TIMER")->label == "GAME" && holding(c, "INSTANT RESTART")->label == "CONTROLS");
    }
}

TEST(rumble_rows) {
    // rumble1 (src/native/pad_actuator.h): RUMBLE Off / On (default Off: the requests left pending, as before), RUMBLE
    // STRENGTH 10-100 % (default 100) and TRIGGER RUMBLE (default off) in the INTERFACE tab's CONTROLLER section; in the
    // file, untouched by the presets
    const Settings d{};
    CHECK(d.rumble == 0 && d.rumble_strength == 100 && !d.rumble_triggers);
    CHECK(to_ini(d).find("rumble = 0") != std::string::npos && to_ini(d).find("rumble_strength = 100") != std::string::npos &&
          to_ini(d).find("rumble_triggers = false") != std::string::npos);
    Settings s;
    s.rumble = 1;
    s.rumble_strength = 40;
    s.rumble_triggers = true;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(from_ini("rumble = 1\n").rumble == 1);
    std::vector<std::string> w;
    const Settings bad = from_ini("rumble = 2\nrumble_strength = 5\nrumble_strength = 101\nrumble_triggers = maybe\n", &w);
    CHECK(bad.rumble == 0 && bad.rumble_strength == 100 && !bad.rumble_triggers && w.size() == 4);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        const Settings q = with_preset(s, p);
        CHECK(q.rumble == 1 && q.rumble_strength == 40 && q.rumble_triggers);
    }
    CHECK(matching_preset(s) == Preset::kHigh);
    CHECK(hset_encode(s, 1).size() > 0);   // still fits the HSET record
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        const Tab* in = nullptr;
        for (const Tab& t : c.tabs) if (t.label == "CONTROLS") in = &t;   // settingsui: CONTROLS > FEEDBACK
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t sec = 0, on = 0, strength = 0, triggers = 0, ghost = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            Settings x;
            if (r.kind == RowKind::kSection && r.label == "FEEDBACK") sec = i;
            if (r.kind == RowKind::kSection && r.label == "DRIVING") ghost = i;
            if (r.label == "RUMBLE") {
                on = i;
                CHECK(r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "On" && r.value == 0 &&
                      !r.disabled);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.rumble == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.rumble == 0);
            }
            if (r.label == "RUMBLE STRENGTH") {
                strength = i;
                CHECK(r.kind == RowKind::kSlider && r.levels == 10 && r.value == 9 && r.level_text == "100%");
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.rumble_strength == 10);
                CHECK(apply_edit(x, Edit{r.id, 4, 0, false}) && x.rumble_strength == 50);
                CHECK(apply_edit(x, Edit{r.id, 20, 0, false}) && x.rumble_strength == 100);
            }
            if (r.label == "TRIGGER RUMBLE") {
                triggers = i;
                CHECK(r.kind == RowKind::kToggle && r.value == 0);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.rumble_triggers);
            }
        }
        CHECK(sec == 0 && in->rows[0].kind == RowKind::kSection && on == sec + 1 && strength == sec + 2 && triggers == sec + 3 && ghost > triggers);
        for (const Tab& t : build_content(s, env).tabs)   // shown as set
            for (const Row& r : t.rows) {
                if (r.label == "RUMBLE") CHECK(r.value == 1);
                if (r.label == "RUMBLE STRENGTH") CHECK(r.value == 3 && r.level_text == "40%");
                if (r.label == "TRIGGER RUMBLE") CHECK(r.value == 1);
            }
    }
}

TEST(other_music_apps_row) {
    // nowplaying1 (tools/native/now_playing_popup.h): OTHER MUSIC APPS Off / Show now playing (default Off: no reader, no
    // system call) after PLAYLIST in the INTERFACE tab's MUSIC section (only with the host's music: native_play); in the
    // file, untouched by the presets
    const Settings d{};
    CHECK(d.other_music_apps == 0 && to_ini(d).find("other_music_apps = 0") != std::string::npos);
    Settings s;
    s.other_music_apps = 1;
    CHECK(from_ini(to_ini(s)) == s);
    std::vector<std::string> w;
    CHECK(from_ini("other_music_apps = 2\n", &w).other_music_apps == 0 && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).other_music_apps == 1);
    auto find = [](const Content& c, const std::string& label) -> const Row* {
        for (const Tab& t : c.tabs)
            for (const Row& r : t.rows)
                if (r.label == label) return &r;
        return nullptr;
    };
    Env env;
    CHECK(!find(build_content(s, env), "OTHER MUSIC APPS"));   // no MUSIC section without the host's music
    env.music = true;
    const Content c = build_content(Settings{}, env);
    const Row* r = find(c, "OTHER MUSIC APPS");
    CHECK(r && r->id == 91 && r->kind == RowKind::kChoice && r->value == 0);
    if (!r) return;
    CHECK((r->values == std::vector<std::string>{"Off", "Show now playing"}));
#if defined(__APPLE__)
    CHECK(r->disabled && r->description.find("macOS") != std::string::npos);
#else
    CHECK(!r->disabled && r->description.find("EA TRAX popup") != std::string::npos);
    CHECK(r->description.find("Now:") == std::string::npos);   // off: nothing detected is shown
    env.now_playing = "Spotify: Band - Song, playing";
    const Content c2 = build_content(s, env);
    const Row* on = find(c2, "OTHER MUSIC APPS");
    CHECK(on && on->value == 1 && on->description.find("Now: Spotify: Band - Song, playing.") != std::string::npos);
#endif
    Settings x;
    CHECK(apply_edit(x, Edit{r->id, 1, 0, false}) && x.other_music_apps == 1);
    CHECK(apply_edit(x, Edit{r->id, 0, 0, false}) && x.other_music_apps == 0);
    // right after PLAYLIST and its CUSTOM MUSIC VOLUME (musicvol1)
    const Tab* in = nullptr;
    for (const Tab& t : c.tabs) if (t.label == "AUDIO") in = &t;   // settingsui: AUDIO > MUSIC
    CHECK(in != nullptr);
    if (!in) return;
    for (std::size_t i = 0; i + 2 < in->rows.size(); ++i)
        if (in->rows[i].label == "PLAYLIST")
            CHECK(in->rows[i + 1].label == "CUSTOM MUSIC VOLUME" && in->rows[i + 2].label == "OTHER MUSIC APPS");
}

TEST(adaptive_trigger_rows) {
    // pad1 (src/native/pad_triggers.h): ADAPTIVE TRIGGERS Off / On (default Off: no effect ever sent) and TRIGGER STRENGTH
    // 10-100 % (default 100) after TRIGGER RUMBLE in the INTERFACE tab's CONTROLLER section; in the file, untouched by
    // the presets
    const Settings d{};
    CHECK(d.adaptive_triggers == 0 && d.adaptive_triggers_strength == 100);
    CHECK(to_ini(d).find("adaptive_triggers = 0") != std::string::npos && to_ini(d).find("adaptive_triggers_strength = 100") != std::string::npos);
    Settings s;
    s.adaptive_triggers = 1;
    s.adaptive_triggers_strength = 30;
    CHECK(from_ini(to_ini(s)) == s);
    std::vector<std::string> w;
    const Settings bad = from_ini("adaptive_triggers = 2\nadaptive_triggers_strength = 5\nadaptive_triggers_strength = 101\n", &w);
    CHECK(bad.adaptive_triggers == 0 && bad.adaptive_triggers_strength == 100 && w.size() == 3);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        const Settings q = with_preset(s, p);
        CHECK(q.adaptive_triggers == 1 && q.adaptive_triggers_strength == 30);
    }
    CHECK(matching_preset(s) == Preset::kHigh);
    CHECK(hset_encode(s, 1).size() > 0);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Tab* in = nullptr;
        const Content c = build_content(Settings{}, env);
        for (const Tab& t : c.tabs) if (t.label == "CONTROLS") in = &t;   // settingsui: CONTROLS > FEEDBACK
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t triggers = 0, on = 0, strength = 0, ghost = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            Settings x;
            if (r.label == "TRIGGER RUMBLE") triggers = i;
            if (r.kind == RowKind::kSection && r.label == "DRIVING") ghost = i;
            if (r.label == "ADAPTIVE TRIGGERS") {
                on = i;
                CHECK(r.id == 190 && r.kind == RowKind::kChoice && r.values.size() == 2 && r.value == 0 && !r.disabled);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.adaptive_triggers == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.adaptive_triggers == 0);
            }
            if (r.label == "TRIGGER STRENGTH") {
                strength = i;
                CHECK(r.id == 191 && r.kind == RowKind::kSlider && r.levels == 10 && r.value == 9 && r.level_text == "100%");
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.adaptive_triggers_strength == 10);
                CHECK(apply_edit(x, Edit{r.id, 6, 0, false}) && x.adaptive_triggers_strength == 70);
                CHECK(apply_edit(x, Edit{r.id, 20, 0, false}) && x.adaptive_triggers_strength == 100);
            }
        }
        CHECK(triggers > 0 && on == triggers + 1 && strength == triggers + 2 && ghost > strength);   // (carsel1's row between)
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows) {
                if (r.label == "ADAPTIVE TRIGGERS") CHECK(r.value == 1);
                if (r.label == "TRIGGER STRENGTH") CHECK(r.value == 2 && r.level_text == "30%");
            }
    }
}

TEST(series_shoulders_row) {
    // carsel1 (src/native/input_keys.h ShoulderSeries): CAR SERIES ON L1 / R1 Off / On (default Off: the reports untouched,
    // nothing drawn), row 200, the CONTROLLER section's last row (after TRIGGER STRENGTH, before TIME ATTACK); in the file,
    // untouched by the presets
    const Settings d{};
    CHECK(d.series_shoulders == 0 && to_ini(d).find("series_shoulders = 0") != std::string::npos);
    Settings s;
    s.series_shoulders = 1;
    CHECK(from_ini(to_ini(s)) == s);
    std::vector<std::string> w;
    CHECK(from_ini("series_shoulders = 2\n", &w).series_shoulders == 0 && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).series_shoulders == 1);
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Tab* in = nullptr;
        const Content c = build_content(Settings{}, env);
        for (const Tab& t : c.tabs) if (t.label == "CAR") in = &t;   // settingsui: CAR > GARAGE
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t strength = 0, row = 0, ghost = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            Settings x;
            if (r.label == "GARAGE CAMERA") strength = i;
            if (r.kind == RowKind::kSection && r.label == "GARAGE") ghost = i;
            if (r.label == "CAR SERIES ON L1 / R1") {
                row = i;
                CHECK(r.id == 200 && r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "On" &&
                      r.value == 0 && !r.disabled);
                CHECK(r.description.find("input") != std::string::npos);   // says it changes the game's input
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.series_shoulders == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.series_shoulders == 0);
            }
        }
        // settingsui: CAR > GARAGE: GARAGE CAMERA, then CAR SERIES ON L1 / R1, the tab's last row
        CHECK(ghost > 0 && strength == ghost + 1 && row == strength + 1 && row + 1 == in->rows.size());
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "CAR SERIES ON L1 / R1") CHECK(r.value == 1);
    }
}

// garagecam1 (src/b3replay/garagecam.h): GARAGE CAMERA, on by default, drawn only; the ini key, the row, the presets keep it
TEST(garage_camera_row) {
    const Settings d;
    Settings o;
    o.garage_camera = false;
    CHECK(d.garage_camera && to_ini(d).find("garage_camera") != std::string::npos && !from_ini(to_ini(o)).garage_camera);
    CHECK(!from_ini("garage_camera = 0\n").garage_camera && from_ini("").garage_camera);
    Settings off;
    off.garage_camera = false;
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(!with_preset(off, p).garage_camera);
    for (const Env& env : {Env{true}, Env{false}}) {
        int seen = 0;
        for (const Tab& t : build_content(Settings{}, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "GARAGE CAMERA") {
                    ++seen;
                    CHECK(t.label == "CAR" && r.id == 290 && r.value == 1 && !r.disabled);
                    CHECK(r.description.find("right stick") != std::string::npos && r.description.find("The game is unchanged.") != std::string::npos);
                    Settings x;
                    CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && !x.garage_camera);
                    CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.garage_camera);
                }
        CHECK(seen == 1);
    }
}

TEST(gyro_controls_rows) {
    // pad1: the CONTROLS section's gyro rows (ids 305-309) with the controller device only, after the action rows and
    // before RESET; the host applies them (apply_edit leaves them)
    Env env{true};
    env.controls.on = true;
    env.controls.device = 0;
    env.controls.actions.push_back({"CROSS", "Z", ""});
    env.controls.gyro = false;
    for (const Tab& t : build_content(Settings{}, env).tabs)
        for (const Row& r : t.rows) CHECK(r.label.find("GYRO") == std::string::npos);
    env.controls.device = 1;
    env.controls.gyro = true;
    env.controls.gyro_mode = 2;
    env.controls.gyro_axis = 1;
    env.controls.gyro_sensitivity = 7;   // 200 %
    env.controls.gyro_dead_zone = 4;
    env.controls.gyro_recentre = 3;
    env.controls.gyro_recentre_labels = {"NONE", "TOUCHPAD", "SELECT", "L3", "R3", "MIC", "PADDLE L1", "PADDLE R1"};
    const Tab* in = nullptr;
    const Content c = build_content(Settings{}, env);
    for (const Tab& t : c.tabs) if (t.label == "CONTROLS") in = &t;
    CHECK(in != nullptr);
    if (!in) return;
    std::size_t action = 0, reset = 0, first = 0, n = 0;
    for (std::size_t i = 0; i < in->rows.size(); ++i) {
        const Row& r = in->rows[i];
        if (r.id == kActionCtlFirst) action = i;
        if (r.id == kActionCtlReset) reset = i;
        if (r.id < kRowCtlGyroMode || r.id > kRowCtlGyroRecentre) continue;
        if (!first) first = i;
        ++n;
        CHECK(r.kind == RowKind::kChoice);
        Settings x;
        CHECK(!apply_edit(x, Edit{r.id, 1, 0, false}) && x == Settings{});
        if (r.id == kRowCtlGyroMode) CHECK(r.label == "GYRO STEERING" && r.values.size() == 3 && r.value == 2);
        if (r.id == kRowCtlGyroAxis) CHECK(r.values.size() == 2 && r.value == 1);
        if (r.id == kRowCtlGyroSensitivity) CHECK(r.values.size() == 12 && r.value == 7 && r.values[7] == "200%" && r.values[0] == "25%");
        if (r.id == kRowCtlGyroDeadZone) CHECK(r.values.size() == 11 && r.value == 4);
        if (r.id == kRowCtlGyroRecentre) CHECK(r.values.size() == 8 && r.value == 3 && r.values[3] == "L3");
    }
    // settingsui: the GYRO section after the bindings and their resets (RESET, RESET ALL CONTROLS)
    CHECK(n == 5 && action > 0 && reset > action && first == reset + 3 && in->rows[first - 1].kind == RowKind::kSection &&
          in->rows[first - 1].label == "GYRO" && first + 5 == in->rows.size());
}

TEST(record_replays_row) {
    // replay1 (replay files, src/native/replay_file.h): RECORD REPLAYS Off / On in the INTERFACE tab's REPLAYS section,
    // after TIME ATTACK; Off by default (nothing recorded), in the file, untouched by the presets
    const Settings d{};
    CHECK(d.record_replays == 0);
    Settings s;
    s.record_replays = 1;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("record_replays = 1") != std::string::npos && to_ini(d).find("record_replays = 0") != std::string::npos);
    std::vector<std::string> w;
    CHECK(from_ini("record_replays = 2\n", &w).record_replays == 0 && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).record_replays == 1);
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        const Tab* in = nullptr;
        for (const Tab& t : c.tabs) if (t.label == "GAME") in = &t;   // settingsui: GAME > MODES
        CHECK(in != nullptr);
        if (!in) return;
        std::size_t sec = 0, row = 0, ghost = 0;
        for (std::size_t i = 0; i < in->rows.size(); ++i) {
            const Row& r = in->rows[i];
            if (r.kind == RowKind::kSection && r.label == "MODES") sec = i;
            if (r.label == "GHOST") ghost = i;
            if (r.label == "RECORD REPLAYS") {
                row = i;
                CHECK(r.kind == RowKind::kToggle && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "On" && r.value == 0 && !r.disabled);
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.record_replays == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.record_replays == 0);
            }
        }
        CHECK(sec > 0 && ghost == sec + 1 && row == sec + 3);
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "RECORD REPLAYS") CHECK(r.value == 1);
    }
}

TEST(launcher_at_start_setting) {
    // launchskip1: OPEN THE LAUNCHER AT START On / Off (default On: the launcher shows), in the INTERFACE tab right
    // after DATE FORMAT (never on DISPLAY), row 210; ini key launcher_at_start; untouched by the presets; the launcher
    // reads it (src/b3launcher/core.h skip_launcher)
    const Settings d{};
    CHECK(d.launcher_at_start);
    CHECK(to_ini(d).find("launcher_at_start = true") != std::string::npos);
    Settings s;
    s.launcher_at_start = false;
    CHECK(to_ini(s).find("launcher_at_start = false") != std::string::npos);
    CHECK(from_ini(to_ini(s)) == s && !from_ini(to_ini(s)).launcher_at_start);
    CHECK(from_ini("launcher_at_start = false\n").launcher_at_start == false);
    CHECK(from_ini("launcher_at_start = true\n").launcher_at_start == true);
    CHECK(from_ini("").launcher_at_start == true);   // an older file: on
    std::vector<std::string> w;
    CHECK(from_ini("launcher_at_start = maybe\n", &w).launcher_at_start == true && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(!with_preset(s, p).launcher_at_start);
    CHECK(matching_preset(s) == Preset::kHigh);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        int found = 0;
        for (const Tab& t : c.tabs)
            for (std::size_t i = 0; i < t.rows.size(); ++i) {
                const Row& r = t.rows[i];
                if (r.label != "OPEN THE LAUNCHER AT START") continue;
                ++found;
                CHECK(t.label == "GAME" && i > 0 && t.rows[i - 1].label == "PAUSE WHEN UNFOCUSED");   // settingsui: STARTING AND LOADING
                CHECK(r.id == 210 && r.kind == RowKind::kToggle && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "On" &&
                      r.value == 1 && !r.disabled);
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && !x.launcher_at_start);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.launcher_at_start);
            }
        CHECK(found == 1);
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "OPEN THE LAUNCHER AT START") CHECK(r.value == 0);
        // the DISPLAY tab (the golden's page shot) is as before
        CHECK(c.tabs[0].label == "DISPLAY");
        for (const Row& r : c.tabs[0].rows) CHECK(r.id != 210);
    }
}

TEST(speed_units_row) {
    // units1 (tools/native/speed_units.h): SPEED UNITS MPH / KM/H (default MPH: the US game's), in the INTERFACE tab right
    // before DATE FORMAT (DATE FORMAT stays right before OPEN THE LAUNCHER AT START), row 220; ini key speed_units 0-1;
    // untouched by the presets and safe mode
    const Settings d{};
    CHECK(d.speed_units == 0);
    CHECK(to_ini(d).find("speed_units = 0") != std::string::npos);
    Settings s;
    s.speed_units = 1;
    CHECK(to_ini(s).find("speed_units = 1") != std::string::npos);
    CHECK(from_ini(to_ini(s)) == s && from_ini(to_ini(s)).speed_units == 1);
    CHECK(from_ini("").speed_units == 0);   // an older file: MPH
    std::vector<std::string> w;
    CHECK(from_ini("speed_units = 7\n", &w).speed_units == 0 && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).speed_units == 1);
    CHECK(safe_settings(s).speed_units == 1);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        int found = 0;
        for (const Tab& t : c.tabs)
            for (std::size_t i = 0; i < t.rows.size(); ++i) {
                const Row& r = t.rows[i];
                if (r.label != "SPEED UNITS") continue;
                ++found;
                CHECK(t.label == "GAME" && i + 2 < t.rows.size() && t.rows[i + 1].label == "DATE FORMAT" &&
                      t.rows[i + 2].label == "MODES");   // settingsui: LANGUAGE AND UNITS' last two rows
                CHECK(r.id == 220 && r.kind == RowKind::kChoice && r.values.size() == 2 && r.values[0] == "MPH" && r.values[1] == "KM/H" &&
                      r.value == 0 && !r.disabled);
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.speed_units == 1);
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && x.speed_units == 0);
            }
        CHECK(found == 1);
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "SPEED UNITS") CHECK(r.value == 1);
        CHECK(c.tabs[0].label == "DISPLAY");   // the golden's page shot: DISPLAY as before
        for (const Row& r : c.tabs[0].rows) CHECK(r.id != 220);
    }
}

TEST(photo_row_setting) {
    // photorow1 (tools/native/pc_menu.h): PHOTO MODE IN PAUSE MENU On / Off (default On: the pause menu has the row), in the
    // INTERFACE tab after VIDEO SKIP HINT (never on DISPLAY), row 240; ini key photo_row; the presets (the PS2 ORIGINAL one
    // too: it only sets visuals) and safe mode leave it alone
    const Settings d{};
    CHECK(d.photo_row);
    CHECK(to_ini(d).find("photo_row = true") != std::string::npos);
    Settings s;
    s.photo_row = false;
    CHECK(to_ini(s).find("photo_row = false") != std::string::npos);
    CHECK(from_ini(to_ini(s)) == s && !from_ini(to_ini(s)).photo_row);
    CHECK(from_ini("photo_row = false\n").photo_row == false);
    CHECK(from_ini("photo_row = true\n").photo_row == true);
    CHECK(from_ini("").photo_row == true);   // an older file: on
    std::vector<std::string> w;
    CHECK(from_ini("photo_row = maybe\n", &w).photo_row == true && w.size() == 1);
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) {
        CHECK(!with_preset(s, p).photo_row);                   // off stays off
        CHECK(with_preset(Settings{}, p).photo_row);           // on stays on
    }
    CHECK(safe_settings(s).photo_row == s.photo_row);
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        int found = 0;
        for (const Tab& t : c.tabs)
            for (std::size_t i = 0; i < t.rows.size(); ++i) {
                const Row& r = t.rows[i];
                if (r.label != "PHOTO MODE IN PAUSE MENU") continue;
                ++found;
                CHECK(t.label == "GAME" && i > 0 && t.rows[i - 1].label == "VIDEO SKIP HINT");
                CHECK(r.id == 240 && r.kind == RowKind::kToggle && r.values.size() == 2 && r.values[0] == "Off" && r.values[1] == "On" &&
                      r.value == 1 && !r.disabled);
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 0, 0, false}) && !x.photo_row);
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.photo_row);
            }
        CHECK(found == 1);
        for (const Tab& t : build_content(s, env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "PHOTO MODE IN PAUSE MENU") CHECK(r.value == 0);
        CHECK(c.tabs[0].label == "DISPLAY");
        for (const Row& r : c.tabs[0].rows) CHECK(r.id != 240);
    }
}

TEST(fps_counter_rows_and_old_ini) {
    // fpsoverlay2: FPS COUNTER (Off / Minimal / Standard / Detailed, ini fps_view 0-3, row 255) and its corner (fps_corner 0-3,
    // row 256), first on INTERFACE; an ini from before the views holds fps_counter = true / false (on = Standard)
    const Settings d{};
    CHECK(d.fps_view == 0 && d.fps_corner == 0);
    CHECK(from_ini("fps_counter = true\n").fps_view == 2 && from_ini("fps_counter = on\n").fps_view == 2 && from_ini("fps_counter = 1\n").fps_view == 2);
    CHECK(from_ini("fps_counter = false\n").fps_view == 0 && from_ini("fps_counter = 0\n").fps_view == 0);
    CHECK(from_ini("fps_view = 3\n").fps_view == 3 && from_ini("fps_view = 1\nfps_corner = 2\n").fps_corner == 2);
    CHECK(from_ini("fps_counter = true\nfps_view = 3\n").fps_view == 3);   // the newer key wins, whatever the order
    CHECK(from_ini("fps_view = 1\nfps_counter = true\n").fps_view == 1);
    std::vector<std::string> w;
    CHECK(from_ini("fps_view = 9\nfps_corner = x\nfps_counter = maybe\n", &w).fps_view == 0 && w.size() == 3);
    Settings s;
    s.fps_view = 3;
    s.fps_corner = 1;
    CHECK(from_ini(to_ini(s)) == s);
    CHECK(to_ini(s).find("fps_counter") == std::string::npos && to_ini(s).find("fps_view = 3") != std::string::npos);
    for (const Env& env : {Env{true}, Env{false}}) {
        int found = 0;
        for (const Tab& t : build_content(s, env).tabs)
            for (std::size_t i = 0; i < t.rows.size(); ++i) {
                const Row& r = t.rows[i];
                if (r.label != "FPS COUNTER") continue;
                ++found;
                CHECK(t.label == "GAME" && i == 4 && r.id == 255 && r.kind == RowKind::kChoice && r.values.size() == 4 && r.value == 3);   // settingsui: INTERFACE's 4th
                CHECK(r.values[0] == "Off" && r.values[1] == "Minimal" && r.values[2] == "Standard" && r.values[3] == "Detailed");
                CHECK(t.rows[i + 1].id == 256 && t.rows[i + 1].values.size() == 4 && t.rows[i + 1].value == 1);
                Settings x;
                CHECK(apply_edit(x, Edit{255, 2, 0, false}) && x.fps_view == 2);
                CHECK(apply_edit(x, Edit{256, 3, 0, false}) && x.fps_corner == 3);
            }
        CHECK(found == 1);
    }
    for (Preset p : {Preset::kPs2, Preset::kLow, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(s, p).fps_view == 3);

}
TEST(effect_levels_effects2) {
    // effects2: SPARKS and CRASH DEBRIS are Off / Low / Medium / High (0..3), High the default (the game's whole set);
    // the presets drive them: PS2 ORIGINAL, HIGH, ULTRA High, MEDIUM Medium, LOW Low; ini keys sparks / popcorn
    const Settings d{};
    CHECK(d.sparks == kLevelHigh && d.popcorn == kLevelHigh && !d.battery_saver);
    for (Preset p : {Preset::kPs2, Preset::kHigh, Preset::kUltra}) {
        CHECK(with_preset(d, p).sparks == kLevelHigh && with_preset(d, p).popcorn == kLevelHigh);
        CHECK(matching_preset(with_preset(d, p)) == p);
    }
    CHECK(with_preset(d, Preset::kMedium).sparks == kLevelMedium && with_preset(d, Preset::kMedium).popcorn == kLevelMedium);
    CHECK(with_preset(d, Preset::kLow).sparks == kLevelLow && with_preset(d, Preset::kLow).popcorn == kLevelLow);
    CHECK(matching_preset(with_preset(d, Preset::kLow)) == Preset::kLow && matching_preset(with_preset(d, Preset::kMedium)) == Preset::kMedium);
    CHECK(safe_settings(d).sparks == kLevelLow);
    Settings s = with_preset(d, Preset::kLow);
    s.sparks = kLevelOff;
    CHECK(matching_preset(s) == Preset::kCustom);   // one row changed: Custom
    // the ini round trip at every level, and the files from before the levels (true / false)
    for (int a = 0; a <= 3; ++a)
        for (int b = 0; b <= 3; ++b) {
            Settings x;
            x.sparks = a;
            x.popcorn = b;
            CHECK(from_ini(to_ini(x)) == x);
        }
    CHECK(from_ini("sparks = false\npopcorn = true\n").sparks == kLevelOff && from_ini("sparks = false\npopcorn = true\n").popcorn == kLevelHigh);
    CHECK(from_ini("sparks = on\n").sparks == kLevelHigh && from_ini("sparks = 2\n").sparks == kLevelMedium);
    std::vector<std::string> w;
    CHECK(from_ini("sparks = 4\npopcorn = loud\n", &w).sparks == kLevelHigh && w.size() == 2);   // out of range: the default
    CHECK(std::string(level_name(0)) == "Off" && std::string(level_name(1)) == "Low" && std::string(level_name(2)) == "Medium" &&
          std::string(level_name(3)) == "High");
    CHECK(level_percent(0) == 0 && level_percent(1) == 25 && level_percent(2) == 50 && level_percent(3) == 100);
    // the rows: EFFECTS tab, Off / Low / Medium / High, ids as before, the edit takes the index
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        int found = 0;
        for (const Tab& t : c.tabs)
            for (const Row& r : t.rows) {
                if (r.label != "SPARKS" && r.label != "CRASH DEBRIS") continue;
                ++found;
                CHECK(t.label == "EFFECTS" && r.kind == RowKind::kChoice && r.values.size() == 4 && r.values[0] == "Off" && r.values[3] == "High" && r.value == 3);
                Settings x;
                for (int v = 0; v <= 3; ++v) {
                    CHECK(apply_edit(x, Edit{r.id, std::size_t(v), 0, false}));
                    CHECK((r.label == "SPARKS" ? x.sparks : x.popcorn) == v);
                }
            }
        CHECK(found == 2);
        for (const Tab& t : build_content(with_preset(Settings{}, Preset::kLow), env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "SPARKS" || r.label == "CRASH DEBRIS") CHECK(r.value == 1);
        for (const Row& r : c.tabs[2].rows) CHECK(r.id != 245);   // settingsui: on DISPLAY > WINDOW, not EFFECTS
    }
}

TEST(battery_saver_effects2) {
    // BATTERY SAVER (DISPLAY > WINDOW, row 245, default off): the host's copy while on battery holds the frame rate to 60 and
    // the particle effects to at most Medium; off, or plugged in (the host does not call it), nothing changes
    Settings s;
    CHECK(!s.battery_saver && to_ini(s).find("battery_saver = false") != std::string::npos);
    CHECK(on_battery(s) == s);                    // off: nothing
    s.battery_saver = true;
    CHECK(from_ini(to_ini(s)) == s && from_ini(to_ini(s)).battery_saver);
    Settings b = on_battery(s);
    CHECK(b.frame_cap == 60 && b.sparks == kLevelMedium && b.popcorn == kLevelMedium);
    s.frame_cap = 30;
    s.sparks = kLevelLow;
    b = on_battery(s);
    CHECK(b.frame_cap == 30 && b.sparks == kLevelLow);   // never raised
    s.frame_cap = 144;
    CHECK(on_battery(s).frame_cap == 60);
    CHECK(with_preset(s, Preset::kUltra).battery_saver);   // the presets leave it
    int found = 0;
    for (const Tab& t : build_content(Settings{}, Env{true}).tabs)
        for (const Row& r : t.rows)
            if (r.label == "BATTERY SAVER") {
                ++found;
                CHECK(t.label == "DISPLAY" && r.id == 245 && r.kind == RowKind::kToggle && r.value == 0);   // settingsui: WINDOW
                Settings x;
                CHECK(apply_edit(x, Edit{r.id, 1, 0, false}) && x.battery_saver);
            }
    CHECK(found == 1);
}

TEST(render_scale_effects2) {
    // RENDER SCALE (GRAPHICS tab, row 246, default 100 %): the 3D picture at a share of the window; the presets: LOW 75,
    // the others 100; ini `render_scale` 50..200 (perf4: 125 / 150 / 200 supersampled)
    const Settings d{};
    CHECK(d.render_scale == 100);
    for (Preset p : {Preset::kPs2, Preset::kMedium, Preset::kHigh, Preset::kUltra}) CHECK(with_preset(d, p).render_scale == 100);
    CHECK(with_preset(d, Preset::kLow).render_scale == 75 && matching_preset(with_preset(d, Preset::kLow)) == Preset::kLow);
    CHECK(with_preset(Settings{}, Preset::kHigh) == d);   // the default is still the HIGH preset
    Settings s = d;
    s.render_scale = 67;
    CHECK(from_ini(to_ini(s)) == s && matching_preset(s) == Preset::kCustom);
    CHECK(to_ini(d).find("render_scale = 100") != std::string::npos);
    std::vector<std::string> w;
    CHECK(from_ini("render_scale = 20\n", &w).render_scale == 100 && w.size() == 1);   // out of range: the default
    CHECK(from_ini("render_scale = 85\n").render_scale == 85);
    CHECK(from_ini("render_scale = 200\n").render_scale == 200 && from_ini("render_scale = 150\n").render_scale == 150);   // perf4
    CHECK(from_ini("render_scale = 201\n").render_scale == 100);
    CHECK(from_ini("").render_scale == 100);   // an older file
    CHECK(render_scale_index(100) == 3 && render_scale_index(75) == 5 && render_scale_index(50) == 7 && render_scale_index(60) == 6 &&
          render_scale_index(90) == 4 && render_scale_index(200) == 0 && render_scale_index(150) == 1 && render_scale_index(125) == 2 &&
          render_scale_index(110) == 3);
    int found = 0;
    for (const Env& env : {Env{true}, Env{false}}) {
        const Content c = build_content(Settings{}, env);
        for (const Tab& t : c.tabs)
            for (const Row& r : t.rows)
                if (r.label == "RENDER SCALE") {
                    ++found;
                    CHECK(t.label == "GRAPHICS" && r.id == 246 && r.kind == RowKind::kChoice && r.values.size() == 8 && r.values[0] == "200%" &&
                          r.values[3] == "100%" && r.values[7] == "50%" && r.value == 3);
                    for (std::size_t v = 0; v < r.values.size(); ++v) {
                        Settings x;
                        CHECK(apply_edit(x, Edit{r.id, v, 0, false}) && x.render_scale == kRenderScales[v]);
                    }
                }
        for (const Row& r : c.tabs[0].rows) CHECK(r.id != 246);   // not on DISPLAY
        for (const Tab& t : build_content(with_preset(Settings{}, Preset::kLow), env).tabs)
            for (const Row& r : t.rows)
                if (r.label == "RENDER SCALE") CHECK(r.value == 5);
    }
    CHECK(found == 2);
}

TEST(mods_tab_only_with_code_mod_rows) {   // sdk2b: PC SETTINGS > MODS (the code mods' rows; settingsui: then DEVELOPER MODS)
    {
        const Content plain = build_content(Settings{}, Env{true});
        CHECK(plain.tabs.back().label == "MODS" && plain.tabs.back().rows.size() == 2 && plain.tabs.back().rows[1].label == "DEVELOPER MODS");
    }
    Env e{true};
    Row sec;
    sec.id = kSectionModBase;
    sec.kind = RowKind::kSection;
    sec.label = "MENU-LABEL-SETTING";
    Row r;
    r.id = kModRowBase;
    r.kind = RowKind::kToggle;
    r.label = "QUICK RACE LABEL";
    r.values = {"Off", "On"};
    e.mod_rows = {sec, r};
    const Content c = build_content(Settings{}, e);
    CHECK(!c.tabs.empty() && c.tabs.back().label == "MODS" && c.tabs.back().rows.size() == 4 && c.tabs.back().rows[1].id == kModRowBase &&
          c.tabs.back().rows[2].label == "DEVELOPER" && c.tabs.back().rows[3].label == "DEVELOPER MODS");
    Settings s;
    CHECK(!apply_edit(s, Edit{kModRowBase, 1, 1, false}));   // the host's row, not a setting
}

TEST(language_row_and_edit) {
    // 1.0.1 LANGUAGE (INTERFACE): English and the imported languages; language_edit maps the value to a code
    Env env{true};
    env.languages = {{"FR", "Fran\xC3\xA7" "ais (French)"}, {"IT", "Italiano (Italian)"}};
    Settings s;
    s.language = "IT";
    const Content content = build_content(s, env);   // kept: `row` points into it
    const Row* row = nullptr;
    for (const Tab& t : content.tabs)
        for (const Row& r : t.rows)
            if (r.id == kRowLanguage) row = &r;
    CHECK(row && row->label == "LANGUAGE" && row->values.size() == 3 && row->values[0] == "English" && row->value == 2);
    Settings e;
    CHECK(language_edit(e, env, Edit{kRowLanguage, 1, 0, false}) && e.language == "FR");
    CHECK(language_edit(e, env, Edit{kRowLanguage, 0, 0, false}) && e.language.empty());
    CHECK(language_edit(e, env, Edit{kRowLanguage, 9, 0, false}) && e.language.empty());   // out of range: English
    CHECK(!language_edit(e, env, Edit{kRowSaveScreens, 1, 0, false}));                    // not its row
    // a chosen language that is not imported shows English, and says so
    Settings gone;
    gone.language = "GE";
    for (const Tab& t : build_content(gone, env).tabs)
        for (const Row& r : t.rows)
            if (r.id == kRowLanguage) CHECK(r.value == 0 && r.description.find("not imported") != std::string::npos);
    // the ini keeps it (two letters, upper case), empty for English
    CHECK(from_ini(to_ini(s)).language == "IT");
    CHECK(from_ini("language = it\n").language == "IT");
    CHECK(from_ini("language = Italian\n").language.empty());
}

TEST(translations_cover_the_page) {
    // 1.1 (i18n.h): every label, value and description sentence of a full page has its translation in each language
    Env env{true};
    env.music = true;
    env.language_import = true;
    env.languages = {{"FR", "Fran\xC3\xA7" "ais (French)"}};
    // 1.3.0: the DJ VOICE import rows in their three states (on the disc, being imported, imported)
    env.dj_voices = {{"FR", "Fran\xC3\xA7" "ais (French)", 285556736, 0, -1}, {"GE", "Deutsch (German)", 352722944, 0, 40},
                     {"IT", "Italiano (Italian)", 0, 330457088, -1}, {"GE", "Deutsch (German)", 135000000, 0, -1, true},
                     {"FR", "Fran\xC3\xA7" "ais (French)", 0, 135000000, -1, true}, {"SP", "Espa\xC3\xB1" "ol (Spanish)", 1, 0, 3, true}};
    env.xbox_textures = true;   // xboximport: XBOX TEXTURES
    env.profile = 1;
    env.next_profile = 2;
    env.controls.on = true;
    env.controls.gyro = true;
    env.portable.shown = true;   // portable mode (1.3.0): PORTABLE MODE, disabled with its reason
    env.portable.disabled = "The install folder cannot be written by you: move the game to a folder of your own to use portable mode.";
    Settings s;
    s.hd_pack_dir = "/packs/SLUS-21050";
    s.music_folder = "/music";
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"}) {
        for (std::size_t player = 0; player < 3; ++player)
            for (std::size_t device = 0; device < 2; ++device) {
                env.controls.player = player;
                env.controls.device = device;
                const std::vector<std::string> missing = untranslated(build_content(s, env), code);
                for (const std::string& m : missing)
                    // data, not words: the chosen folders' names and a language's own name stay as they are
                    if (m.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos &&
                        m.find('/') == std::string::npos && m != "music" && m != "SLUS-21050" && m != env.languages[0].second)
                    {
                        std::printf("  %s lacks: %s\n", code, m.c_str());
                        CHECK(false);
                    }
            }
    }
    CHECK(untranslated(build_content(s, env), "XX").empty());   // no table: English, nothing to check
}

TEST(translations_cover_the_shortcuts) {
    // 1.2.1: CONTROLS > Shortcuts (player 2 on the page): every port action's label and use sentence, and the Bound /
    // CONFIRM sentences, in each language. The rows as PcPage::controls_env builds them (src/b3replay/pcpage.cpp); the
    // bound inputs are key and button names and stay as they are.
    namespace in = b3native::input;
    const in::Controls defaults = in::default_controls();
    Env env{true};
    env.languages = {{"FR", "Fran\xC3\xA7" "ais (French)"}};
    env.controls.on = true;
    env.controls.player = 2;
    for (std::size_t device = 0; device < 2; ++device) {
        const in::Device dev = device == 0 ? in::Device::Keyboard : in::Device::Gamepad;
        env.controls.device = device;
        env.controls.actions.clear();
        std::vector<std::string> values;
        for (int a = 0; a < in::kPortActions; ++a) {
            const in::PortAction pa = in::PortAction(a);
            if (dev == in::Device::Gamepad && !in::port_has_pad(pa)) continue;
            Env::Controls::Action row;
            // the ids the hosts give (src/b3replay/pcpage.cpp port_row_id): photoblur's block, todrace's 413; the rest positional
            row.id = pa == in::PortAction::PhotoMotion ? kActionCtlPhotoFirst : pa == in::PortAction::PhotoShutterFaster ? kActionCtlPhotoFirst + 1
                   : pa == in::PortAction::PhotoShutterSlower ? kActionCtlPhotoFirst + 2 : pa == in::PortAction::RaceTime ? kActionCtlRaceTime : 0;
            row.label = in::port_label(pa);
            row.value = in::bound_port_text(defaults, pa, dev, in::PadStyle::PlayStation);
            row.description = std::string(in::port_use(pa)) + " Bound: " + row.value + ". CONFIRM: press a new " +
                              (dev == in::Device::Keyboard ? "key" : "button, stick or trigger") + " for it; RIGHT: add one more" +
                              (dev == in::Device::Gamepad ? " (held together with the rest)" : "") +
                              "; LEFT: remove the last; SQUARE: back to the default.";
            values.push_back(row.value);
            env.controls.actions.push_back(std::move(row));
        }
        CHECK(env.controls.actions.size() >= (dev == in::Device::Keyboard ? std::size_t(in::kPortActions) : std::size_t(20)));
        for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"}) {
            const Content c = build_content(Settings{}, env);
            for (const std::string& m : untranslated(c, code))
                // numbers (the other tabs' values) and the bound inputs are not words
                if (m.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos &&
                    std::find(values.begin(), values.end(), m) == values.end() && m != env.languages[0].second) {
                    std::printf("  %s lacks: %s\n", code, m.c_str());
                    CHECK(false);
                }
            // the labels and use sentences really change (no entry left English by mistake), the inputs do not
            Content t = c;
            translate(t, code);
            std::size_t rows = 0;
            for (std::size_t k = 0; k < t.tabs.size(); ++k)
                for (std::size_t r = 0; r < t.tabs[k].rows.size(); ++r)
                    for (const Env::Controls::Action& a : env.controls.actions)
                        if (c.tabs[k].rows[r].label == a.label && c.tabs[k].rows[r].description == a.description) {
                            ++rows;
                            CHECK(t.tabs[k].rows[r].description.find(a.value) != std::string::npos);
                            CHECK(t.tabs[k].rows[r].description.compare(0, 20, a.description, 0, 20) != 0);
                            // French has its own word for every label (other languages keep a few: SCREENSHOT)
                            if (std::string(code) == "FR") CHECK(t.tabs[k].rows[r].label != a.label);
                        }
            CHECK(rows == env.controls.actions.size());
        }
    }
}

TEST(translations_cover_the_controls_change_notes) {
    // polish122: the CONTROLS change notes (in the game: the LAST CHANGE row, src/b3replay/pcpage.cpp controls_changed; the
    // launcher's Controls page note, src/b3launcher/main.cpp controls_save) are composed from these words, each translated
    // on its own (the key and button names between them stay). Every one is in every language's table.
    namespace in = b3native::input;
    std::vector<std::string> words = {
        // the game's page
        "PLAYER", "KEYBOARD", "CONTROLLER", "DRIVING", "MENUS", "SHORTCUTS", "CONTROLLER LAYOUT", "RESET TO DEFAULTS",
        "ALL CONTROLS RESET TO DEFAULTS", "PRESS RESET ALL CONTROLS AGAIN TO PUT EVERYTHING BACK (ANY OTHER ROW CANCELS)",
        "(this run only)", "(NOT SAVED: {})", "GYRO STEERING", "GYRO AXIS", "GYRO SENSITIVITY", "GYRO DEAD ZONE",
        "GYRO RECENTRE", "Off", "Mix", "Replace", "Tilt", "Turn", "{} DEG",
        // the launcher's
        "{}: saved (the game reads it at its next start).", "Cannot save the controls:", "Player 1", "Player 2", "Shortcuts",
        "controller layout", "gyro steering", "gyro axis", "gyro sensitivity", "gyro dead zone", "gyro recentre button",
        "keyboard reset to the defaults", "controller reset to the defaults", "All controls reset to the defaults",
        "Xbox", "PlayStation", "Custom"};
    for (in::PadStyle f : {in::PadStyle::Generic, in::PadStyle::PlayStation, in::PadStyle::Xbox, in::PadStyle::Nintendo})
        for (bool connected : {false, true}) words.push_back(in::auto_text(f, connected));
    for (int k = 0; k < in::kActions; ++k)
        if (std::strpbrk(in::action_label(in::Ds2(k)), " -")) words.push_back(in::action_label(in::Ds2(k)));   // the button names stay
    for (int k = 0; k < in::kPortActions; ++k) words.push_back(in::port_label(in::PortAction(k)));
    for (const i18n::Table& t : i18n::kTables)
        for (const std::string& w : words) {
            bool found = false;
            for (std::size_t e = 0; e < t.count && !found; ++e) found = w == t.entries[e].en;
            if (!found) std::printf("  %s lacks: %s\n", t.code, w.c_str());
            CHECK(found);
        }
    // the launcher's form: the {} entry's text with {} kept, then filled; the game's: a whole status sentence
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"}) {
        const std::string t = translate_text("{}: saved (the game reads it at its next start).", code);
        CHECK(t.find("{}") != std::string::npos && t.find("saved") == std::string::npos);
        CHECK(translate_text("(NOT SAVED: disk full)", code).find("disk full") != std::string::npos);
        CHECK(translate_text("(NOT SAVED: disk full)", code) != "(NOT SAVED: disk full)");
    }
    CHECK(translate_text("{}: saved (the game reads it at its next start).", "") == "{}: saved (the game reads it at its next start).");
}

TEST(translations_cover_the_player_rows) {
    // 1.2.1: CONTROLS > Player 1 / Player 2: every game action's label, its "Driving: ..." and "Menus: ..." sentences and
    // the Bound / CONFIRM sentences, in each language, on the keyboard (driving and menus views) and the controller. The
    // rows as PcPage::controls_env builds them (src/b3replay/pcpage.cpp); the launcher's Controls page (src/b3launcher/
    // main.cpp) builds the same descriptions. The bound inputs are key and button names and stay as they are.
    namespace in = b3native::input;
    const in::Controls defaults = in::default_controls();
    Env env{true};
    env.languages = {{"FR", "Fran\xC3\xA7" "ais (French)"}};
    env.controls.on = true;
    for (std::size_t player = 0; player < 2; ++player)
        for (std::size_t view = 0; view < 4; ++view) {   // keyboard driving, keyboard menus, controller driving, controller menus
            const in::Device dev = view >= 2 ? in::Device::Gamepad : in::Device::Keyboard;
            const in::KeyCtx ctx = view % 2 == 0 ? in::KeyCtx::Drive : in::KeyCtx::Menu;   // padlayout: the controller's views too
            env.controls.player = player;
            env.controls.device = view >= 2 ? 1 : 0;
            env.controls.view = view % 2;
            env.controls.actions.clear();
            std::vector<std::string> values;
            for (in::Ds2 a : in::action_order()) {
                Env::Controls::Action row;
                row.label = in::action_label(a);
                row.value = in::bound_text(defaults, int(player), dev, a, ctx, in::PadStyle::PlayStation);
                const std::string drive = in::action_use(a, true), menus = in::action_use(a, false);
                std::string use;
                if (!drive.empty()) use += "Driving: " + drive + ". ";
                if (!menus.empty()) use += "Menus: " + menus + ". ";
                row.description = use + "Bound: " + row.value + ". CONFIRM: press a new " +
                                  (dev == in::Device::Keyboard ? "key" : "button, stick or trigger") +
                                  " for it; RIGHT: add one more; LEFT: remove the last; SQUARE: back to the default.";
                values.push_back(row.value);
                env.controls.actions.push_back(std::move(row));
            }
            CHECK(env.controls.actions.size() == std::size_t(in::kActions));
            for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"}) {
                const Content c = build_content(Settings{}, env);
                for (const std::string& m : untranslated(c, code))
                    if (m.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos &&
                        std::find(values.begin(), values.end(), m) == values.end() && m != env.languages[0].second) {
                        std::printf("  %s lacks: %s\n", code, m.c_str());
                        CHECK(false);
                    }
                // every use sentence really changes (no entry left English by mistake), the bound inputs do not
                Content t = c;
                translate(t, code);
                std::size_t rows = 0;
                for (std::size_t k = 0; k < t.tabs.size(); ++k)
                    for (std::size_t r = 0; r < t.tabs[k].rows.size(); ++r)
                        for (const Env::Controls::Action& a : env.controls.actions)
                            if (c.tabs[k].rows[r].label == a.label && c.tabs[k].rows[r].description == a.description) {
                                ++rows;
                                const std::string& d = t.tabs[k].rows[r].description;
                                CHECK(d.find(a.value) != std::string::npos);
                                CHECK(d.find("Driving:") == std::string::npos && d.find("Menus:") == std::string::npos);
                                CHECK(d.find("Bound:") == std::string::npos);
                            }
                CHECK(rows == env.controls.actions.size());
            }
        }
}

TEST(translations_cover_the_controller_layout) {
    // 1.2.1 padlayout: CONTROLS > a player > Controller: the CONTROLLER LAYOUT row (its label, every value Auto can be
    // worded as for a pad, Xbox, PlayStation, Custom, its description) and BUTTONS FOR (label, values, description), in
    // each language, as PcPage::controls_env builds them; the launcher's Controls page uses the same words ("Layout",
    // "Buttons for")
    namespace in = b3native::input;
    Env env{true};
    env.languages = {{"FR", "Fran\xC3\xA7" "ais (French)"}};
    env.controls.on = true;
    env.controls.device = 1;
    std::vector<std::string> autos;
    for (in::PadStyle f : {in::PadStyle::Generic, in::PadStyle::PlayStation, in::PadStyle::Xbox, in::PadStyle::Nintendo})
        for (bool connected : {false, true}) autos.push_back(in::auto_text(f, connected));
    in::PlayerControls p;
    for (in::PadLayout l : {in::PadLayout::Xbox, in::PadLayout::PlayStation, in::PadLayout::Nintendo, in::PadLayout::Custom}) {
        p.layout = l;
        autos.push_back(in::layout_text(p, in::PadStyle::Xbox, true));   // the change notes' names (the fixed values)
    }
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"}) {
        for (const std::string& a : autos) {   // every value really changes or is a name kept as it is
            const std::string t = translate_text(a, code);
            CHECK(!t.empty());
            if (a.rfind("Auto:", 0) == 0 || a == "Custom") {
                if (t == a) std::printf("  %s lacks: %s\n", code, a.c_str());
                CHECK(t != a);
            }
        }
        for (const char* w : {"Buttons for", "CONTROLLER LAYOUT", "BUTTONS FOR"}) CHECK(translate_text(w, code) != w);
        for (std::size_t player = 0; player < 2; ++player)
            for (std::size_t view = 0; view < 2; ++view)
                for (std::size_t layout = 0; layout < 5; ++layout)   // padpaint: Nintendo the fourth
                    for (const std::string& a : autos) {
                        if (a.rfind("Auto:", 0) != 0) continue;
                        env.controls.player = player;
                        env.controls.view = view;
                        env.controls.layout = layout;
                        env.controls.layout_auto = a;
                        const Content c = build_content(Settings{}, env);
                        bool row = false, buttons = false;
                        for (const Tab& tab : c.tabs)
                            for (const Row& r : tab.rows) {
                                if (r.id == kRowCtlLayout) {
                                    row = r.values.size() == 5 && r.values[0] == a && r.value == layout;
                                }
                                if (r.id == kRowCtlView) buttons = r.label == "BUTTONS FOR";
                            }
                        CHECK(row && buttons);
                        for (const std::string& m : untranslated(c, code))
                            if (m.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos &&
                                m != env.languages[0].second && m != "Xbox" && m != "PlayStation" && m != "Nintendo") {
                                std::printf("  %s lacks: %s\n", code, m.c_str());
                                CHECK(false);
                            }
                    }
    }
    // no layout row on the keyboard nor the shortcuts
    env.controls.device = 0;
    env.controls.player = 0;
    bool any = false;
    for (const Tab& tab : build_content(Settings{}, env).tabs)
        for (const Row& r : tab.rows) any = any || r.id == kRowCtlLayout;
    env.controls.device = 1;
    env.controls.player = 2;
    for (const Tab& tab : build_content(Settings{}, env).tabs)
        for (const Row& r : tab.rows) any = any || r.id == kRowCtlLayout;
    CHECK(!any);
}

TEST(translate_page) {
    Env env{true};
    env.languages = {{"FR", "Fran\xC3\xA7" "ais (French)"}, {"IT", "Italiano (Italian)"}};
    Settings s;
    const Content english = build_content(s, env);
    // English (and a language without a table) leaves the page as it is
    Content same = english;
    translate(same, "");
    translate(same, "XX");
    CHECK(same.tabs.size() == english.tabs.size() && same.title == "SETTINGS");
    for (std::size_t k = 0; k < same.tabs.size(); ++k) CHECK(same.tabs[k].label == english.tabs[k].label);
    // French: the same rows (ids, kinds, value counts), other words
    Content fr = english;
    translate(fr, "FR");
    CHECK(fr.title != "SETTINGS" && fr.footer_back != english.footer_back);
    for (std::size_t k = 0; k < fr.tabs.size(); ++k) {
        CHECK(fr.tabs[k].rows.size() == english.tabs[k].rows.size());
        for (std::size_t r = 0; r < fr.tabs[k].rows.size(); ++r) {
            const Row& a = fr.tabs[k].rows[r];
            const Row& b = english.tabs[k].rows[r];
            CHECK(a.id == b.id && a.kind == b.kind && a.value == b.value && a.values.size() == b.values.size());
        }
    }
    // a `{}` entry carries the text it stands for; an unknown sentence stays, the known ones around it change
    const std::string chosen = translate_text("Chosen: /my packs/SLUS-21050 (in use).", "FR");
    CHECK(chosen.find("/my packs/SLUS-21050") != std::string::npos && chosen.find("Chosen") == std::string::npos);
    const std::string mixed = translate_text("Smooths jagged edges. Something new here. Applies at the next start.", "FR");
    CHECK(mixed.find("Something new here.") != std::string::npos && mixed.find("Smooths") == std::string::npos &&
          mixed.find("Applies") == std::string::npos);
    // the page shows the chosen language only when it is imported
    s.language = "IT";
    CHECK(ui_language(s, env) == "IT");
    s.language = "GE";
    CHECK(ui_language(s, env).empty());
    s.language.clear();
    CHECK(ui_language(s, env).empty());
}

TEST(chase_camera_and_upscaling_rows) {
    // 1.1: CAMERA DISTANCE / HEIGHT (CAMERA tab, after FOV CORRECTION) and UPSCALING (GRAPHICS, after RENDER SCALE)
    Settings s;
    CHECK(s.camera_distance == 100 && s.camera_height == 0 && s.upscaling == 1);
    const Content c = build_content(s, Env{true});
    const Row* dist = nullptr;
    const Row* height = nullptr;
    const Row* up = nullptr;
    for (const Tab& t : c.tabs)
        for (std::size_t k = 0; k < t.rows.size(); ++k) {
            const Row& r = t.rows[k];
            if (r.label == "CAMERA DISTANCE") {
                dist = &r;
                CHECK(t.label == "DISPLAY" && k > 0 && t.rows[k - 1].label == "FOV CORRECTION");   // settingsui: DISPLAY > CAMERA
            }
            if (r.label == "CAMERA HEIGHT") height = &r;
            if (r.label == "UPSCALING") {
                up = &r;
                CHECK(k > 0 && t.rows[k - 1].label == "RENDER SCALE");
            }
        }
    CHECK(dist && dist->values[dist->value] == "Game" && height && height->values[height->value] == "Game");
    CHECK(up && up->values[up->value] == "Sharp");
    Settings e;
    CHECK(apply_edit(e, Edit{dist->id, 4, 0, false}) && e.camera_distance == 130);
    CHECK(apply_edit(e, Edit{dist->id, 0, 0, false}) && e.camera_distance == 90);
    CHECK(apply_edit(e, Edit{height->id, 4, 0, false}) && e.camera_height == 100);
    CHECK(apply_edit(e, Edit{up->id, 0, 0, false}) && e.upscaling == 0);
    const Settings back = from_ini(to_ini(e));
    CHECK(back.camera_distance == 90 && back.camera_height == 100 && back.upscaling == 0);
    CHECK(from_ini("camera_distance = 125\n").camera_distance == 100);   // not a step: the game's
    CHECK(from_ini("camera_height = 30\n").camera_height == 0);
    CHECK(from_ini("camera_distance = 200\n").camera_distance == 100);   // out of range
}

TEST(car_paint_rows) {
    // carpaint (remaster-features §15): CAR > CAR PAINT, first on the tab; off by default, the colour rows disabled
    // until Custom, the sliders in steps of 15, kept in the ini
    Settings s;
    CHECK(s.car_paint == 0 && s.car_decals && s.paint_red == 195 && s.paint_green == 30 && s.paint_blue == 30);   // padpaint: on the steps
    Content c = build_content(s, Env{true});
    const Row *paint = nullptr, *red = nullptr, *blue = nullptr, *decals = nullptr;
    for (const Tab& t : c.tabs)
        for (std::size_t k = 0; k < t.rows.size(); ++k) {
            const Row& r = t.rows[k];
            if (r.label == "CAR PAINT" && r.kind != RowKind::kSection) { paint = &r; CHECK(t.label == "CAR"); }   // settingsui
            if (r.label == "PAINT RED") red = &r;
            if (r.label == "PAINT BLUE") blue = &r;
            if (r.label == "DECALS") decals = &r;
        }
    CHECK(paint && red && blue && decals);
    if (!paint || !red || !blue || !decals) return;
    CHECK(paint->values[paint->value] == "Game" && red->disabled && red->level_text == "195" && red->value == 13);
    CHECK(red->swatch == 0xC31E1E && blue->swatch == 0xC31E1E);   // padpaint: the colour the rows make
    CHECK(decals->value == 1);
    Settings e;
    CHECK(apply_edit(e, Edit{paint->id, 1, 0, false}) && e.car_paint == 1);
    CHECK(apply_edit(e, Edit{blue->id, 17, 0, false}) && e.paint_blue == 255);
    CHECK(apply_edit(e, Edit{red->id, 0, 0, false}) && e.paint_red == 0);
    CHECK(apply_edit(e, Edit{decals->id, 0, 0, false}) && !e.car_decals);
    c = build_content(e, Env{true});
    for (const Tab& t : c.tabs) for (const Row& r : t.rows) if (r.label == "PAINT BLUE") CHECK(!r.disabled && r.level_text == "255");
    const Settings back = from_ini(to_ini(e));
    CHECK(back.car_paint == 1 && back.paint_red == 0 && back.paint_blue == 255 && !back.car_decals);
    CHECK(from_ini("paint_green = 100\n").paint_green == 100);   // the ini takes any value; the slider shows the nearest step
    CHECK(from_ini("paint_green = 300\n").paint_green == 30);    // out of range: the default
    CHECK(with_preset(e, Preset::kLow).car_paint == 1);           // the presets leave it
}

// padpaint: every default colour is on the rows' steps (a value the rows can give back after a change); FINISH and
// PLAYER 2 CAR PAINT after DECALS (CAR > CAR PAINT), with their row ids; off by default; kept in the ini
TEST(car_paint_finish_and_player2_rows) {
    Settings s;
    for (int v : {s.paint_red, s.paint_green, s.paint_blue, s.p2_paint_red, s.p2_paint_green, s.p2_paint_blue}) CHECK(v % 15 == 0);
    CHECK(s.paint_finish == 0 && s.p2_car_paint == 0 && s.p2_car_decals);
    const Content c = build_content(s, Env{true});
    const Tab* g = nullptr;
    for (const Tab& t : c.tabs) if (t.label == "CAR") g = &t;   // settingsui: CAR > CAR PAINT, then PLAYER 2 CAR PAINT
    CHECK(g != nullptr);
    if (!g) return;
    std::size_t at = 0;
    while (at < g->rows.size() && g->rows[at].label != "FINISH") ++at;
    CHECK(at + 9 < g->rows.size());
    if (at + 9 >= g->rows.size()) return;
    const Row& fin = g->rows[at];
    CHECK(fin.id == 285 && fin.label == "FINISH" && fin.values.size() == 3 && fin.values[2] == "Pearl" && fin.value == 0);
    CHECK(g->rows[at + 1].label == "DECALS");
    CHECK(g->rows[at + 2].kind == RowKind::kSection && g->rows[at + 2].label == "PLAYER 2 CAR PAINT");
    const std::uint32_t ids[6] = {286, 293, 287, 288, 289, 291};   // (293: P2 COLOUR, the ready colours)
    for (int k = 0; k < 6; ++k) CHECK(g->rows[at + 3 + std::size_t(k)].id == ids[k]);
    CHECK(g->rows[at + 5].label == "P2 PAINT RED" && g->rows[at + 5].disabled && g->rows[at + 5].swatch == 0x1E5AD2);
    CHECK(g->rows[at + 8].label == "P2 DECALS" && g->rows[at + 8].value == 1);
    Settings e;
    CHECK(apply_edit(e, Edit{285, 2, 0, false}) && e.paint_finish == 2);
    CHECK(apply_edit(e, Edit{286, 1, 0, false}) && e.p2_car_paint == 1);
    CHECK(apply_edit(e, Edit{287, 17, 0, false}) && e.p2_paint_red == 255);
    CHECK(apply_edit(e, Edit{289, 0, 0, false}) && e.p2_paint_blue == 0);
    CHECK(apply_edit(e, Edit{291, 0, 0, false}) && !e.p2_car_decals);
    const Settings back = from_ini(to_ini(e));
    CHECK(back.paint_finish == 2 && back.p2_car_paint == 1 && back.p2_paint_red == 255 && back.p2_paint_blue == 0 && !back.p2_car_decals);
    CHECK(from_ini("paint_finish = 3\n").paint_finish == 0);
    // every word of the new rows has its translation
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"})
        for (const std::string& m : untranslated(c, code))
            if (m.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos && m != "Metallic" &&
                (m.rfind("P2 ", 0) == 0 || m.find("PAINT") != std::string::npos || m.find("FINISH") != std::string::npos ||
                 m.find("aint") != std::string::npos || m.find("Gloss") != std::string::npos || m.find("Pearl") != std::string::npos)) {
                std::printf("  %s lacks: %s\n", code, m.c_str());
                CHECK(false);
            }
}

TEST(night_rows) {
    // night (remaster-features §17 / §18): 1.3.0 holds TIME OF DAY, MOON and AMBIENT OCCLUSION back (night still being
    // finished; AO draws noise lines): only SURFACE DETAIL (372) shows in GRAPHICS > LIGHTING. The ids, the edits and the
    // ini keys stay, so the rows come back unchanged
    Settings s;
    CHECK(s.time_of_day == 0 && s.moon && !s.surface_detail && !s.ambient_occlusion);
    Content c = build_content(s, Env{true});
    const Row* surf = nullptr;
    bool held = false;
    for (const Tab& t : c.tabs)
        for (std::size_t k = 0; k < t.rows.size(); ++k) {
            const Row& r = t.rows[k];
            if (r.label == "TIME OF DAY" || r.label == "MOON" || r.label == "AMBIENT OCCLUSION") held = true;
            if (r.label == "SURFACE DETAIL") { surf = &r; CHECK(t.label == "GRAPHICS" && k > 1 && t.rows[k - 1].label == "LIGHTING"); }
        }
    CHECK(!held && surf);
    if (!surf) return;
    CHECK(surf->id == 372 && surf->value == 0);
    Settings e;
    CHECK(apply_edit(e, Edit{370, 1, 0, false}) && e.time_of_day == 1);
    CHECK(apply_edit(e, Edit{371, 0, 0, false}) && !e.moon);
    CHECK(apply_edit(e, Edit{surf->id, 1, 0, false}) && e.surface_detail);
    CHECK(apply_edit(e, Edit{373, 1, 0, false}) && e.ambient_occlusion);
    const Settings back = from_ini(to_ini(e));
    CHECK(back.time_of_day == 1 && !back.moon && back.surface_detail && back.ambient_occlusion);
    CHECK(from_ini("time_of_day = 2\n").time_of_day == 0);   // out of range: the default
    const Settings low = with_preset(e, Preset::kLow);
    CHECK(low.time_of_day == 1 && low.surface_detail && low.ambient_occlusion);   // the presets leave them
}

TEST(sun_shadow_row) {
    // sunshadow (remaster-features §19): GRAPHICS > LIGHTING's last row, SUN SHADOWS Off / On (id 390, ini `sun_shadows`), off by
    // default; the PS2 and Low presets turn it off, the others leave it; translated in every language
    Settings s;
    CHECK(!s.sun_shadows);
    Content c = build_content(s, Env{true});
    const Row* row = nullptr;
    for (const Tab& t : c.tabs)
        for (std::size_t k = 0; k < t.rows.size(); ++k)
            if (t.rows[k].label == "SUN SHADOWS") {
                row = &t.rows[k];
                CHECK(t.label == "GRAPHICS" && k + 1 == t.rows.size() && t.rows[k - 1].label == "SURFACE DETAIL");   // (1.3.0: AO held back)
            }
    CHECK(row != nullptr);
    if (!row) return;
    CHECK(row->id == 390 && row->kind == RowKind::kToggle && row->value == 0);
    Settings e;
    CHECK(apply_edit(e, Edit{row->id, 1, 0, false}) && e.sun_shadows);
    CHECK(from_ini(to_ini(e)).sun_shadows);
    CHECK(!from_ini(to_ini(s)).sun_shadows);
    CHECK(!with_preset(e, Preset::kPs2).sun_shadows && !with_preset(e, Preset::kLow).sun_shadows);
    CHECK(with_preset(e, Preset::kMedium).sun_shadows && with_preset(e, Preset::kHigh).sun_shadows && with_preset(e, Preset::kUltra).sun_shadows);
    CHECK(matching_preset(Settings{}) == Preset::kHigh);   // the defaults are still the High preset
    CHECK(matching_preset(e) == Preset::kHigh);            // (High leaves the row)
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"}) {
        Content t = build_content(e, Env{true});
        translate(t, code);
        for (const Tab& tab : t.tabs)
            for (const Row& r : tab.rows)
                if (r.id == 390) CHECK(r.label != "SUN SHADOWS" && r.description.find("cast by the sun") == std::string::npos &&
                                       r.description.find("Sharper") == std::string::npos && r.description.find("bridges") == std::string::npos);
    }
}

TEST(row_ids_unique) {
    // Every row, action and section id on the page is unique, across every configuration of the host's sections (three
    // collisions on 2026-11-19: CAR PAINT / GARAGE CAMERA at 280, night's first rows at 350-351 on the CONTROLS host ids).
    // Within one page no id may appear twice; and no row of the page's own (built without the host sections) may sit on
    // an id page.h reserves for the host's rows and actions.
    std::vector<Content> pages;
    Env base{true};
    base.music = true;
    base.portable.shown = true;   // portable mode (1.3.0): PORTABLE MODE (405)
    base.language_import = true;
    base.languages = {{"FR", "Fran\xC3\xA7" "ais (French)"}};
    base.dj_voices = {{"FR", "F", 1, 0, -1}, {"GE", "G", 0, 1, -1}, {"IT", "I", 1, 0, 5}, {"SP", "S", 1, 1, -1},   // 1.3.0
                      {"FR", "F", 1, 0, -1, true}, {"GE", "G", 0, 1, -1, true}, {"IT", "I", 1, 0, 5, true}, {"SP", "S", 1, 1, -1, true}};
    base.profile = 1;
    base.next_profile = 2;
    Settings s;
    s.hd_pack_dir = "/packs/SLUS-21050";
    s.music_folder = "/music";
    s.car_paint = 1;
    s.time_of_day = 1;
    const Content own = build_content(s, base);
    pages.push_back(own);
    for (std::size_t player = 0; player < 3; ++player)
        for (std::size_t device = 0; device < 2; ++device) {
            Env e = base;
            e.controls.on = true;
            e.controls.gyro = true;
            e.controls.player = player;
            e.controls.device = device;
            e.controls.status = "PLAYER 1 CROSS: B";
            e.controls.actions = {{"CROSS", "ENTER, Z", "Driving: accelerate."}, {"CIRCLE", "X, H", ""},
                                  {"PHOTO: MOTION BLUR", "J", "", kActionCtlPhotoFirst}};   // photoblur: the second block
            pages.push_back(build_content(s, e));
        }
    for (const Content& c : pages) {
        std::map<std::uint32_t, std::string> seen;
        for (const Tab& t : c.tabs)
            for (const Row& r : t.rows) {
                const auto [it, fresh] = seen.emplace(r.id, t.label + " > " + r.label);
                if (!fresh) std::printf("  id %u twice: %s and %s > %s\n", r.id, it->second.c_str(), t.label.c_str(), r.label.c_str());
                CHECK(fresh);
            }
    }
    const std::uint32_t reserved[][2] = {{kActionExportSave, kActionTuneOverlay}, {kRowCtlPlayer, kRowCtlLayout},
                                          {kRowLanguage, kActionImportLanguages}, {kActionDjFirst, kActionDjLast},
                                          {kActionTrainingFirst, kActionTrainingLast}};
    for (const Tab& t : own.tabs)
        for (const Row& r : t.rows) {
            if (r.kind == RowKind::kSection || r.id == kRowLanguage || r.id == kActionImportLanguages || (r.id >= kActionExportSave && r.id <= kActionTuneOverlay) ||
                (r.id >= kActionDjFirst && r.id <= kActionDjLast) || (r.id >= kActionTrainingFirst && r.id <= kActionTrainingLast))
                continue;   // (the page builds these host rows itself)
            for (const auto& range : reserved) {
                const bool clash = r.id >= range[0] && r.id <= range[1];
                if (clash) std::printf("  %s > %s (id %u) on a host id (%u..%u)\n", t.label.c_str(), r.label.c_str(), r.id, range[0], range[1]);
                CHECK(!clash);
            }
        }
}

TEST(dj_voice_rows) {
    // 1.3.0 VOICES (INTERFACE, after LANGUAGE): Language by default, English keeps the US DJ; kept in the ini. The
    // launcher's import rows: one per language it lists, by its fixed id, Import / Cancel / Remove by state
    const auto find = [](const Content& c, std::uint32_t id) -> const Row* {
        for (const Tab& t : c.tabs)
            for (const Row& r : t.rows)
                if (r.id == id) return &r;
        return nullptr;
    };
    Settings s;
    CHECK(s.voices == 0);
    {
        const Content none = build_content(s, Env{true});   // nothing imported, no language chosen: no row
        CHECK(!find(none, kRowDjVoice));
    }
    Env env{true};
    env.languages = {{"GE", "Deutsch (German)"}};
    const Content c = build_content(s, env);
    const Row* r = find(c, kRowDjVoice);
    CHECK(r && r->label == "VOICES" && r->kind == RowKind::kChoice && r->values.size() == 2 && r->values[0] == "Language" &&
          r->values[1] == "English" && r->value == 0);
    CHECK(!find(c, kActionDjFirst + 1));   // the in-game page: no import rows
    Settings x;
    CHECK(apply_edit(x, Edit{kRowDjVoice, 1, 0, false}) && x.voices == 1);
    CHECK(from_ini(to_ini(x)).voices == 1 && from_ini(to_ini(Settings{})).voices == 0);
    CHECK(from_ini("voices = 7\n").voices != 7);
    env.language_import = true;
    env.dj_voices = {{"GE", "Deutsch (German)", 352722944, 0, -1}, {"IT", "Italiano (Italian)", 0, 330457088, -1}, {"SP", "Espa\xC3\xB1" "ol (Spanish)", 349601792, 0, 37}};
    env.dj_status = "Import cancelled: nothing was kept.";
    const Content l = build_content(s, env);
    const Row* ge = find(l, kActionDjFirst + 1);
    const Row* it = find(l, kActionDjFirst + 2);
    const Row* sp = find(l, kActionDjFirst + 3);
    CHECK(!find(l, kActionDjFirst));   // French: neither on the disc nor imported
    CHECK(ge && ge->label == "DJ VOICE: Deutsch (German)" && ge->values[0] == "Import (336 MB)" && !ge->disabled &&
          ge->description.find("Needs 336 MB") != std::string::npos && ge->description.find("nothing was kept") != std::string::npos);
    CHECK(it && it->values[0] == "Remove (315 MB)");
    CHECK(sp && sp->values[0] == "Cancel (37 %)");
    // the race training videos' rows: their own ids after the DJ's
    env.dj_voices.push_back({"GE", "Deutsch (German)", 135668524, 0, -1, true});
    const Content tv = build_content(s, env);
    const Row* tge = find(tv, kActionTrainingFirst + 1);
    CHECK(tge && tge->label == "TRAINING VIDEOS: Deutsch (German)" && tge->values[0] == "Import (129 MB)" &&
          tge->description.find("English pictures") != std::string::npos);
    CHECK(find(tv, kActionDjFirst + 1) && find(tv, kActionDjFirst + 1)->label == "DJ VOICE: Deutsch (German)");
    // translated: the labels' and values' patterns keep the names and numbers
    Content t = l;
    translate(t, "GE");
    const Row* gde = find(t, kActionDjFirst + 1);
    CHECK(gde && gde->label == "DJ-STIMME: Deutsch (German)" && gde->values[0] == "Importieren (336 MB)");
}

TEST(analog_triggers_row) {
    // 1.0.1 ANALOG TRIGGERS (INTERFACE, after CAR SERIES): on by default, a live toggle, kept in the ini
    Settings s;
    CHECK(s.analog_pedals);
    const Content c = build_content(s, Env{true});   // kept: `row` points into it
    const Row* row = nullptr;
    for (const Tab& t : c.tabs)
        for (const Row& r : t.rows)
            if (r.label == "ANALOG TRIGGERS") row = &r;
    CHECK(row && row->id == 272 && row->kind == RowKind::kToggle && row->value == 1);
    Settings x;
    CHECK(apply_edit(x, Edit{272, 0, 0, false}) && !x.analog_pedals);
    CHECK(apply_edit(x, Edit{272, 1, 0, false}) && x.analog_pedals);
    x.analog_pedals = false;
    CHECK(!from_ini(to_ini(x)).analog_pedals && from_ini(to_ini(Settings{})).analog_pedals);
}

// padpaint (review): a `{}` entry is looked up by its pattern and the argument put in after, whatever the argument says
TEST(translations_cover_the_save_notices) {
    // saveflow: the save bubble's lines (live.cpp show_save_bubble: save_store.h ProfileCard's texts, one per " - " part)
    // and the launcher's Set aside strings, in each language; the composed ones translate as a whole
    static const char* const kWords[] = {
        "Set aside file {}?", "The game can't play profile-{}.sav.", "Why: {}.",
        "It is renamed in the save folder (kept, not deleted) and the file starts empty: the game makes a new profile at its first save.",
        "Quit the game first if it is running.", "Set aside", "Set aside as {}.", "The game starts this file fresh.",
        "Cannot set it aside: {}", "Can't be played: damaged or unreadable", "Set aside file {} and start fresh...",
        "Renames profile-{}.sav in the save folder (kept, not deleted) so that the game starts this file empty.", "Asks first.",
        "SAVED", "SAVE FAILED", "YOUR LAST SAVE IS KEPT", "RETRYING", "SAVED AGAIN TO PROFILE {}", "NOT SAVED", "CAN'T READ {}",
        "PROGRESS WON'T BE SAVED THIS SESSION", "THE LAUNCHER'S SAVES PAGE CAN SET IT ASIDE", "{} CHANGED OUTSIDE THE GAME",
        "SAVED TO {}", "A SAVE WAS IMPORTED: RESTART THE GAME TO PLAY IT", "SAVE WARNING",
        "ANOTHER GAME ON THIS COMPUTER USES THIS SAVE FOLDER", "PLAY ONE AT A TIME",
        "Progress from {} was saved beside it, because the file changed while the game was running.", "Use it", "Leave it",
        "Makes it this file's save; the current one is kept as a backup (.bak and backups).",
        "Hides this line; the file stays in the save folder.", "Use the progress from {}?", "It replaces profile-{}.sav.",
        "The current one is kept as profile-{}.sav.bak and in backups.", "Now using the progress from {}.", "Cannot use it: {}",
    };
    for (const i18n::Table& t : i18n::kTables)
        for (const char* w : kWords) {
            bool found = false;
            for (std::size_t k = 0; k < t.count && !found; ++k) found = std::strcmp(t.entries[k].en, w) == 0 && t.entries[k].text[0] != 0;
            if (!found) std::printf("  %s lacks: %s\n", t.code, w);
            CHECK(found);
        }
    using b3settings::translate_text;
    CHECK(translate_text("SAVED", "FR") == "SAUVEGARD\xC3\x89");
    CHECK(translate_text("CAN'T READ PROFILE-1.SAV", "GE") == "PROFILE-1.SAV IST NICHT LESBAR");
    CHECK(translate_text("PROFILE-2.SAV CHANGED OUTSIDE THE GAME", "SW") == "PROFILE-2.SAV \xC3\x84NDRADES UTANF\xC3\x96R SPELET");
    CHECK(translate_text("SAVED TO PROFILE-1-UNSAVED-20261120-101500.SAV", "IT") == "SALVATO IN PROFILE-1-UNSAVED-20261120-101500.SAV");
    CHECK(translate_text("SAVED AGAIN TO PROFILE 2", "DU") == "WEER OPGESLAGEN IN PROFIEL 2");
    CHECK(translate_text("Set aside file 3?", "SP") == "\xC2\xBF" "Apartar el archivo 3?");
    const std::string modal = translate_text("The game can't play profile-1.sav. Why: the game's save is damaged (its checksum does not match). "
                                             "Quit the game first if it is running.", "FR");
    CHECK(modal == "Le jeu ne peut pas lire profile-1.sav. Raison : the game's save is damaged (its checksum does not match). "
                   "Quitter d'abord le jeu s'il est lanc\xC3\xA9.");
}

TEST(translate_format_pattern_then_argument) {
    using b3settings::translate_format;
    CHECK(translate_format("(NOT SAVED: {})", "disk full", "FR") == "(NON ENREGISTR\xC3\x89 : disk full)");
    // a reason with sentences, or reading as another entry's text, stays the argument
    CHECK(translate_format("(NOT SAVED: {})", "Off. On", "FR") == "(NON ENREGISTR\xC3\x89 : Off. On)");
    CHECK(translate_format("{} DEG", "5", "GE") == "5 GRAD" && translate_format("{} DEG", "0", "SW") == "0 GRADER");
    CHECK(translate_format("{} DEG", "5", "") == "5 DEG" && translate_format("(NOT SAVED: {})", "x", "") == "(NOT SAVED: x)");
    CHECK(translate_format("{} DEG", "5", "XX") == "5 DEG");   // no table: English
}

// ---- settingsui (2026-11-20): the page tidied up ----

// Every row, action and section id on a page is used once (the 280 / 290 / 350 collisions: an edit sent to the wrong
// row): the fullest game page (a card, a controller with its gyro, a status, a pack, music, mods, a host tab) and the
// launcher's
// portable mode (1.3.0): PORTABLE MODE on GAME > STARTING AND LOADING only when the host offers it (never in the launcher's
// copy of the game's tabs: its LAUNCHER tab has the row), its value the request when one is pending, the launcher's and
// the game's words, every reason translated; its edit is the host's (apply_edit leaves it)
TEST(portable_mode_row) {
    const auto find = [](const Content& c) -> const Row* {
        for (const Tab& t : c.tabs)
            for (const Row& r : t.rows)
                if (r.id == kRowPortable) return &r;
        return nullptr;
    };
    Settings s;
    Env e{true};
    CHECK(find(build_content(s, e)) == nullptr);   // not shown: no row
    e.portable.shown = true;
    const Content c = build_content(s, e);
    const Row* r = find(c);
    CHECK(r && r->kind == RowKind::kToggle && r->value == 0 && !r->disabled && r->label == "PORTABLE MODE");
    bool on_game_tab = false;
    for (const Tab& t : c.tabs)
        if (t.label == "GAME")
            for (const Row& row : t.rows) on_game_tab = on_game_tab || row.id == kRowPortable;
    CHECK(on_game_tab);
    e.portable.request = 1;   // asked of the launcher: shown on
    // macintel1: each page in a variable (find(build_content(...)) left r pointing into the freed temporary, so the
    // check read freed memory: it failed on macOS, whose allocator reuses it at once; AddressSanitizer:
    // heap-use-after-free)
    const Content asked = build_content(s, e);
    r = find(asked);
    CHECK(r && r->value == 1 && r->description.find("at its next start") != std::string::npos);
    e.portable.on = true;
    e.portable.request = 0;
    const Content on = build_content(s, e);
    r = find(on);
    CHECK(r && r->value == 0);
    e.launcher = true;   // the launcher's settings: its LAUNCHER tab has the row instead
    CHECK(find(build_content(s, e)) == nullptr);
    Env::Portable p;
    p.on = true;
    p.request = 0;
    const Row l = portable_row(p, true);
    CHECK(l.value == 1 && l.description.find("moves your data now") != std::string::npos);
    p.disabled = "The game is running: close it first.";
    CHECK(portable_row(p, true).disabled);
    CHECK(!apply_edit(s, Edit{kRowPortable, 1, 0, false}));
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"})   // the game's notices (save bubble segments)
        for (const char* w : {"THE LAUNCHER IS MOVING YOUR DATA", "QUIT AND START AGAIN WHEN IT IS DONE",
                              "THE PORTABLE DATA FOLDER CAN'T BE WRITTEN", "SAVES AND SETTINGS WON'T BE KEPT",
                              "Your data folder cannot be written (a write-protected drive?): settings and saves will not be kept."})
            CHECK(translate_text(w, code) != w);
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"})
        for (const char* why : {"The install folder cannot be written by you: move the game to a folder of your own to use portable mode.",
                                "The game is running: close it first.",
                                "A data folder was given when the launcher was started (--pref), so this does not apply.",
                                "B3PORT_USER_DIR is set to a data folder of your choice, so this does not apply.",
                                "The launcher's own folder is not known."})
            for (const bool launcher : {false, true}) {
                Env::Portable q;
                q.disabled = why;
                Content one;
                one.tabs.push_back(Tab{"GAME", {portable_row(q, launcher)}});
                const std::vector<std::string> missing = untranslated(one, code);
                for (const std::string& m : missing) std::printf("  %s lacks: %s\n", code, m.c_str());
                CHECK(missing.empty());
            }
}

// portable mode (1.3.0): HD pack and music folders inside the program's folder are written as "<install>/..." and read
// back against it (a USB stick on another drive letter); a folder outside stays absolute
TEST(folders_inside_the_install_kept_relative) {
    namespace fs = std::filesystem;
    const fs::path install = b3io::exe_dir();
    CHECK(!install.empty());
    if (install.empty()) return;
    Settings s;
    s.hd_pack_dir = b3io::u8string(install / "packs" / "SLUS-21050");
    s.music_folder = "/music/mine";
    const fs::path file = fs::path("build") / "test_settings.install_relative.ini";
    CHECK(save_file(file.string(), s));
    std::ifstream f(file);
    std::stringstream text;
    text << f.rdbuf();
    CHECK(text.str().find("hd_pack_dir = <install>/packs/SLUS-21050") != std::string::npos);
    CHECK(text.str().find("music_folder = /music/mine") != std::string::npos);
    Settings back;
    CHECK(load_file(file.string(), back));
    CHECK(back.hd_pack_dir == s.hd_pack_dir && back.music_folder == "/music/mine");
    Settings other = from_ini(text.str());   // the same file beside the program on another drive
    paths_from_install(other, "/run/media/other/B3");
    CHECK(fs::path(other.hd_pack_dir) == fs::path("/run/media/other/B3/packs/SLUS-21050") && other.music_folder == "/music/mine");
    Settings in = s;
    paths_to_install(in, install);
    CHECK(in.hd_pack_dir == "<install>/packs/SLUS-21050" && in.music_folder == "/music/mine");
    std::error_code ec;
    fs::remove(file, ec);
}

TEST(row_ids_unique_full_pages) {
    Env e{true};
    e.hd_pack_loaded = true;
    e.music = true;
    e.language_import = true;
    e.languages = {{"FR", "Francais"}};
    e.profile = 1;
    e.profile_used = {true, false, false, false, false};
    e.save_status = "EXPORTED";
    e.controls.on = true;
    e.controls.actions = {{"CROSS", "A", ""}, {"CIRCLE", "B", ""}, {"PHOTO: MOTION BLUR", "J", "", kActionCtlPhotoFirst},
                           {"TIME OF DAY PER RACE", "T", "", kActionCtlRaceTime}};   // photoblur's block (todrace: 413)
    e.controls.status = "PLAYER 1 CROSS: A";
    e.controls.gyro = true;
    e.controls.gyro_recentre_labels = {"NONE", "L3"};
    Row msec, mrow;
    msec.id = kSectionModBase;
    msec.kind = RowKind::kSection;
    msec.label = "A MOD";
    mrow.id = kModRowBase;
    mrow.kind = RowKind::kToggle;
    mrow.label = "A MOD ROW";
    e.mod_rows = {msec, mrow};
    Tab dev{"DEVELOPER", {}};
    Row tune;
    tune.id = kRowTuneFirst;
    tune.label = "A VALUE";
    dev.rows.push_back(tune);
    e.extra_tabs = {dev};
    Settings s;
    s.hd_pack_dir = "/hd";
    s.music_folder = "/music";
    s.car_paint = 1;
    s.p2_car_paint = 1;
    s.time_of_day = 1;
    for (std::size_t device : {0u, 1u})
        for (std::size_t player : {0u, 1u, 2u}) {
            e.controls.device = device;
            e.controls.player = player;
            const std::vector<std::uint32_t> dup = duplicate_ids(build_content(s, e));
            for (std::uint32_t id : dup) std::printf("  duplicate id %u (0x%X)\n", id, id);
            CHECK(dup.empty());
        }
    Env l;   // the launcher's
    l.launcher = true;
    l.hd_pack = true;
    l.language_import = true;
    {
        const std::vector<std::uint32_t> dup = duplicate_ids(build_content(s, l));
        for (std::uint32_t id : dup) std::printf("  launcher: duplicate id %u (0x%X)\n", id, id);
        CHECK(dup.empty());
    }
    // and the check finds one
    Content twice = build_content(s, l);
    twice.tabs[0].rows.push_back(twice.tabs[1].rows[0]);
    CHECK(duplicate_ids(twice).size() == 1);
}

// The tabs and their sections, in order (the game's page with a card; the launcher's: link rows in place of the
// controls, the music folder and the HD pack)
TEST(settingsui_tabs_and_sections) {
    Env e{true};
    e.music = true;
    e.profile = 1;
    e.profile_used = {true, false, false, false, false};
    e.controls.on = true;
    e.controls.device = 1;
    e.controls.gyro = true;
    e.controls.actions = {{"CROSS", "A", ""}};
    const Content c = build_content(Settings{}, e);
    std::vector<std::string> tabs;
    for (const Tab& t : c.tabs) tabs.push_back(t.label);
    CHECK((tabs == std::vector<std::string>{"DISPLAY", "GRAPHICS", "EFFECTS", "CAR", "CONTROLS", "AUDIO", "GAME", "SAVES", "MODS"}));
    const auto sections = [](const Tab& t) {
        std::vector<std::string> o;
        for (const Row& r : t.rows)
            if (r.kind == RowKind::kSection) o.push_back(r.label);
        return o;
    };
    CHECK((sections(c.tabs[0]) == std::vector<std::string>{"WINDOW", "LAYOUT", "CAMERA", "SMOOTHNESS"}));
    CHECK((sections(c.tabs[1]) == std::vector<std::string>{"IMAGE", "TEXTURES", "WORLD", "LIGHTING"}));
    CHECK(c.tabs[1].rows[0].label == "QUALITY PRESET");   // the first row of the tab it changes
    CHECK((sections(c.tabs[2]) == std::vector<std::string>{"POST", "CRASHES"}));
    CHECK((sections(c.tabs[3]) == std::vector<std::string>{"CAR PAINT", "PLAYER 2 CAR PAINT", "CAR LOOK", "GARAGE"}));
    CHECK((sections(c.tabs[4]) == std::vector<std::string>{"FEEDBACK", "DRIVING", "BINDINGS", "GYRO"}));
    CHECK((sections(c.tabs[5]) == std::vector<std::string>{"MUSIC"}));
    CHECK(c.tabs[5].rows[0].label == "VOLUMES AND DJ" && c.tabs[5].rows[0].disabled);
    CHECK((sections(c.tabs[6]) == std::vector<std::string>{"INTERFACE", "LANGUAGE AND UNITS", "MODES", "ACCESSIBILITY", "STARTING AND LOADING"}));
    CHECK((sections(c.tabs[8]) == std::vector<std::string>{"DEVELOPER"}));
    // each tab's first row is selectable, or a section heading followed by one
    for (const Tab& t : c.tabs) CHECK(!t.rows.empty() && (t.rows[0].kind != RowKind::kSection || t.rows.size() > 1));
    // the launcher's: no SAVES (its own page), the controls / music / HD pack rows open its pages
    Env l;
    l.launcher = true;
    const Content lc = build_content(Settings{}, l);
    tabs.clear();
    for (const Tab& t : lc.tabs) tabs.push_back(t.label);
    CHECK((tabs == std::vector<std::string>{"DISPLAY", "GRAPHICS", "EFFECTS", "CAR", "CONTROLS", "AUDIO", "GAME", "MODS"}));
    int links = 0;
    for (const Tab& t : lc.tabs)
        for (const Row& r : t.rows) {
            if (r.id == kActionOpenControllers) links += t.label == "CONTROLS" && !r.disabled ? 1 : 100;
            if (r.id == kActionOpenMusic) links += t.label == "AUDIO" && !r.disabled ? 1 : 100;
            if (r.id == kActionOpenHdPage) links += t.label == "GRAPHICS" && !r.disabled ? 1 : 100;
            CHECK(r.id != kActionChooseHdPack && r.id != kActionChooseMusic && r.id != kRowCtlPlayer);
            Settings x;
            if (r.kind == RowKind::kAction) CHECK(!apply_edit(x, Edit{r.id, 0, 0, true}));
        }
    CHECK(links == 3);
    // every word of both pages in every language (the new tabs, sections, rows and the prompt bar's words)
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"})
        for (const Content* x : {&c, &lc}) {
            Content y = *x;
            y.prompt_device = Content::PromptDevice::kMouse;
            for (const std::string& m : untranslated(y, code))
                if (m.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos && m != "A" && m != "MODS" &&
                    m != "Francais") {
                    std::printf("  %s lacks: %s\n", code, m.c_str());
                    CHECK(false);
                }
        }
}

// COLOUR / P2 COLOUR: the ready colours (each on the sliders' steps); Custom listed only while the colour is none of
// them; a choice sets the three values and turns the custom paint on
TEST(paint_preset_rows) {
    Settings s;   // the default paint (195, 30, 30) is no preset
    const Content c = build_content(s, Env{true});
    const Tab* car = holding(c, "COLOUR");
    CHECK(car && car->label == "CAR");
    if (!car) return;
    const std::size_t at = row_at(*car, "COLOUR");
    const Row& p = car->rows[at];
    CHECK(p.id == 292 && car->rows[at - 1].label == "CAR PAINT" && car->rows[at + 1].label == "PAINT RED");
    CHECK(p.values.size() == 11 && p.values.back() == "Custom" && p.value == 10 && !p.disabled && p.swatch == 0xC31E1E);
    Settings x = s;
    CHECK(apply_edit(x, Edit{292, 4, 0, false}) && x.car_paint == 1 && x.paint_red == 30 && x.paint_green == 90 && x.paint_blue == 225);
    const Content c2 = build_content(x, Env{true});
    const Tab* car2 = holding(c2, "COLOUR");
    const Row& p2 = car2->rows[row_at(*car2, "COLOUR")];
    CHECK(p2.values.size() == 10 && p2.value == 4 && p2.values[4] == "Blue");   // a preset: no Custom value
    CHECK(car2->rows[row_at(*car2, "PAINT BLUE")].level_text == "225" && !car2->rows[row_at(*car2, "PAINT BLUE")].disabled);
    CHECK(apply_edit(x, Edit{292, 10, 0, false}) && x.paint_blue == 225);   // Custom (out of the presets): as it is
    // player 2's
    Settings y;
    CHECK(apply_edit(y, Edit{293, 9, 0, false}) && y.p2_car_paint == 1 && y.p2_paint_red == 15 && y.car_paint == 0);
    // the values survive the ini (they are the RGB keys: no new key)
    CHECK(from_ini(to_ini(y)) == y && to_ini(y).find("preset = ") == std::string::npos);
}

// FRAME RATE LIMIT shows an ini value the page does not offer as itself (90 written by hand was shown as Unlimited)
TEST(frame_cap_value_of_its_own) {
    Settings s;
    s.frame_cap = 90;
    const Content c = build_content(s, Env{true});
    const Tab* d = holding(c, "FRAME RATE LIMIT");
    CHECK(d && d->label == "DISPLAY");
    if (!d) return;
    const Row& r = d->rows[row_at(*d, "FRAME RATE LIMIT")];
    CHECK(r.values.size() == 8 && r.values[r.value] == "90" && r.values[0] == "Unlimited" && r.values[2] == "60" && r.values[4] == "120");
    Settings x = s;
    CHECK(apply_edit(x, Edit{r.id, r.value, 0, false}) && x.frame_cap == 90);   // the same value: unchanged
    CHECK(apply_edit(x, Edit{r.id, r.value + 1, 0, false}) && x.frame_cap == 120);
    Settings y;   // the page's own values: as before
    const Content cy = build_content(y, Env{true});
    const Tab* dy = holding(cy, "FRAME RATE LIMIT");
    CHECK(dy->rows[row_at(*dy, "FRAME RATE LIMIT")].values.size() == 7);
}

// Left / right on an action row (a binding: remove / add) act once per press, never on the hold repeat
TEST(menu_action_rows_take_one_press_each) {
    Content c;
    Tab t{"T", {}};
    Row a;
    a.id = kActionCtlFirst;
    a.kind = RowKind::kAction;
    a.label = "CROSS";
    a.values = {"A"};
    t.rows.push_back(a);
    Row ch;
    ch.id = 7;
    ch.kind = RowKind::kChoice;
    ch.label = "CHOICE";
    ch.values = {"0", "1", "2", "3", "4", "5"};
    t.rows.push_back(ch);
    c.tabs.push_back(t);
    Menu m;
    m.open();
    m.update(c, Input{});
    Input left;
    left.left = true;
    left.seconds = 0.1f;
    int removes = 0;
    for (int k = 0; k < 40; ++k)   // four seconds held
        for (const Edit& e : m.update(c, left).edits) removes += e.id == kActionCtlFirst && e.direction == -1;
    CHECK(removes == 1);
    m.update(c, Input{});
    for (const Edit& e : m.update(c, left).edits) removes += e.id == kActionCtlFirst && e.direction == -1;
    CHECK(removes == 2);   // a new press: one more
    // a value row still repeats
    Input down;
    down.down = true;
    m.update(c, Input{});
    m.update(c, down);
    m.update(c, Input{});
    Input right;
    right.right = true;
    right.seconds = 0.1f;
    int steps = 0;
    for (int k = 0; k < 10; ++k) steps += int(m.update(c, right).edits.size());
    CHECK(steps >= 4);
}

// After a rebind took an input, that input (still held) does not act on the page until it is let go
TEST(menu_hold_until_released) {
    const Content c = build_content(Settings{}, Env{true});
    Menu m;
    m.open(1);
    m.update(c, Input{});
    Input held;
    held.confirm = true;
    held.right = true;
    held.down = true;
    m.hold_until_released();
    const std::size_t focus = m.focus();
    for (int k = 0; k < 30; ++k) CHECK(m.update(c, held).edits.empty());
    CHECK(m.focus() == focus);
    m.update(c, Input{});
    Input right;
    right.right = true;
    CHECK(m.update(c, right).edits.size() == 1);   // released, then pressed: acts
}

// A tab coming or going (SAVES, DEVELOPER) starts the per-tab memory again and keeps the page on the tab it showed
TEST(menu_tab_memory_follows_the_tab_list) {
    Env e{true};
    Content a = build_content(Settings{}, e);
    Menu m;
    m.open(6);   // GAME
    m.update(a, Input{});
    CHECK(a.tabs[m.tab()].label == "GAME");
    e.profile = 1;
    e.profile_used = {true, false, false, false, false};
    Tab dev{"DEVELOPER", {}};
    Row r;
    r.id = kRowTuneFirst;
    r.label = "X";
    dev.rows.push_back(r);
    e.extra_tabs = {dev};
    const Content b = build_content(Settings{}, e);
    m.update(b, Input{});
    CHECK(b.tabs[m.tab()].label == "GAME");
    Input next;
    next.next_tab = true;
    m.update(b, next);
    CHECK(b.tabs[m.tab()].label == "SAVES");
}

// The prompt bar: glyphs for the controller or keyboard (the binding rows' own words), words for the mouse
TEST(menu_prompt_bar) {
    Env e{true};
    e.controls.on = true;
    e.controls.actions = {{"CROSS", "A", ""}};
    Content c = build_content(Settings{}, e);
    Menu m;
    m.open(4);   // CONTROLS
    m.update(c, Input{});
    const auto images = [&](const std::vector<Item>& items) {
        std::vector<std::string> o;
        for (const Item& i : items)
            if (i.kind == ItemKind::kImage && i.text.rfind(kGlyphPrefix, 0) == 0) o.push_back(i.text.substr(std::strlen(kGlyphPrefix)));
        return o;
    };
    std::vector<Item> items = m.items("", "", nullptr);
    std::vector<std::string> g = images(items);
    // RUMBLE (a choice) focused: move, change, the sections (four on this tab), close
    CHECK((g == std::vector<std::string>{"dpad-ud", "dpad-lr", "section-up", "section-down", "triangle"}));
    CHECK(text_item(items, "CHANGE") && text_item(items, "SECTION") && text_item(items, "CLOSE AND SAVE"));
    // a binding row: rebind, add, remove, default
    Input down;
    down.down = true;
    for (int k = 0; k < 40 && c.tabs[4].rows[m.focus()].id != kActionCtlFirst; ++k) {
        m.update(c, down);
        m.update(c, Input{});
    }
    CHECK(c.tabs[4].rows[m.focus()].id == kActionCtlFirst);
    g = images(m.items("", "", nullptr));
    CHECK((g == std::vector<std::string>{"dpad-ud", "cross", "dpad-r", "dpad-l", "square", "triangle"}));
    // the mouse: words only
    c.prompt_device = Content::PromptDevice::kMouse;
    m.update(c, Input{});
    items = m.items("", "", nullptr);
    CHECK(images(items).empty() && text_item(items, "CLICK") && text_item(items, "RIGHT CLICK"));
    // German: the words translated, still inside the bar (75 % at the least, or a prompt left out)
    Content de = build_content(Settings{}, e);
    translate(de, "GE");
    m.update(de, Input{});
    items = m.items("", "", nullptr);
    CHECK(text_item(items, "SCHLIESSEN UND SPEICHERN") != nullptr);
    for (const Item& i : items)
        if (i.y >= 400 && i.y < 430 && (i.kind == ItemKind::kText || i.kind == ItemKind::kImage)) CHECK(i.x >= 32 && i.x + i.w <= 608.5f);
    // every language: when the bar is too narrow the least needed prompts go first (the section jump before CHANGE); a
    // binding row keeps REMOVE and DEFAULT, and CLOSE stays
    for (const char* code : {"FR", "GE", "IT", "SP", "DU", "SW"}) {
        Content t = build_content(Settings{}, e);
        translate(t, code);
        Menu tm;
        tm.open(4);
        tm.update(t, Input{});
        g = images(tm.items("", "", nullptr));
        const auto has = [&](const char* n) { return std::find(g.begin(), g.end(), n) != g.end(); };
        CHECK(has("triangle") && (has("section-up") ? has("dpad-lr") && has("dpad-ud") : true));
        for (int k = 0; k < 40 && t.tabs[4].rows[tm.focus()].id != kActionCtlFirst; ++k) {
            tm.update(t, down);
            tm.update(t, Input{});
        }
        g = images(tm.items("", "", nullptr));
        if (!(has("dpad-l") && has("square") && has("triangle"))) std::printf("  %s: the binding row's bar lost REMOVE or DEFAULT\n", code);
        CHECK(has("dpad-l") && has("square") && has("triangle"));
        for (const Item& i : tm.items("", "", nullptr))
            if (i.y >= 400 && i.y < 430 && (i.kind == ItemKind::kText || i.kind == ItemKind::kImage)) CHECK(i.x >= 32 && i.x + i.w <= 608.5f);
    }
}

// settingsui: each tab as wide as its label (the measure's) needs: wider labels get wider tabs, the bar is filled, the
// pointer hits the tab drawn there, and the labels keep their full size while they fit
TEST(menu_tabs_sized_to_their_labels) {
    Env e{true};
    e.music = true;
    e.profile = 1;
    Content c = build_content(Settings{}, e);
    translate(c, "GE");
    Menu m;
    m.open(0);
    m.update(c, Input{});
    const Measure measure = [](std::string_view s, float h, std::uint8_t) { return float(s.size()) * h * 0.45f; };
    std::vector<const Item*> labels;
    const std::vector<Item> items = m.items("", "", measure);
    for (const Item& i : items)
        if (i.kind == ItemKind::kText && i.y < 80 && i.y > 50)
            for (const Tab& t : c.tabs)
                if (i.text == t.label) labels.push_back(&i);
    CHECK(labels.size() == c.tabs.size());
    if (labels.size() != c.tabs.size()) return;
    float left = 1e9f, right = 0;
    for (std::size_t k = 0; k < labels.size(); ++k) {
        left = std::min(left, labels[k]->x);
        right = std::max(right, labels[k]->x + labels[k]->w);
        if (k) CHECK(labels[k]->x > labels[k - 1]->x);
        CHECK(labels[k]->h >= (24.0f - 9.0f) * 0.75f - 0.01f);   // no label below the shrink floor
    }
    CHECK(left < 100 && right > 540);   // the bar filled (between the L1 / R1 prompts)
    // the longest label's tab is the widest
    std::size_t longest = 0, widest = 0;
    for (std::size_t k = 0; k < labels.size(); ++k) {
        if (c.tabs[k].label.size() > c.tabs[longest].label.size()) longest = k;
        if (labels[k]->w > labels[widest]->w) widest = k;
    }
    CHECK(longest == widest);
    // a click in the middle of the last tab's label picks it
    Input click;
    click.pointer = std::array<float, 2>{labels.back()->x + labels.back()->w * 0.5f, labels.back()->y + labels.back()->h * 0.5f};
    click.pointer_moved = true;
    click.click = true;
    m.update(c, click);
    CHECK(m.tab() == c.tabs.size() - 1);
}

// settingsui: the player's own names are not translated: the HD pack's and the music folder's names on the page
TEST(folder_names_not_translated) {
    Env e{true};
    e.music = true;
    Settings s;
    s.hd_pack_dir = "/packs/Paint Pack";   // "Paint {}" is a table pattern
    s.music_folder = "/music/gold";        // "gold" a table word
    Content c = build_content(s, e);
    translate(c, "GE");
    bool hd = false, music = false;
    for (const Tab& t : c.tabs)
        for (const Row& r : t.rows) {
            if (r.id == kActionChooseHdPack) hd = r.values.size() == 1 && r.values[0] == "Paint Pack";
            if (r.id == kActionChooseMusic) music = r.values.size() == 1 && r.values[0] == "gold";
        }
    CHECK(hd && music);
    CHECK(translate_text("gold", "GE") == "Gold" && translate_text("Paint Pack", "GE") != "Paint Pack");   // (what they would have read)
}

// The launcher and the game both write pc_settings.ini: each writes only the keys it changed
TEST(ini_merge_keeps_the_other_programs_changes) {
    Settings base;
    Settings mine = base, disk = base;
    mine.vsync = !base.vsync;          // this program's change
    mine.music_folder = "/mine";
    disk.fov = 100;                    // the other program's
    disk.language = "GE";
    disk.vsync = base.vsync;           // (it did not touch vsync)
    const Settings m = merge_changes(base, mine, disk);
    CHECK(m.vsync == mine.vsync && m.music_folder == "/mine" && m.fov == 100 && m.language == "GE");
    // both changed one key: this program's wins (it writes last)
    disk.vsync = mine.vsync;
    mine.fov = 110;
    CHECK(merge_changes(base, mine, disk).fov == 110);
    CHECK(merge_changes(base, base, disk) == disk);   // nothing changed here: the file as it is
}

// merge_changes covers every field: Settings has as many fields as the ini has keys (a field the ini does not hold
// needs adding to both), and each key, changed by the other program only, is taken from the file, and changed here
// only, kept. A new field outside the key table and not handled in merge_changes fails here.
namespace {
struct AnyField { template <class U> constexpr operator U() const; };   // (unevaluated only)
template <class T, class... A> constexpr std::size_t field_count() {
    if constexpr (requires { T{A{}..., AnyField{}}; }) return field_count<T, A..., AnyField>();
    else return sizeof...(A);
}
}  // namespace

TEST(ini_merge_covers_every_field) {
    const Settings base;
    std::vector<std::string> keys;
    {
        const std::string ini = to_ini(base);
        std::size_t at = 0;
        while (at < ini.size()) {
            std::size_t nl = ini.find('\n', at);
            if (nl == std::string::npos) nl = ini.size();
            const std::string line = ini.substr(at, nl - at);
            at = nl + 1;
            const std::size_t eq = line.find(" = ");
            if (line.empty() || line[0] == '#' || eq == std::string::npos || line.compare(0, eq, "version") == 0) continue;
            keys.push_back(line.substr(0, eq));
        }
    }
    constexpr std::size_t fields = field_count<Settings>();
    if (fields != keys.size()) std::printf("  Settings has %zu fields, the ini %zu keys\n", fields, keys.size());
    CHECK(fields == keys.size());
    const char* const tries[] = {"true", "false", "0", "1", "2", "3", "4", "5", "8", "-1", "50", "60", "90", "100", "110", "120",
                                 "125", "150", "200", "nearest", "bilinear", "trilinear", "anisotropic", "/elsewhere", "GE"};
    for (const std::string& k : keys) {
        Settings disk = base;
        for (const char* v : tries) {
            disk = from_ini(k + " = " + v + "\n");
            if (!(disk == base)) break;
        }
        if (disk == base) { std::printf("  %s: no value tried changes it\n", k.c_str()); CHECK(false); continue; }
        const bool taken = merge_changes(base, base, disk) == disk;   // the other program's change
        const bool kept = merge_changes(base, disk, base) == disk;    // this program's
        if (!taken || !kept) std::printf("  %s: merge_changes %s\n", k.c_str(), taken ? "drops this program's change" : "ignores the file's change");
        CHECK(taken && kept);
    }
}

TEST_MAIN("b3settings (settings1)")

// photoblur: a CONTROLS action row with an id of its own (the photo shortcuts' block 410-419) keeps it; the rows without
// one stay positional from kActionCtlFirst, the explicit rows not counted
TEST(controls_action_rows_with_their_own_ids) {
    Env env{true};
    env.controls.on = true;
    env.controls.player = 2;
    env.controls.actions = {{"A", "1", "", 0}, {"PHOTO: MOTION BLUR", "J", "", kActionCtlPhotoFirst}, {"B", "2", "", 0},
                            {"PHOTO: SLOWER SHUTTER", "L", "", kActionCtlPhotoFirst + 2}};
    const Content c = build_content(Settings{}, env);
    std::map<std::string, std::uint32_t> id;
    for (const Tab& t : c.tabs)
        for (const Row& r : t.rows) id[r.label] = r.id;
    CHECK(id["A"] == kActionCtlFirst && id["B"] == kActionCtlFirst + 1);
    CHECK(id["PHOTO: MOTION BLUR"] == kActionCtlPhotoFirst && id["PHOTO: SLOWER SHUTTER"] == kActionCtlPhotoFirst + 2);
    CHECK(is_ctl_action(kActionCtlPhotoFirst + 9) && !is_ctl_action(kActionCtlPhotoLast + 1) && !is_ctl_action(kActionCtlResetAll));
}
