// oneclick.cpp -- see oneclick.h
#include "oneclick.h"
#include "b3port/fileio.h"   // fsrobust: UTF-8 paths

#include "sign.h"
#include "update_key.h"
#include "../native/sha256.h"

#include <cstdio>
#include <system_error>

namespace b3launcher::oneclick {

namespace {
constexpr std::size_t kSmallMax = 64 * 1024;
constexpr std::size_t kPackageMax = std::size_t(2) << 30;

Result fail(std::string why) {
    Result r;
    r.why = std::move(why);
    return r;
}
std::string hex(const std::array<std::uint8_t, 32>& h) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (const std::uint8_t b : h) {
        s += d[b >> 4];
        s += d[b & 15];
    }
    return s;
}
// One file of the release: the body of a 200 answer, else why not
bool get(const Fetch& fetch, const update::Release::Asset& a, bool download, std::size_t max, std::string& body, std::string* why) {
    const update::Response r = fetch(a.url, download, max);
    if (!r.ok) {
        *why = "could not download " + a.name + " (" + (r.error.empty() ? "no answer" : r.error) + ")";
        return false;
    }
    if (r.status != 200) {
        *why = "could not download " + a.name + " (HTTP " + std::to_string(r.status) + ")";
        return false;
    }
    body = r.body;
    return true;
}
std::string text_of(const update::Version& v) {
    std::string s = std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
    for (std::size_t k = 0; k < v.pre.size(); ++k) s += (k ? "." : "-") + v.pre[k];
    return s;
}
bool ends_with(std::string_view s, std::string_view t) { return s.size() >= t.size() && s.substr(s.size() - t.size()) == t; }
}  // namespace

std::optional<update::Version> package_version(std::string_view name) {
    constexpr std::string_view head = "burnout3-native-";
    if (name.substr(0, head.size()) != head) return std::nullopt;
    name.remove_prefix(head.size());
    bool tail = false;
    // macintel1: an Intel Mac's package too (tools/release/package_mac.sh names it by the CPU; update.cpp picks it)
    for (const std::string_view t : {"-win64.zip", "-linux-x86_64.tar.gz", "-macos-arm64.dmg", "-macos-x86_64.dmg"})
        if (ends_with(name, t)) {
            name.remove_suffix(t.size());
            tail = true;
            break;
        }
    if (!tail) return std::nullopt;
    if (ends_with(name, "-dirty")) name.remove_suffix(6);
    // the commit after the version ("-1a2b3c4d"), when what is left before it is a version
    if (const std::size_t dash = name.rfind('-'); dash != std::string_view::npos && dash + 1 < name.size()) {
        const std::string_view id = name.substr(dash + 1);
        bool hex = true;
        for (const char c : id) hex = hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        if (hex && update::parse_version(name.substr(0, dash))) name = name.substr(0, dash);
    }
    const std::optional<update::Version> v = update::parse_version(name);
    if (!v) return std::nullopt;
    return update::as_running(*v);
}

bool release_key(std::array<std::uint8_t, 32>& key) {
    return update::kReleaseKey[0] != '\0' && sign::parse_ssh_ed25519(update::kReleaseKey, key);
}

Result fetch_package(const update::Release& r, update::Platform p, std::string_view installed, const std::array<std::uint8_t, 32>& key,
                     const Fetch& fetch, const fs::path& dir, const OnStep& on_step) {
    const update::Release::Asset* pkg = update::package_asset(r, p);
    const update::Release::Asset* sums = update::find_asset(r, "SHA256SUMS");
    const update::Release::Asset* sig = update::find_asset(r, "SHA256SUMS.sig");
    if (!pkg) return fail("the release has no package for this system");
    if (!sums || !sig) return fail("the release is not signed (no SHA256SUMS / SHA256SUMS.sig)");
    if (on_step) on_step(Step::kSums);
    std::string sums_text, sig_text, why;
    if (!get(fetch, *sums, true, kSmallMax, sums_text, &why) || !get(fetch, *sig, true, kSmallMax, sig_text, &why)) return fail(why);
    if (!sign::verify_sshsig(sig_text, sums_text, update::kSignatureNamespace, key, &why)) return fail("the update's signature is not valid: " + why);
    const std::string want = update::sum_for(sums_text, pkg->name);
    if (want.empty()) return fail(pkg->name + " is not in the signed list");
    // The API's tag is not signed: a replayed older release (its own valid signature) under a new tag must not install
    // as an "update". The version in the signed name decides.
    const std::optional<update::Version> pv = package_version(pkg->name), tv = update::parse_version(r.tag), iv = update::parse_version(installed);
    if (!pv) return fail(pkg->name + " carries no version in its name");
    if (!tv || update::compare(*pv, update::as_running(*tv)) != 0)
        return fail("the signed package is version " + text_of(*pv) + ", not the release's " + r.tag);
    if (!iv) return fail("the installed version is not known");
    if (update::compare(*pv, update::as_running(*iv)) <= 0)
        return fail("the signed package (" + text_of(*pv) + ") is not newer than the installed version (" + text_of(update::as_running(*iv)) + ")");
    if (on_step) on_step(Step::kPackage);
    std::string bytes;
    if (!get(fetch, *pkg, true, kPackageMax, bytes, &why)) return fail(why);
    if (on_step) on_step(Step::kCheck);
    if (hex(b3native::sha256(bytes.data(), bytes.size())) != want) return fail("the download does not match the signed list (damaged or changed)");
    if (on_step) on_step(Step::kWrite);
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path to = dir / pkg->name, part = dir / (pkg->name + ".part");
    // fsrobust: written wide (a user name such as José in the folder), flushed, then put in place in one step
    if (!b3io::write_synced(part, bytes, &why) || !b3io::commit_replace(part, to, &why))
        return fail("could not write the download to " + b3io::u8string(dir) + ": " + why);
    std::printf("[oneclick] the update written to %s\n", b3io::u8string(to).c_str());
    Result res;
    res.ok = true;
    res.package = to;
    return res;
}

}  // namespace b3launcher::oneclick
