// browse.h -- the tuning console's browser (tunenav1, docs/modding/tuning.md "Finding a value"): the ~1,100 variables
// a race registers placed in category tabs, sections and subsections, the player's-car page, the most-used page
// (favourites, recently changed, a starter list), the CHANGED page (tunenav2: every value that differs from its default
// now, and the car page's toggles), search, and the navigation both views share (the F9 overlay and PC
// SETTINGS > DEVELOPER: src/b3replay/tuneconsole.cpp). Header only, standard library only (and the C ABI's B3TuneVar):
// the renderer and the tests (tests/test_tune_browse.cpp) use it. Presentation only: nothing here writes a value.
//
// The place of a variable comes from what the registry gives: its group, name and config file (the game's own
// strings), by the first row of rules() that matches. Each row is patterns (case-insensitive, `*` any run of
// characters) on the group, the name and the file, the category, and the section / subsection titles: `{1}` is what
// the group's first `*` matched, `{file}` the config file's stem (COMPCAR1 of .../VehiclePhysics/COMPCAR1.cfg),
// `{group}` the whole group. A row marked per_vehicle is a per-car physics value: a traffic model's (its config file
// has no "Physics/Race Car/..." variable) goes to TRAFFIC instead. A variable no row matches goes to OTHER under its own
// group. A section with one subsection shows its variables directly.
//
// The tree is built when the set of live variables changes (Browser::set_vars), never per frame; a page's rows are a
// few dozen at most, so neither view needs to scroll through the whole registry.
#ifndef B3TUNE_BROWSE_H
#define B3TUNE_BROWSE_H

#include "b3tune/tune.h"
#include "b3tune/tune_abi.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace b3tune::browse {

// ---- categories (the tabs, in order) ----
enum class Cat : std::uint8_t { kCar, kChanged, kMostUsed, kHandling, kEngine, kBoost, kCrash, kAI, kTraffic, kCamera, kAudio, kSurfaces, kVisuals,
                                kRumble, kOther, kCount };
constexpr std::size_t kCats = std::size_t(Cat::kCount);
struct CatInfo {
    const char* tab;     // the tab's title
    const char* about;   // one line for the help text
};
inline const CatInfo& cat_info(Cat c) {
    static const CatInfo t[kCats] = {
        {"YOUR CAR", "The car you are driving now: speed, boost, health, quick actions, and its model's main tuning."},
        {"CHANGED", "Every value that differs from its default now, with the default: ENTER goes to one, R puts it back."},
        {"MOST USED", "Your favourites, the values you changed last, and a short list of the most interesting ones."},
        {"HANDLING", "Steering, drift, suspension, body roll, the differential, mass, downforce: per car model."},
        {"ENGINE & SPEED", "Top speed in boost, acceleration, braking, drag, the engine, the gears, the boost kick: per car model."},
        {"BOOST & SCORE", "The boost bar, the boost each move earns, burnout points and the score rules."},
        {"DAMAGE & CRASHES", "How cars react to hits, wall hits, the crash action (air ram) forces."},
        {"AI RIVALS", "How the rival racers drive: avoidance, targets, aggression, slams."},
        {"TRAFFIC", "The traffic models' mass and suspension, and the traffic's sounds (passing, Doppler, crashing, the L train)."},
        {"CAMERA", "The race cameras per car, the crash camera, the camera shake effects."},
        {"AUDIO", "Skids, engines, boost, props hit, the HUD's sounds and the mix."},
        {"SURFACES", "The sound of each road surface (tarmac, gravel, snow ...) at speed."},
        {"VISUALS", "Bloom per route, the cars' paint and glass."},
        {"RUMBLE", "Controller rumble: surfaces, kerbs, boost, collisions, crashes."},
        {"OTHER", "Variables no rule places, by their own group."},
    };
    return t[std::size_t(c) < kCats ? std::size_t(c) : kCats - 1];
}

// ---- the mapping (data: adjust here) ----
struct Rule {
    const char* group;      // pattern on the group
    const char* name;       // pattern on the name ("*": any)
    const char* file;       // pattern on the config file ("*": any)
    Cat cat;
    const char* section;    // the section's title (templates: {1} {file} {group}); empty after expansion: "GENERAL"
    const char* sub;        // the subsection's title ("": none)
    bool per_vehicle;       // a traffic model's variable goes to TRAFFIC
};
inline const std::vector<Rule>& rules() {
    static const std::vector<Rule> r = {
        // handling and speed: VehiclePhysics/<car>.cfg (Register__24CB3RaceCarPhysicsAttribs / CB3VehiclePhysicsAttribs)
        {"Physics/Race Car/Misc", "Max Speed*", "*", Cat::kEngine, "Speed & braking", "{file}", true},
        {"Physics/Race Car/Misc", "Accel*", "*", Cat::kEngine, "Speed & braking", "{file}", true},
        {"Physics/Race Car/Misc", "*Braking*", "*", Cat::kEngine, "Speed & braking", "{file}", true},
        {"Physics/Race Car/Misc", "Drag*", "*", Cat::kEngine, "Speed & braking", "{file}", true},
        {"Physics/Race Car/Misc", "Wall*", "*", Cat::kCrash, "Wall hits", "{file}", true},
        {"Physics/Race Car/Misc", "*", "*", Cat::kHandling, "Downforce & in the air", "{file}", true},
        {"Physics/Transmission/Engine", "*", "*", Cat::kEngine, "Engine", "{file}", true},
        {"Physics/Transmission/Gear Ratios", "*", "*", Cat::kEngine, "Gear ratios", "{file}", true},
        {"Physics/Transmission/Boost Kick", "*", "*", Cat::kEngine, "Boost kick", "{file}", true},
        {"Physics/Transmission*", "*", "*", Cat::kEngine, "{group}", "{file}", true},
        {"Physics/Steering", "*", "*", Cat::kHandling, "Steering", "{file}", true},
        {"Physics/Drift", "*", "*", Cat::kHandling, "Drift", "{file}", true},
        {"Physics/Suspension/*", "*", "*", Cat::kHandling, "Suspension {1}", "{file}", true},
        {"Physics/Race Car/Body Roll", "*", "*", Cat::kHandling, "Body roll", "{file}", true},
        {"Physics/Race Car/LSDM", "*", "*", Cat::kHandling, "Differential (LSD)", "{file}", true},
        {"Physics/Vehicle", "*", "*", Cat::kHandling, "Mass", "{file}", true},
        {"Physics/Aggressive Driving Reaction", "*", "*", Cat::kCrash, "Reaction to being hit", "{file}", true},
        {"Physics*", "*", "*", Cat::kHandling, "{group}", "{file}", true},
        // boost and score: Score.cfg (CB3Burn, CB3Score)
        {"Boost Bar", "*", "*", Cat::kBoost, "Boost bar", "", false},
        {"Score/Boost/*", "*", "*", Cat::kBoost, "Boost earned", "{1}", false},
        {"Score/Burnout Points", "*Category BP", "*", Cat::kBoost, "Burnout points", "By category", false},
        {"Score/Burnout Points", "*Takedown*", "*", Cat::kBoost, "Burnout points", "Takedowns & slams", false},
        {"Score/Burnout Points", "*Slam*", "*", Cat::kBoost, "Burnout points", "Takedowns & slams", false},
        {"Score/Burnout Points", "*Revenge*", "*", Cat::kBoost, "Burnout points", "Takedowns & slams", false},
        {"Score/Burnout Points", "Psyche*", "*", Cat::kBoost, "Burnout points", "Takedowns & slams", false},
        {"Score/Burnout Points", "No Mercy*", "*", Cat::kBoost, "Burnout points", "Takedowns & slams", false},
        {"Score/Burnout Points", "Denied*", "*", Cat::kBoost, "Burnout points", "Takedowns & slams", false},
        {"Score/Burnout Points", "*", "*", Cat::kBoost, "Burnout points", "Driving", false},
        {"Score/*", "*", "*", Cat::kBoost, "{1}", "", false},
        // crashes: Physics/cad.cfg (CB3CrashActionDirector)
        {"CAD Properties", "*", "*", Cat::kCrash, "Crash action (air ram)", "", false},
        // AI: AI/defaults.cfg (CB3AIAttribs)
        {"AI/Aggressive Driving*", "*", "*", Cat::kAI, "Aggressive driving", "{1}", false},
        {"AI/*", "*", "*", Cat::kAI, "{1}", "", false},
        // cameras: Camera/Camera.cfg, Camera/<car>.cfg (CB3CameraAttribs, CB3CameraAttribsCar)
        {"Camera/*", "*", "*", Cat::kCamera, "{1}", "{file}", false},
        {"Crash/HandyCam*", "*", "*", Cat::kCamera, "Crash camera", "", false},
        {"InGame/Effect/*", "*", "*", Cat::kCamera, "Shake effects", "{1}", false},
        // surfaces: the surface groups of Sound/Surface.cfg (CB3SoundRaceCarManager)
        {"*", "*", "*Sound/Surface.cfg", Cat::kSurfaces, "{group}", "", false},
        // audio (CB3Sound*): Sound/*.cfg, VehicleAudio/<car>.cfg, VehicleAudioAI/<car>.cfg
        {"Sound/Skids*", "*", "*", Cat::kAudio, "Skids", "{1}", false},
        {"Sound/Prop/*", "*", "*", Cat::kAudio, "Props hit", "{1}", false},
        {"Sound/Traffic*", "*", "*", Cat::kTraffic, "Traffic sounds", "General", false},
        {"Sound/Doppler*", "*", "*", Cat::kTraffic, "Traffic sounds", "Doppler", false},
        {"Sound/CrashingTraffic/*", "*", "*", Cat::kTraffic, "Traffic sounds", "Crashing {1}", false},
        {"Sound/L Train*", "*", "*", Cat::kTraffic, "Traffic sounds", "L train", false},
        {"Sound/Hud*", "*", "*", Cat::kAudio, "HUD", "{1}", false},
        {"Sound/Boost*", "*", "*", Cat::kAudio, "Boost", "", false},
        {"Sound/Esm*", "*", "*", Cat::kAudio, "Engine sound model", "", false},
        {"Sound/Misc*", "*", "*", Cat::kAudio, "Mix", "{1}", false},
        {"VehicleAudioAI*", "*", "*", Cat::kAudio, "Rivals' engines", "{file}", false},
        {"VehicleAudio*", "*", "*", Cat::kAudio, "Your car's engine", "{file}", false},
        {"Sound/*", "*", "*", Cat::kAudio, "{1}", "", false},
        // rumble: Input/rumble.cfg
        {"Rumble/Car/*", "*", "*", Cat::kRumble, "Car", "{1}", false},
        {"Rumble/Collision/*", "*", "*", Cat::kRumble, "Collisions", "{1}", false},
        {"Rumble/*", "*", "*", Cat::kRumble, "{1}", "", false},
        // visuals: TrackBloom/<route>.cfg (CB3StaticTrack), VehicleMaterial/<car>.cfg (CB3RaceCar::Register)
        {"Bloom*", "*", "*", Cat::kVisuals, "Bloom", "{file}", false},
        {"Material Properties", "*", "*", Cat::kVisuals, "Car paint & glass", "{file}", false},
    };
    return r;
}

// ---- small text helpers ----
inline char lower(char c) { return char(std::tolower(static_cast<unsigned char>(c))); }
inline std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
// `pat` against `s`, case-insensitive; `*` matches any run (also empty), the first `*` as little as it can. *first:
// what the first `*` matched.
inline bool glob_at(std::string_view pat, std::string_view s, std::size_t p, std::size_t i, bool first_star, std::string* first) {
    while (p < pat.size()) {
        if (pat[p] == '*') {
            for (std::size_t k = i; k <= s.size(); ++k)
                if (glob_at(pat, s, p + 1, k, false, nullptr)) {
                    if (first_star && first) *first = std::string(s.substr(i, k - i));
                    return true;
                }
            return false;
        }
        if (i >= s.size() || lower(pat[p]) != lower(s[i])) return false;
        ++p;
        ++i;
    }
    return i == s.size();
}
inline bool glob(std::string_view pat, std::string_view s, std::string* first = nullptr) {
    if (first) first->clear();
    return glob_at(pat, s, 0, 0, true, first);
}
// "../Export/ValueDB/VehiclePhysics/COMPCAR1.cfg" -> "COMPCAR1" (".cfg" alone, or no file: "")
inline std::string file_stem(std::string_view file) {
    const std::size_t slash = file.find_last_of("/\\");
    std::string_view b = slash == std::string_view::npos ? file : file.substr(slash + 1);
    if (const std::size_t dot = b.rfind('.'); dot != std::string_view::npos) b = b.substr(0, dot);
    return std::string(b);
}
// a group's piece as a title: '/' at the ends dropped, inner '/' as " / ", spaces trimmed
inline std::string tidy(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '/') out += " / ";
        else out += c;
    }
    std::size_t b = 0, e = out.size();
    auto junk = [](char c) { return c == ' ' || c == '/'; };
    while (b < e && junk(out[b])) ++b;
    while (e > b && junk(out[e - 1])) --e;
    out = out.substr(b, e - b);
    std::string sq;   // no double spaces
    for (char c : out)
        if (!(c == ' ' && !sq.empty() && sq.back() == ' ')) sq += c;
    return sq;
}
inline std::string expand(std::string_view t, std::string_view cap, std::string_view group, std::string_view file) {
    std::string out;
    for (std::size_t i = 0; i < t.size();) {
        if (t.substr(i, 3) == "{1}") { out += tidy(cap); i += 3; }
        else if (t.substr(i, 6) == "{file}") { const std::string st = file_stem(file); out += st.empty() ? "default" : st; i += 6; }
        else if (t.substr(i, 7) == "{group}") { out += tidy(group); i += 7; }
        else out += t[i++];
    }
    return tidy(out);
}

// ---- the place of one variable ----
struct Place {
    Cat cat = Cat::kOther;
    std::string section, sub;
    int rule = -1;   // the row of rules() (-1: none, OTHER)
};
// race_car_file: the variable's config file has "Physics/Race Car/..." variables (a racing car's, not a traffic model's)
inline Place place(std::string_view group, std::string_view name, std::string_view file, bool race_car_file) {
    const std::vector<Rule>& rs = rules();
    for (std::size_t k = 0; k < rs.size(); ++k) {
        const Rule& r = rs[k];
        std::string cap;
        if (!glob(r.group, group, &cap) || !glob(r.name, name) || !glob(r.file, file)) continue;
        Place p;
        p.rule = int(k);
        p.cat = r.per_vehicle && !race_car_file ? Cat::kTraffic : r.cat;
        p.section = expand(r.section, cap, group, file);
        if (p.section.empty()) p.section = "General";
        p.sub = expand(r.sub, cap, group, file);
        return p;
    }
    Place p;
    p.section = tidy(group).empty() ? "No group" : tidy(group);
    return p;
}

// ---- the tree ----
struct Item {
    std::uint32_t var = 0;       // the registry index (B3TuneVar::index, B3TuneHooks::get's i)
    std::uint32_t element = 0;
};
struct Node {
    std::string label;
    int parent = -1;
    Cat cat = Cat::kOther;
    int rule = -1;                    // the first rule that made it (section order)
    std::vector<int> kids;
    std::vector<Item> items;
    std::size_t count = 0;            // elements in and below it
};
// what the tree needs of a variable (B3TuneVar's strings and shape)
struct VarInfo {
    std::uint32_t index = 0, elements = 1, type = 2, count = 0;
    std::int32_t key = 0;
    std::string group, name, file;
};
inline VarInfo info_of(const B3TuneVar& v) {
    VarInfo i;
    i.index = v.index;
    i.elements = std::max<std::uint32_t>(1, std::min<std::uint32_t>(v.elements, B3TUNE_MAX_WORDS));
    i.type = v.type;
    i.count = v.count;
    i.key = v.key;
    i.group = v.group ? v.group : "";
    i.name = v.name ? v.name : "";
    i.file = v.file ? v.file : "";
    return i;
}
inline std::string element_label(const VarInfo& v, std::uint32_t el) {
    std::string n = v.name;
    if (v.type == 5) n += el == 0 ? " X" : el == 1 ? " Y" : " Z";
    else if (v.count >= 2) n += " [" + std::to_string(el) + "]";
    return n;
}
// the player's car model: the stem of the first player camera's config file (Camera/<car>.cfg registers per player:
// RegisterVariables__19CB3CameraAttribsCar), else of the player engine sound's (VehicleAudio/<car>.cfg)
inline std::string player_stem(const std::vector<VarInfo>& vars) {
    for (const char* dir : {"/Camera/", "/VehicleAudio/"})
        for (const VarInfo& v : vars) {
            std::string f = v.file;
            for (char& c : f) c = lower(c);
            std::string d = dir;
            for (char& c : d) c = lower(c);
            if (f.find(d) == std::string::npos) continue;
            const std::string st = file_stem(v.file);
            if (!st.empty() && upper(st) != "CAMERA") return st;   // (Camera/Camera.cfg is the shared one)
        }
    return {};
}

class Tree {
public:
    void build(const std::vector<VarInfo>& vars) {
        nodes_.clear();
        where_.clear();
        vars_ = vars;
        by_index_.clear();
        for (std::size_t i = 0; i < vars_.size(); ++i) by_index_[vars_[i].index] = i;
        player_ = player_stem(vars_);
        std::set<std::string> race_files;
        for (const VarInfo& v : vars_)
            if (glob("Physics/Race Car/*", v.group)) race_files.insert(upper(v.file));
        for (std::size_t c = 0; c < kCats; ++c) {
            Node n;
            n.label = cat_info(Cat(c)).tab;
            n.cat = Cat(c);
            nodes_.push_back(n);
        }
        std::map<std::pair<int, std::string>, int> child;   // (parent, label) -> node
        auto kid = [&](int parent, const std::string& label, int rule) {
            const auto key = std::make_pair(parent, upper(label));
            if (auto it = child.find(key); it != child.end()) {
                Node& k = nodes_[std::size_t(it->second)];
                if (rule >= 0 && (k.rule < 0 || rule < k.rule)) k.rule = rule;
                return it->second;
            }
            Node n;
            n.label = label;
            n.parent = parent;
            n.cat = nodes_[std::size_t(parent)].cat;
            n.rule = rule;
            nodes_.push_back(n);
            const int id = int(nodes_.size() - 1);
            nodes_[std::size_t(parent)].kids.push_back(id);
            child[key] = id;
            return id;
        };
        for (const VarInfo& v : vars_) {
            const Place p = place(v.group, v.name, v.file, race_files.count(upper(v.file)) != 0);
            int n = kid(int(p.cat), p.section, p.rule);
            if (!p.sub.empty()) {
                std::string sub = p.sub;
                if (!player_.empty() && upper(sub) == upper(player_)) sub += " (your car)";
                n = kid(n, sub, p.rule);
            }
            for (std::uint32_t el = 0; el < v.elements; ++el) nodes_[std::size_t(n)].items.push_back({v.index, el});
        }
        // sections by their rule's order (then first seen); the player's car first among the subsections
        for (std::size_t c = 0; c < kCats; ++c) {
            std::vector<int>& ks = nodes_[c].kids;
            std::stable_sort(ks.begin(), ks.end(), [&](int a, int b) {
                const int ra = nodes_[std::size_t(a)].rule, rb = nodes_[std::size_t(b)].rule;
                return (ra < 0 ? 1 << 20 : ra) < (rb < 0 ? 1 << 20 : rb);
            });
            for (int s : ks) {
                std::vector<int>& subs = nodes_[std::size_t(s)].kids;
                std::stable_partition(subs.begin(), subs.end(), [&](int x) {
                    return nodes_[std::size_t(x)].label.find(" (your car)") != std::string::npos;
                });
                if (subs.size() == 1 && nodes_[std::size_t(s)].items.empty()) {   // one subsection: its items directly
                    Node& only = nodes_[std::size_t(subs[0])];
                    nodes_[std::size_t(s)].items = only.items;
                    nodes_[std::size_t(s)].label += only.label == "default" ? "" : " - " + only.label;
                    only.items.clear();
                    only.parent = -2;   // dropped
                    subs.clear();
                }
            }
        }
        // counts and the place of each item
        for (std::size_t i = nodes_.size(); i-- > 0;) {
            Node& n = nodes_[i];
            if (n.parent == -2) continue;
            n.count += n.items.size();
            for (std::size_t k = 0; k < n.items.size(); ++k) where_[{n.items[k].var, n.items[k].element}] = {int(i), k};
            if (n.parent >= 0) nodes_[std::size_t(n.parent)].count += n.count;
        }
    }
    const Node& node(int i) const { return nodes_.at(std::size_t(i)); }
    std::size_t size() const { return nodes_.size(); }
    int root(Cat c) const { return int(c); }
    // where an element lives: (node, its row among the node's items); node -1: not in the tree (not live)
    std::pair<int, std::size_t> where(std::uint32_t var, std::uint32_t element) const {
        const auto it = where_.find({var, element});
        return it == where_.end() ? std::make_pair(-1, std::size_t(0)) : it->second;
    }
    std::vector<int> path(int n) const {   // the category's node first
        std::vector<int> p;
        for (; n >= 0; n = nodes_[std::size_t(n)].parent) p.insert(p.begin(), n);
        return p;
    }
    std::string path_text(int n) const {
        std::string t;
        for (int k : path(n)) t += (t.empty() ? "" : " > ") + upper(nodes_[std::size_t(k)].label);
        return t;
    }
    const VarInfo* var(std::uint32_t index) const {
        const auto it = by_index_.find(index);
        return it == by_index_.end() ? nullptr : &vars_[it->second];
    }
    const std::vector<VarInfo>& vars() const { return vars_; }
    const std::string& player() const { return player_; }
    // every element in the tabs' order (search results, tests)
    std::vector<std::pair<int, Item>> all() const {
        std::vector<std::pair<int, Item>> out;
        std::vector<int> st;
        for (std::size_t c = kCats; c-- > 0;) st.push_back(int(c));
        while (!st.empty()) {
            const int n = st.back();
            st.pop_back();
            const Node& nd = nodes_[std::size_t(n)];
            for (const Item& it : nd.items) out.push_back({n, it});
            for (std::size_t k = nd.kids.size(); k-- > 0;) st.push_back(nd.kids[k]);
        }
        return out;
    }
    // a variable by group, name and (optionally) config file stem; -1 none
    long find(std::string_view group, std::string_view name, std::string_view stem = {}) const {
        for (const VarInfo& v : vars_)
            if (upper(v.group) == upper(std::string(group)) && upper(v.name) == upper(std::string(name)) &&
                (stem.empty() || upper(file_stem(v.file)) == upper(std::string(stem))))
                return long(v.index);
        return -1;
    }

private:
    std::vector<Node> nodes_;
    std::vector<VarInfo> vars_;
    std::map<std::uint32_t, std::size_t> by_index_;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<int, std::size_t>> where_;
    std::string player_;
};

// ---- the player's car (the YOUR CAR page) ----
// Its own fields (b3tune::car) and readouts; the page's rows of the model's tuning are variables (car_tuning()).
enum : int { kCarSpeed = 100, kCarBoostMax = 101 };   // readouts only (the editable ones are b3tune::car fields)
struct CarRow {
    int field;
    const char* label;
    const char* help;
};
inline const std::vector<CarRow>& car_rows() {
    static const std::vector<CarRow> r = {
        {kCarSpeed, "Speed", "The speedometer's value now (read only)."},
        {int(car::kBoost), "Boost", "The boost in the bar now, in boost units: LEFT / RIGHT change it."},
        {kCarBoostMax, "Boost bar size", "The bar's size now (it grows with the bar's extra segments; read only)."},
        {int(car::kBoostRate), "Boost use rate", "Boost units used per second of boost (the game sets it again when the bar changes size)."},
        {int(car::kHealth), "Health", "The car's health (100 % full; Road Rage ends a race at 0): LEFT / RIGHT change it."},
        {int(car::kRepair), "Repair", "Health back to full (CONFIRM)."},
        {int(car::kFillBoost), "Fill boost", "The bar full (CONFIRM)."},
        {int(car::kFreezeDamage), "Freeze damage", "On: the health never goes down (written back each frame)."},
        {int(car::kInfiniteBoost), "Infinite boost", "On: the game's own infinite boost (the bar stays full), set again each frame."},
    };
    return r;
}
struct CarTuning {
    const char* group;
    const char* name;
    const char* label;
};
// the model's main tuning values (VehiclePhysics/<the player's car>.cfg): every car of that model shares them
inline const std::vector<CarTuning>& car_tuning() {
    static const std::vector<CarTuning> t = {
        {"Physics/Race Car/Misc", "Max Speed In Boost", "Top speed in boost"},
        {"Physics/Race Car/Misc", "Accel Multiplier", "Acceleration"},
        {"Physics/Transmission/Engine", "Torque", "Engine torque"},
        {"Physics/Transmission/Boost Kick", "Boost Kick Torque", "Boost kick"},
        {"Physics/Race Car/Misc", "Braking Factor", "Brakes"},
        {"Physics/Race Car/Misc", "Drag Coef", "Drag"},
        {"Physics/Race Car/Misc", "Downforce Coef", "Downforce (grip)"},
        {"Physics/Vehicle", "Mass (Kg)", "Mass (kg)"},
        {"Physics/Steering", "Response", "Steering response"},
        {"Physics/Drift", "Turn Rate", "Drift turn rate"},
        {"Physics/Race Car/Misc", "Wall Collsion Penalty", "Wall hit penalty"},
    };
    return t;
}
// The starter list (MOST USED): the most interesting values to begin with. A per-car one resolves to the player's car.
inline const std::vector<CarTuning>& starter() {
    static const std::vector<CarTuning> s = {
        {"Physics/Race Car/Misc", "Max Speed In Boost", ""},
        {"Physics/Race Car/Misc", "Accel Multiplier", ""},
        {"Physics/Transmission/Engine", "Torque", ""},
        {"Physics/Vehicle", "Mass (Kg)", ""},
        {"Physics/Steering", "Response", ""},
        {"Physics/Drift", "Turn Rate", ""},
        {"Boost Bar", "Boost Rate (Boost Units per Second)", ""},
        {"Boost Bar", "Boost Earning Multiplier", ""},
        {"Score/Boost/Near Miss", "Boost value per Near Miss (Boost units)", ""},
        {"Score/Boost/Drift", "Boost value per Drift metre (Boost units)", ""},
        {"Score/Boost/Takedowns", "Takedown Boost value (Boost units)", ""},
        {"AI/Car", "Top speed mps", ""},
        {"Camera/Follow/", "Camera Offset", ""},
        {"Bloom Parameters", "Intensity", ""},
    };
    return s;
}
// a list entry to a variable: the player's car's file first, else the first live one
inline long resolve(const Tree& t, std::string_view group, std::string_view name) {
    if (!t.player().empty())
        if (const long i = t.find(group, name, t.player()); i >= 0) return i;
    return t.find(group, name);
}

inline float f32(std::uint32_t w) { return as_float(w); }
inline std::string fixed(double v, int d) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", d, v);
    return b;
}
// a car row's value text from the snapshot
inline std::string car_value(int field, const B3TuneCar& c) {
    if (!c.active && field != int(car::kFreezeDamage) && field != int(car::kInfiniteBoost)) return "-";
    switch (field) {
    case kCarSpeed: {
        const double shown = double(c.speed) * double(c.speed_factor);
        const bool mph = c.speed_factor > 2.2f && c.speed_factor < 2.3f, kmh = c.speed_factor > 3.5f && c.speed_factor < 3.7f;
        return std::to_string(long(shown)) + (mph ? " MPH" : kmh ? " KM/H" : "") + "  (" + fixed(c.speed, 1) + " M/S)";
    }
    case int(car::kBoost): return fixed(c.boost, 1) + (c.boosting ? "  BOOSTING" : "");
    case kCarBoostMax: return fixed(c.boost_max, 1);
    case int(car::kBoostRate): return fixed(c.boost_rate, 2);
    case int(car::kHealth): return std::to_string(int(std::lround(double(c.health) * 100.0))) + " %";
    case int(car::kRepair): return c.health >= 1.0f ? "FULL" : "REPAIR";
    case int(car::kFillBoost): return c.boost >= c.boost_max ? "FULL" : "FILL";
    case int(car::kFreezeDamage): return c.freeze ? "ON" : "OFF";
    case int(car::kInfiniteBoost): return c.infinite ? "ON" : "OFF";
    default: return "";
    }
}
inline bool car_editable(int field) {
    return field == int(car::kBoost) || field == int(car::kBoostRate) || field == int(car::kHealth) || field == int(car::kRepair) ||
           field == int(car::kFillBoost) || field == int(car::kFreezeDamage) || field == int(car::kInfiniteBoost);
}
// The word a car row's change sends (dir -1 / +1 a step, 0 CONFIRM; mult 10 coarse, -10 fine); false: nothing to send
inline bool car_change(int field, const B3TuneCar& c, int dir, int mult, std::uint32_t& word) {
    const double scale = mult >= 2 ? double(mult) : mult <= -2 ? 1.0 / double(-mult) : 1.0;
    switch (field) {
    case int(car::kBoost): {
        if (!dir || !c.active) return false;
        const double step = std::max(1.0, double(c.boost_max) / 20.0) * scale;
        word = float_word(float(std::clamp(double(c.boost) + dir * step, 0.0, double(c.boost_max))));
        return true;
    }
    case int(car::kBoostRate): {
        if (!dir || !c.active) return false;
        const double a = std::fabs(double(c.boost_rate));
        const double step = (a < 1e-6 ? 0.01 : std::pow(10.0, std::floor(std::log10(a)) - 1.0)) * scale;
        double v = std::max(0.0, double(c.boost_rate) + dir * step);
        v = std::round(v / step) * step;
        word = float_word(float(v));
        return true;
    }
    case int(car::kHealth): {
        if (!dir || !c.active) return false;
        word = float_word(float(std::clamp(double(c.health) + dir * 0.05 * scale, 0.0, 1.0)));
        return true;
    }
    case int(car::kRepair):
    case int(car::kFillBoost):
        if (dir < 0 || !c.active) return false;
        word = 1;
        return true;
    case int(car::kFreezeDamage): word = c.freeze ? 0u : 1u; return true;
    case int(car::kInfiniteBoost): word = c.infinite ? 0u : 1u; return true;
    default: return false;
    }
}

// ---- favourites and recently changed (kept in a small ini in the user folder) ----
// An element's identity across runs: "<group> | <name> [<element>] @ <config file>" (the key differs per model, the
// file names the model)
inline std::string ident(const VarInfo& v, std::uint32_t el) {
    return v.group + " | " + v.name + " [" + std::to_string(el) + "] @ " + v.file;
}
class Usage {
public:
    static constexpr std::size_t kRecent = 12;
    bool favourite(const std::string& id) const { return std::find(fav_.begin(), fav_.end(), id) != fav_.end(); }
    // true: now a favourite
    bool toggle(const std::string& id) {
        if (const auto it = std::find(fav_.begin(), fav_.end(), id); it != fav_.end()) {
            fav_.erase(it);
            dirty_ = true;
            return false;
        }
        fav_.push_back(id);
        dirty_ = true;
        return true;
    }
    void changed(const std::string& id) {
        if (!recent_.empty() && recent_.front() == id) return;
        recent_.erase(std::remove(recent_.begin(), recent_.end(), id), recent_.end());
        recent_.insert(recent_.begin(), id);
        if (recent_.size() > kRecent) recent_.resize(kRecent);
        dirty_ = true;
    }
    const std::vector<std::string>& favourites() const { return fav_; }
    const std::vector<std::string>& recent() const { return recent_; }
    bool dirty() const { return dirty_; }
    std::string text() const {
        std::string t = "# The tuning console's favourites and recently changed values (docs/modding/tuning.md).\n"
                        "# One per line: <group> | <name> [<element>] @ <config file>\n[favourites]\n";
        for (const std::string& f : fav_) t += f + "\n";
        t += "\n[recent]\n";
        for (const std::string& r : recent_) t += r + "\n";
        return t;
    }
    void parse(std::string_view text) {
        fav_.clear();
        recent_.clear();
        int sec = 0;
        std::size_t at = 0;
        while (at <= text.size()) {
            const std::size_t nl = text.find('\n', at);
            std::string_view l = trim(text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at));
            at = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
            if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
            if (l.empty() || l[0] == '#' || l[0] == ';') continue;
            if (l == "[favourites]") { sec = 1; continue; }
            if (l == "[recent]") { sec = 2; continue; }
            if (l[0] == '[' && l.back() == ']') { sec = 0; continue; }   // a section of a later version: skipped
            if (l.find(" | ") == std::string_view::npos || l.find(" @ ") == std::string_view::npos) continue;
            std::vector<std::string>& to = sec == 2 ? recent_ : fav_;
            if (sec == 0) continue;
            if (std::find(to.begin(), to.end(), std::string(l)) == to.end()) to.emplace_back(l);
        }
        if (recent_.size() > kRecent) recent_.resize(kRecent);
        dirty_ = false;
    }
    bool load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        parse(ss.str());
        return true;
    }
    bool save(const std::string& path) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << text();
        if (!f) return false;
        dirty_ = false;
        return true;
    }

private:
    std::vector<std::string> fav_, recent_;
    bool dirty_ = false;
};
// an identity back to a live element: (var index, element); false: not registered now
inline bool resolve_ident(const Tree& t, const std::string& id, Item& out) {
    const std::size_t bar = id.find(" | "), at = id.rfind(" @ ");
    if (bar == std::string::npos || at == std::string::npos || at < bar) return false;
    std::string name = id.substr(bar + 3, at - bar - 3);
    std::uint32_t el = 0;
    if (const std::size_t ob = name.rfind(" ["); ob != std::string::npos && !name.empty() && name.back() == ']') {
        el = std::uint32_t(std::strtoul(name.substr(ob + 2).c_str(), nullptr, 10));
        name = name.substr(0, ob);
    }
    const std::string group = id.substr(0, bar), file = id.substr(at + 3);
    for (const VarInfo& v : t.vars())
        if (v.group == group && v.name == name && v.file == file && el < v.elements) {
            out = {v.index, el};
            return true;
        }
    return false;
}

// ---- the browser: one view's place in the tree ----
// A page's row: a section to open, a variable's element, a car row, an action (tunenav2: the CHANGED page's RESET ALL),
// or a line of text
struct Row {
    enum class Kind : std::uint8_t { kNode, kVar, kCar, kText, kAction } kind = Kind::kText;
    std::string label;
    int node = -1;                 // kNode: the node to open (or one of the virtual pages below)
    Item item;                     // kVar
    int car = 0;                   // kCar: its field
    int action = 0;                // kAction: kActResetAll
    bool elsewhere = false;        // kVar / kCar: shown outside its place (car page, most used, changed, search): CONFIRM jumps there
};
enum : int { kPageMost = -10, kPageFavourites = -11, kPageRecent = -12, kPageStarter = -13, kPageCar = -14, kPageSearch = -15,
             kPageChanged = -16 };
enum : int { kActResetAll = 1 };

// tunenav2: one value that differs from its default now (native_play says which: B3TuneHooks::changes), or a car row
// the console changed (FREEZE DAMAGE or INFINITE BOOST on, the boost use rate). The browser lists them (the CHANGED
// page), counts them per tab, section and subsection, and says which belong to a page (RESET SECTION).
struct Change {
    Item item;                     // a variable's element (car 0)
    int car = 0;                   // else a car row's field (b3tune::car::Field)
    bool live = true;              // registered now (else kept for when it registers again: not in the tree)
    std::string label;             // not live: its name for the list
    bool operator==(const Change& o) const {
        return item.var == o.item.var && item.element == o.item.element && car == o.car && live == o.live && label == o.label;
    }
};

class Browser {
public:
    explicit Browser(Usage* usage = nullptr) : usage_(usage) { stack_ = {{root_of(0), 0}}; }
    void set_usage(Usage* u) { usage_ = u; }
    // a new tree (the live set changed): the place is kept by its titles and the cursor's variable
    void set_vars(const std::vector<VarInfo>& vars) {
        if (searching_) {   // the results again; the place to go back to is found again when the search closes
            tree_.build(vars);
            recount();
            saved_ = {{root_of(saved_tab_), 0}};
            rows_dirty_ = true;
            clamp();
            return;
        }
        std::vector<std::string> labels;
        for (const Level& l : stack_)
            labels.push_back(l.node >= 0 && std::size_t(l.node) < tree_.size() ? tree_.node(l.node).label : std::to_string(l.node));
        const Row* cur = current();
        const bool had_var = cur && cur->kind == Row::Kind::kVar;
        const Item keep = had_var ? cur->item : Item{};
        const VarInfo* kv = had_var ? tree_.var(keep.var) : nullptr;
        const std::string keep_id = kv ? ident(*kv, keep.element) : std::string();
        const std::size_t keep_cursor = cursor();
        tree_.build(vars);
        recount();
        // the same titles down from the tab's root
        std::vector<Level> st = {{root_of(tab_), 0}};
        for (std::size_t k = 1; k < labels.size(); ++k) {
            const int at = st.back().node;
            int found = -2;
            if (at >= 0) {
                for (int kid : tree_.node(at).kids)
                    if (tree_.node(kid).label == labels[k]) found = kid;
            } else if (labels[k].size() && (labels[k][0] == '-')) {
                found = std::atoi(labels[k].c_str());
            }
            if (found == -2) break;
            st.push_back({found, 0});
        }
        stack_ = st;
        rows_dirty_ = true;
        stack_.back().cursor = keep_cursor;
        if (!keep_id.empty()) {
            Item it;
            if (resolve_ident(tree_, keep_id, it)) {
                const std::vector<Row>& rs = rows();
                for (std::size_t i = 0; i < rs.size(); ++i)
                    if (rs[i].kind == Row::Kind::kVar && rs[i].item.var == it.var && rs[i].item.element == it.element) stack_.back().cursor = i;
            }
        }
        clamp();
    }
    const Tree& tree() const { return tree_; }

    // tabs
    std::size_t tab() const { return tab_; }
    void set_tab(std::size_t t) {
        if (searching_) search_close();
        tab_ = t % kCats;
        stack_ = {{root_of(tab_), 0}};
        rows_dirty_ = true;
        clamp();
    }
    void next_tab(int dir) { set_tab((tab_ + kCats + std::size_t(dir % int(kCats) + int(kCats))) % kCats); }

    // the current page
    const std::vector<Row>& rows() const {
        if (rows_dirty_) {
            rows_ = build_rows(stack_.back().node);
            rows_dirty_ = false;
        }
        return rows_;
    }
    void refresh() { rows_dirty_ = true; clamp(); }   // a list page's content changed (favourites, recent)
    std::size_t cursor() const { return stack_.back().cursor; }
    void set_cursor(std::size_t c) { stack_.back().cursor = c; clamp(); }
    const Row* current() const {
        const std::vector<Row>& rs = rows();
        return cursor() < rs.size() ? &rs[cursor()] : nullptr;
    }
    void move(int d) {
        const std::vector<Row>& rs = rows();
        if (rs.empty()) return;
        long c = long(cursor());
        const long step = d > 0 ? 1 : -1;
        for (int k = 0; k < std::abs(d); ++k) {   // text rows are skipped
            long n = c + step;
            while (n >= 0 && n < long(rs.size()) && rs[std::size_t(n)].kind == Row::Kind::kText) n += step;
            if (n < 0 || n >= long(rs.size())) break;
            c = n;
        }
        stack_.back().cursor = std::size_t(c);
    }
    int depth() const { return int(stack_.size()) - 1; }
    int page() const { return stack_.back().node; }
    // opens the cursor's section, or (a variable shown elsewhere) goes to its place; true: moved
    bool enter() {
        const Row* r = current();
        if (!r) return false;
        if (r->kind == Row::Kind::kNode) {
            stack_.push_back({r->node, 0});
            rows_dirty_ = true;
            clamp();
            return true;
        }
        if (r->kind == Row::Kind::kVar && r->elsewhere) return jump(r->item);
        if (r->kind == Row::Kind::kCar && r->elsewhere) return jump_car(r->car);
        return false;
    }
    // tunenav2: to a car row on the YOUR CAR page
    bool jump_car(int field) {
        set_tab(std::size_t(Cat::kCar));
        const std::vector<Row>& rs = rows();
        for (std::size_t i = 0; i < rs.size(); ++i)
            if (rs[i].kind == Row::Kind::kCar && rs[i].car == field) {
                stack_.back().cursor = i;
                return true;
            }
        return false;
    }

    // ---- tunenav2: what differs from its default (the CHANGED page, the counts, RESET SECTION) ----
    // the values changed now (in registration order) and the car rows; the CHANGED page's rows are made again only
    // when the list differs from the last one
    void set_changes(std::vector<Change> c) {
        if (c == changes_) return;
        changes_ = std::move(c);
        recount();
        if (page() == kPageChanged) {
            rows_dirty_ = true;
            clamp();
        }
    }
    const std::vector<Change>& changes() const { return changes_; }
    bool is_changed(const Item& it) const { return changed_.count({it.var, it.element}) != 0; }
    bool car_changed(int field) const { return car_changed_.count(field) != 0; }
    // how many changed values are in or below a page: a tab, a section, a subsection; the CHANGED page all of them; a
    // list page (favourites, search ...) those it lists; YOUR CAR its car rows and the model's tuning it shows
    std::size_t changed_count(int node) const {
        if (node >= 0) {
            const auto it = node_changed_.find(node);
            return it == node_changed_.end() ? 0 : it->second;
        }
        if (node == kPageChanged) return changes_.size();
        if (node == kPageMost) return 0;
        std::size_t n = 0;
        for (const Row& r : node == page() ? rows() : build_rows(node))
            n += (r.kind == Row::Kind::kVar && is_changed(r.item)) || (r.kind == Row::Kind::kCar && car_changed(r.car));
        return n;
    }
    std::size_t tab_changed(std::size_t tab) const { return changed_count(root_of(tab % kCats)); }
    // RESET SECTION: the changed values of the page the view is on (a section: its subsections too; a list page: what it
    // lists; the CHANGED page: all)
    std::vector<Change> section_changes() const {
        std::vector<Change> out;
        const int n = page();
        if (n == kPageChanged) return changes_;
        if (n >= 0) {
            for (const Change& c : changes_) {
                if (c.car || !c.live) continue;
                for (int k = tree_.where(c.item.var, c.item.element).first; k >= 0; k = tree_.node(k).parent)
                    if (k == n) { out.push_back(c); break; }
            }
            return out;
        }
        for (const Row& r : rows()) {
            if (r.kind == Row::Kind::kVar && is_changed(r.item)) {
                for (const Change& c : changes_)
                    if (!c.car && c.item.var == r.item.var && c.item.element == r.item.element) { out.push_back(c); break; }
            } else if (r.kind == Row::Kind::kCar && car_changed(r.car)) {
                Change c;
                c.car = r.car;
                out.push_back(c);
            }
        }
        return out;
    }
    // up one level; false at the tab's top (or a search left)
    bool back() {
        if (searching_) { search_close(); return true; }
        if (stack_.size() <= 1) return false;
        stack_.pop_back();
        rows_dirty_ = true;
        clamp();
        return true;
    }
    // to an element's place in its tab. By value (macintel1): enter() passes a row's item, and rows() below rebuilds
    // rows_, so a reference would read freed memory (AddressSanitizer: heap-use-after-free; the cursor landed on
    // element 0 under macOS's allocator)
    bool jump(Item it) {
        const auto [node, row] = tree_.where(it.var, it.element);
        if (node < 0) return false;
        if (searching_) search_close();
        const std::vector<int> p = tree_.path(node);
        tab_ = std::size_t(tree_.node(p.front()).cat);
        stack_.clear();
        for (int n : p) stack_.push_back({n, 0});
        rows_dirty_ = true;
        const std::vector<Row>& rs = rows();
        for (std::size_t i = 0; i < rs.size(); ++i)
            if (rs[i].kind == Row::Kind::kVar && rs[i].item.var == it.var && rs[i].item.element == it.element) stack_.back().cursor = i;
        (void)row;
        return true;
    }
    // where the view is: "HANDLING > STEERING > COMPCAR1 (YOUR CAR)"
    std::string crumbs() const {
        if (searching_) return "SEARCH: " + upper(query_);
        std::string t;
        for (const Level& l : stack_) {
            std::string s = l.node >= 0 ? (std::size_t(l.node) < tree_.size() ? upper(tree_.node(l.node).label) : std::string("?"))
                                        : page_title(l.node);
            t += (t.empty() ? "" : " > ") + s;
        }
        return t;
    }

    // search (a keyboard): every element whose "<group> | <name>" and file contain every word typed
    bool searching() const { return searching_; }
    const std::string& query() const { return query_; }
    void search_open() {
        if (!searching_) {
            saved_ = stack_;
            saved_tab_ = tab_;
            query_.clear();   // a new search starts empty
        }
        searching_ = true;
        stack_ = {{kPageSearch, 0}};
        rows_dirty_ = true;
    }
    void search_type(char c) { query_ += c; rows_dirty_ = true; stack_.back().cursor = 0; clamp(); }
    void search_erase() { if (!query_.empty()) query_.pop_back(); rows_dirty_ = true; stack_.back().cursor = 0; clamp(); }
    void search_close() {
        if (!searching_) return;
        searching_ = false;
        stack_ = saved_.empty() ? std::vector<Level>{{root_of(tab_), 0}} : saved_;
        tab_ = saved_tab_;
        rows_dirty_ = true;
        clamp();
    }
    std::vector<Item> search(std::string_view q, std::size_t limit = 400) const {
        std::vector<std::string> words;
        std::string w;
        for (char c : q) {
            if (c == ' ') { if (!w.empty()) words.push_back(w); w.clear(); }
            else w += lower(c);
        }
        if (!w.empty()) words.push_back(w);
        std::vector<Item> out;
        if (words.empty()) return out;
        for (const auto& [n, it] : tree_.all()) {
            const VarInfo* v = tree_.var(it.var);
            if (!v) continue;
            std::string hay = v->group + " | " + v->name + " " + file_stem(v->file);
            for (char& c : hay) c = lower(c);
            bool all = true;
            for (const std::string& x : words) all = all && hay.find(x) != std::string::npos;
            if (all) out.push_back(it);
            if (out.size() >= limit) break;
        }
        return out;
    }
    // a page's title (the virtual pages)
    static std::string page_title(int node) {
        switch (node) {
        case kPageMost: return "MOST USED";
        case kPageFavourites: return "FAVOURITES";
        case kPageRecent: return "RECENTLY CHANGED";
        case kPageStarter: return "STARTER LIST";
        case kPageCar: return "YOUR CAR";
        case kPageSearch: return "SEARCH";
        case kPageChanged: return "CHANGED";
        default: return "?";
        }
    }

private:
    struct Level {
        int node;
        std::size_t cursor;
    };
    static int root_of(std::size_t tab) {
        return tab == std::size_t(Cat::kCar)        ? kPageCar
               : tab == std::size_t(Cat::kChanged)  ? kPageChanged
               : tab == std::size_t(Cat::kMostUsed) ? kPageMost
                                                    : int(tab);
    }
    // the changed set and the counts per node (after a new tree or a new list)
    void recount() {
        changed_.clear();
        car_changed_.clear();
        node_changed_.clear();
        for (const Change& c : changes_) {
            if (c.car) { car_changed_.insert(c.car); continue; }
            if (!c.live) continue;
            changed_.insert({c.item.var, c.item.element});
            for (int k = tree_.where(c.item.var, c.item.element).first; k >= 0; k = tree_.node(k).parent) ++node_changed_[k];
        }
    }
    void clamp() {
        const std::vector<Row>& rs = rows();
        std::size_t& c = stack_.back().cursor;
        if (rs.empty()) { c = 0; return; }
        if (c >= rs.size()) c = rs.size() - 1;
        if (rs[c].kind == Row::Kind::kText) {   // the nearest selectable row (down, then up)
            std::size_t k = c;
            while (k < rs.size() && rs[k].kind == Row::Kind::kText) ++k;
            if (k == rs.size()) {
                k = c;
                while (k > 0 && rs[k].kind == Row::Kind::kText) --k;
            }
            c = k;
        }
    }
    Row var_row(const Item& it, bool elsewhere, const std::string& label = {}) const {
        Row r;
        r.kind = Row::Kind::kVar;
        r.item = it;
        r.elsewhere = elsewhere;
        const VarInfo* v = tree_.var(it.var);
        r.label = !label.empty() ? label : v ? element_label(*v, it.element) : "?";
        if (!label.empty() && v && v->elements > 1) r.label += v->type == 5 ? (it.element == 0 ? " X" : it.element == 1 ? " Y" : " Z")
                                                                           : " [" + std::to_string(it.element) + "]";
        return r;
    }
    void list_rows(const std::vector<std::string>& ids, std::vector<Row>& out, const char* empty) const {
        for (const std::string& id : ids) {
            Item it;
            if (resolve_ident(tree_, id, it)) out.push_back(var_row(it, true));
        }
        if (out.empty()) {
            Row t;
            t.label = empty;
            out.push_back(t);
        }
    }
    std::vector<Row> build_rows(int node) const {
        std::vector<Row> out;
        auto text = [&](std::string s) { Row t; t.label = std::move(s); out.push_back(t); };
        auto nodes = [&](int id, std::string label) { Row r; r.kind = Row::Kind::kNode; r.node = id; r.label = std::move(label); out.push_back(r); };
        if (node >= 0 && std::size_t(node) < tree_.size()) {
            const Node& n = tree_.node(node);
            for (int k : n.kids) nodes(k, tree_.node(k).label);
            for (const Item& it : n.items) out.push_back(var_row(it, false));
            if (out.empty()) text("Nothing registered here now (a race registers most values).");
            return out;
        }
        switch (node) {
        case kPageMost:
            nodes(kPageFavourites, "Favourites");
            nodes(kPageRecent, "Recently changed");
            nodes(kPageStarter, "Starter list");
            break;
        case kPageFavourites:
            list_rows(usage_ ? usage_->favourites() : std::vector<std::string>{}, out, "No favourites yet: F (a controller: TRIANGLE) on a value pins it.");
            break;
        case kPageRecent:
            list_rows(usage_ ? usage_->recent() : std::vector<std::string>{}, out, "Nothing changed yet.");
            break;
        case kPageStarter:
            for (const CarTuning& s : starter()) {
                const long i = resolve(tree_, s.group, s.name);
                if (i < 0) continue;
                const VarInfo* v = tree_.var(std::uint32_t(i));
                for (std::uint32_t el = 0; v && el < v->elements; ++el) out.push_back(var_row({std::uint32_t(i), el}, true));
            }
            if (out.empty()) text("None of the list is registered now (start a race).");
            break;
        case kPageCar: {
            text(tree_.player().empty() ? "Your car (no race now)" : "Your car: " + tree_.player());
            for (const CarRow& c : car_rows()) {
                Row r;
                r.kind = Row::Kind::kCar;
                r.car = c.field;
                r.label = c.label;
                out.push_back(r);
            }
            bool head = false;
            for (const CarTuning& t : car_tuning()) {
                if (tree_.player().empty()) break;
                const long i = tree_.find(t.group, t.name, tree_.player());
                if (i < 0) continue;
                if (!head) { text("This model's tuning (every car of the model)"); head = true; }
                const VarInfo* v = tree_.var(std::uint32_t(i));
                for (std::uint32_t el = 0; v && el < v->elements; ++el) out.push_back(var_row({std::uint32_t(i), el}, true, t.label));
            }
            break;
        }
        case kPageChanged: {   // tunenav2
            if (changes_.empty()) {
                text("Nothing changed: every value is the game's own.");
                break;
            }
            Row a;
            a.kind = Row::Kind::kAction;
            a.action = kActResetAll;
            a.label = "Reset all";
            out.push_back(a);
            if (!car_changed_.empty()) {
                text("YOUR CAR (" + std::to_string(car_changed_.size()) + ")");
                for (const CarRow& c : car_rows()) {
                    if (!car_changed(c.field)) continue;
                    Row r;
                    r.kind = Row::Kind::kCar;
                    r.car = c.field;
                    r.label = c.label;
                    r.elsewhere = true;
                    out.push_back(r);
                }
            }
            std::vector<std::vector<Item>> per(kCats);   // in the tabs' order
            if (!changed_.empty())
                for (const auto& [n, it] : tree_.all())
                    if (is_changed(it)) per[std::size_t(tree_.node(n).cat)].push_back(it);
            for (std::size_t c = 0; c < kCats; ++c) {
                if (per[c].empty()) continue;
                text(std::string(cat_info(Cat(c)).tab) + " (" + std::to_string(per[c].size()) + ")");
                for (const Item& it : per[c]) out.push_back(var_row(it, true));
            }
            bool head = false;
            for (const Change& ch : changes_) {
                if (ch.car || ch.live) continue;
                if (!head) { text("Not in this race (kept for when it registers again)"); head = true; }
                Row r = var_row(ch.item, true, ch.label);
                r.label = ch.label;
                out.push_back(r);
            }
            break;
        }
        case kPageSearch: {
            const std::vector<Item> found = search(query_);
            for (const Item& it : found) out.push_back(var_row(it, true));
            if (found.empty()) text(query_.empty() ? "Type a name (any part of it)." : "Nothing registered now matches.");
            break;
        }
        default: break;
        }
        return out;
    }

    Tree tree_;
    Usage* usage_ = nullptr;
    std::size_t tab_ = 0;
    std::vector<Level> stack_;
    std::vector<Level> saved_;
    std::size_t saved_tab_ = 0;
    bool searching_ = false;
    std::string query_;
    mutable std::vector<Row> rows_;
    mutable bool rows_dirty_ = true;
    // tunenav2
    std::vector<Change> changes_;
    std::set<std::pair<std::uint32_t, std::uint32_t>> changed_;
    std::set<int> car_changed_;
    std::map<int, std::size_t> node_changed_;
};

}  // namespace b3tune::browse

#endif
