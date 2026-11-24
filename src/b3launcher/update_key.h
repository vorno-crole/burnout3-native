// update_key.h -- 1.1 one-click updates: the public half of the port's release key (sign.h). Only updates whose
// SHA256SUMS is signed by this key, in this namespace, are installed. The private half stays with the maintainer
// (docs/release/RELEASING.md "Signing"). Empty: one-click updates are off (the Play page's banner opens the release
// page as before).
#ifndef B3LAUNCHER_UPDATE_KEY_H
#define B3LAUNCHER_UPDATE_KEY_H

namespace b3launcher::update {

#if defined(__APPLE__) && defined(__x86_64__)
// macintel1 (the Intel Mac fork only): the fork's releases are not signed with this key, so one-click updates are off
// on an Intel Mac; the Play page's banner opens the fork's release page (update.h)
inline constexpr const char* kReleaseKey = "";
#else
inline constexpr const char* kReleaseKey = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIDHDaz6YisOFJiRp8kBkX38hK6mRj5EU84BDYqknIQfF burnout3-native releases";
#endif
inline constexpr const char* kSignatureNamespace = "burnout3-native-update";

}  // namespace b3launcher::update

#endif
