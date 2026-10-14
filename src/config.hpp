#pragma once
#include <filesystem>
#include <string>

namespace slk {

struct Profile {
  std::string name = "default";
  std::string token;   // xoxc-...
  std::string cookie;  // xoxd-...
  std::string team;    // cosmetic, filled in by `auth status`
};

// ~/.config/slkx  (respects XDG_CONFIG_HOME)
std::filesystem::path config_dir();
std::filesystem::path profile_path(const std::string& name);
std::filesystem::path cache_path(const std::string& name);

// Which profile to use: --workspace flag > SLKX_WORKSPACE > "default".
std::string resolve_profile_name(const std::string& flag_value);

// Precedence, applied to each credential independently:
//   --token/--cookie > SLACK_CTOKEN/SLACK_DTOKEN env > profile file.
// Throws std::runtime_error naming whichever credential is missing.
Profile load_profile(const std::string& name, const std::string& token_override,
                     const std::string& cookie_override);

// Writes 0600. The cookie is exactly as sensitive as the token. Replace this
// with a keychain backend before you hand the tool to anyone else: Keychain
// Services on macOS, libsecret on Linux, DPAPI on Windows.
void save_profile(const Profile& p);

}  // namespace slk
