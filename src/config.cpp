#include "config.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <stdexcept>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace slk {
namespace {

std::string env(const char* key) {
  const char* v = std::getenv(key);
  return v ? std::string(v) : std::string();
}

}  // namespace

std::filesystem::path config_dir() {
  std::string xdg = env("XDG_CONFIG_HOME");
  std::filesystem::path base;
  if (!xdg.empty()) {
    base = xdg;
  } else {
    std::string home = env("HOME");
    if (home.empty()) home = env("USERPROFILE");
    if (home.empty()) throw std::runtime_error("cannot locate home directory");
    base = std::filesystem::path(home) / ".config";
  }
  return base / "slkx";
}

std::filesystem::path profile_path(const std::string& name) {
  return config_dir() / (name + ".json");
}

std::filesystem::path cache_path(const std::string& name) {
  return config_dir() / (name + ".channels.json");
}

std::string resolve_profile_name(const std::string& flag_value) {
  if (!flag_value.empty()) return flag_value;
  std::string e = env("SLKX_WORKSPACE");
  if (!e.empty()) return e;
  return "default";
}

Profile load_profile(const std::string& name, const std::string& token_override,
                     const std::string& cookie_override) {
  Profile p;
  p.name = name;

  const auto path = profile_path(name);
  if (std::filesystem::exists(path)) {
    std::ifstream in(path);
    nlohmann::json j;
    in >> j;
    p.token = j.value("token", "");
    p.cookie = j.value("cookie", "");
    p.team = j.value("team", "");
  }

  if (const auto e = env("SLACK_CTOKEN"); !e.empty()) p.token = e;
  if (const auto e = env("SLACK_DTOKEN"); !e.empty()) p.cookie = e;
  if (!token_override.empty()) p.token = token_override;
  if (!cookie_override.empty()) p.cookie = cookie_override;

  std::string missing;
  if (p.token.empty()) missing = "SLACK_CTOKEN";
  if (p.cookie.empty()) missing += (missing.empty() ? "" : " and ") + std::string("SLACK_DTOKEN");
  if (!missing.empty()) {
    throw std::runtime_error(
        "no Slack session for profile '" + name + "': missing " + missing +
        ".\nExport SLACK_CTOKEN (xoxc-...) and SLACK_DTOKEN "
        "(xoxd-...), or run:\n  slkx auth set --token xoxc-... --cookie xoxd-...");
  }
  return p;
}

void save_profile(const Profile& p) {
  std::filesystem::create_directories(config_dir());
  const auto path = profile_path(p.name);

  nlohmann::json j;
  j["token"] = p.token;
  j["cookie"] = p.cookie;
  j["team"] = p.team;

  {
    std::ofstream out(path, std::ios::trunc);
    out << j.dump(2) << "\n";
  }

#if !defined(_WIN32)
  ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
}

}  // namespace slk
