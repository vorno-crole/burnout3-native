// 1.1 one-click updates: fetching a release's package checked against its signed hash list (src/b3launcher/oneclick.h),
// with canned files (a throwaway key made for this test, trusted nowhere; SHA256SUMS signed with ssh-keygen).
#include "test.h"
#include "../src/b3launcher/oneclick.h"
#include "../src/b3launcher/sign.h"
#include "../src/b3launcher/update_key.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace b3launcher;
namespace fs = std::filesystem;

namespace {
const char* kPub = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIF/c6bu8ws5eR6hV9RQVkVJJ5CpjerHcwJCputW0FkNI b3 test key (not trusted)";
const char* kPackage = "PACKAGE BYTES 1.2.0";
const char* kSums =
    "1bd4bfd4e9db50fc388b0b8f24330bc691c14f8af2fe15ce2a609bc3a012030c  burnout3-native-1.2.0-abc-win64.zip\n"
    "1bd4bfd4e9db50fc388b0b8f24330bc691c14f8af2fe15ce2a609bc3a012030c  burnout3-native-1.2.0-abc-linux-x86_64.tar.gz\n";
const char* kSig =
    "-----BEGIN SSH SIGNATURE-----\n"
    "U1NIU0lHAAAAAQAAADMAAAALc3NoLWVkMjU1MTkAAAAgX9zpu7zCzl5HqFX1FBWRUknkKm\n"
    "N6sdzAkKm61bQWQ0gAAAAWYnVybm91dDMtbmF0aXZlLXVwZGF0ZQAAAAAAAAAGc2hhNTEy\n"
    "AAAAUwAAAAtzc2gtZWQyNTUxOQAAAEC50Sv0pvs1M8Kx7/jLZHInZ7CAGppL3w83GgOQPE\n"
    "XdSKkwOUTMMJ3dtOaA5q3gIL0myT32hffLafC6BMcnGhoC\n"
    "-----END SSH SIGNATURE-----\n";
const std::string kBase = "https://github.com/u0rhs/burnout3-native/releases/download/v1.2.0/";

update::Release release() {
    update::Release r;
    r.tag = "v1.2.0";
    for (const char* n : {"burnout3-native-1.2.0-abc-win64.zip", "burnout3-native-1.2.0-abc-linux-x86_64.tar.gz", "SHA256SUMS", "SHA256SUMS.sig"})
        r.assets.push_back({n, kBase + n, 0});
    return r;
}
struct Server {
    std::map<std::string, std::string> files;
    int requests = 0;
    Server() {
        files[kBase + "burnout3-native-1.2.0-abc-win64.zip"] = kPackage;
        files[kBase + "burnout3-native-1.2.0-abc-linux-x86_64.tar.gz"] = kPackage;
        files[kBase + "SHA256SUMS"] = kSums;
        files[kBase + "SHA256SUMS.sig"] = kSig;
    }
    oneclick::Fetch fetch() {
        return [this](const std::string& url, bool, std::size_t max) {
            ++requests;
            update::Response r;
            r.ok = true;
            const auto it = files.find(url);
            if (it == files.end()) { r.status = 404; return r; }
            if (it->second.size() > max) { r.ok = false; r.error = "too big"; return r; }
            r.status = 200;
            r.body = it->second;
            return r;
        };
    }
};
fs::path temp_dir(const char* name) {
    const fs::path d = fs::temp_directory_path() / (std::string("b3_oneclick_") + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}
std::string read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
std::array<std::uint8_t, 32> key() {
    std::array<std::uint8_t, 32> k{};
    sign::parse_ssh_ed25519(kPub, k);
    return k;
}
}  // namespace

TEST(signed_package_is_fetched_and_written) {
    Server s;
    const fs::path d = temp_dir("ok");
    int steps = 0;
    const oneclick::Result r = oneclick::fetch_package(release(), update::Platform::kWindows, "1.1.0+1a2b3c4d", key(), s.fetch(), d, [&](oneclick::Step) { ++steps; });
    CHECK(r.ok && r.why.empty());
    CHECK(r.package == d / "burnout3-native-1.2.0-abc-win64.zip" && read(r.package) == kPackage);
    CHECK(steps == 4 && s.requests == 3);
    CHECK(!fs::exists(d / "burnout3-native-1.2.0-abc-win64.zip.part"));
    const oneclick::Result l = oneclick::fetch_package(release(), update::Platform::kLinux, "1.1.0", key(), s.fetch(), d);
    CHECK(l.ok && l.package.filename() == "burnout3-native-1.2.0-abc-linux-x86_64.tar.gz");
}

TEST(release_replaces_its_candidate) {   // rcprep: the one-click update from 1.2.0-rc.1 to 1.2.0 (as 1.4.0-rc.1 -> 1.4.0)
    Server s;
    const fs::path d = temp_dir("rc");
    const oneclick::Result r = oneclick::fetch_package(release(), update::Platform::kWindows, "1.2.0-rc.1+1a2b3c4d", key(), s.fetch(), d);
    CHECK(r.ok && r.why.empty());
    const fs::path d2 = temp_dir("rcnewer");   // a candidate of a later version is not replaced by this release
    const oneclick::Result n = oneclick::fetch_package(release(), update::Platform::kWindows, "1.3.0-rc.1+1a2b3c4d", key(), s.fetch(), d2);
    CHECK(!n.ok && n.why.find("not newer") != std::string::npos && fs::is_empty(d2));
}

TEST(tampering_is_refused_and_nothing_written) {
    const auto refused = [](Server& s, const char* name, update::Platform p = update::Platform::kWindows, std::array<std::uint8_t, 32> k = key(),
                            update::Release rel = release(), const char* installed = "1.1.0") {
        const fs::path d = temp_dir(name);
        const oneclick::Result r = oneclick::fetch_package(rel, p, installed, k, s.fetch(), d);
        CHECK(!r.ok && !r.why.empty() && fs::is_empty(d));
        return r.why;
    };
    {   // the package changed after signing
        Server s;
        s.files[kBase + "burnout3-native-1.2.0-abc-win64.zip"] = "EVIL BYTES";
        CHECK(refused(s, "pkg").find("does not match the signed list") != std::string::npos);
    }
    {   // the hash list changed (a new hash for an evil package): the signature fails
        Server s;
        std::string sums = kSums;
        sums[0] = sums[0] == 'a' ? 'b' : 'a';
        s.files[kBase + "SHA256SUMS"] = sums;
        CHECK(refused(s, "sums").find("signature is not valid") != std::string::npos);
    }
    {   // signed by another key
        Server s;
        std::array<std::uint8_t, 32> other = key();
        other[3] ^= 1;
        CHECK(refused(s, "key", update::Platform::kWindows, other).find("another key") != std::string::npos);
    }
    {   // no signature file
        Server s;
        s.files.erase(kBase + "SHA256SUMS.sig");
        CHECK(refused(s, "nosig").find("SHA256SUMS.sig") != std::string::npos);
    }
    {   // no package for this system
        Server s;
        CHECK(refused(s, "mac", update::Platform::kMac).find("no package") != std::string::npos);
    }
    // robust2: the downgrade guard. The tag is not signed, the names in SHA256SUMS are.
    {   // the installed version is the signed package's: not an update
        Server s;
        CHECK(refused(s, "same", update::Platform::kWindows, key(), release(), "1.2.0+1a2b3c4d").find("not newer") != std::string::npos);
    }
    {   // a newer one is installed: an old signed release is never installed over it
        Server s;
        CHECK(refused(s, "older", update::Platform::kWindows, key(), release(), "1.2.1").find("not newer") != std::string::npos);
    }
    {   // an old signed release served under a new tag
        Server s;
        update::Release rel = release();
        rel.tag = "v9.9.9";
        CHECK(refused(s, "replay", update::Platform::kWindows, key(), rel).find("not the release's v9.9.9") != std::string::npos);
    }
    {   // the installed version is not known
        Server s;
        CHECK(refused(s, "unknown", update::Platform::kWindows, key(), release(), "").find("not known") != std::string::npos);
    }
}

TEST(package_names_carry_the_version) {
    const auto v = [](const char* n) {
        const auto p = oneclick::package_version(n);
        if (!p) return std::string("-");
        std::string s = std::to_string(p->major) + "." + std::to_string(p->minor) + "." + std::to_string(p->patch);
        for (const std::string& id : p->pre) s += "|" + id;
        return s;
    };
    CHECK(v("burnout3-native-1.2.2-1a2b3c4d-win64.zip") == "1.2.2");
    CHECK(v("burnout3-native-1.2.2-1a2b3c4d-dirty-linux-x86_64.tar.gz") == "1.2.2");
    CHECK(v("burnout3-native-1.2.2-macos-arm64.dmg") == "1.2.2");
    CHECK(v("burnout3-native-1.2.2-1a2b3c4d-macos-x86_64.dmg") == "1.2.2");   // macintel1: an Intel Mac's package
    CHECK(v("burnout3-native-1.3.0-rc.1-1a2b3c4d-win64.zip") == "1.3.0|rc|1");
    CHECK(v("burnout3-native-1.4.0-rc.1-1a2b3c4d-win64.zip") == "1.4.0|rc|1");   // rcprep: 1.4.0's tester candidate
    CHECK(v("burnout3-native-1.4.0-rc.1-1a2b3c4d-dirty-win64.zip") == "1.4.0|rc|1");
    CHECK(v("burnout3-native-1.2.0-abc-win64.zip") == "1.2.0");
    CHECK(v("burnout3-native-1.2.2-test-1a2b3c4d-win64.zip") == "1.2.2");   // a test build counts as its base version
    CHECK(v("burnout3-native-win64.zip") == "-");
    CHECK(v("burnout3-native-latest-win64.zip") == "-");
    CHECK(v("other-1.2.2-win64.zip") == "-");
    CHECK(v("burnout3-native-1.2.2-1a2b3c4d.zip") == "-");
}

TEST(no_release_key_means_off) {
    std::array<std::uint8_t, 32> k{};
    // update_key.h ships empty until the maintainer's key is put in: one-click updates stay off
    CHECK(oneclick::release_key(k) == (update::kReleaseKey[0] != 0));
}

TEST_MAIN("test_launcher_oneclick: one-click updates, the checked download (src/b3launcher/oneclick.h)")
