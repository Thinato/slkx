#include "resolver.hpp"

#include <cctype>
#include <fstream>

#include "config.hpp"

namespace slk {
namespace {

bool looks_like_id(const std::string& s) {
  if (s.size() < 9) return false;
  const char c = s[0];
  if (c != 'C' && c != 'D' && c != 'G') return false;
  for (char ch : s) {
    if (!(std::isupper(static_cast<unsigned char>(ch)) ||
          std::isdigit(static_cast<unsigned char>(ch))))
      return false;
  }
  return true;
}

}  // namespace

Resolver::Resolver(slack::Client& client, std::string profile)
    : client_(client), profile_(std::move(profile)) {}

void Resolver::ensure_loaded() {
  if (loaded_) return;
  const auto path = cache_path(profile_);
  if (std::filesystem::exists(path)) {
    std::ifstream in(path);
    try {
      in >> cache_;
      loaded_ = true;
      return;
    } catch (...) {
      // Corrupt cache is not worth reporting; just rebuild it.
    }
  }
  refresh();
}

void Resolver::refresh() {
  cache_ = nlohmann::json::object();
  cache_["channels"] = nlohmann::json::object();  // name -> id
  cache_["ids"] = nlohmann::json::object();       // id -> name
  cache_["users"] = nlohmann::json::object();     // handle -> user id
  cache_["user_names"] = nlohmann::json::object();// user id -> handle
  cache_["dms"] = nlohmann::json::object();       // user id -> D... channel

  // users:read
  auto members = client_.paginate("users.list", {{"limit", "200"}}, "members", 20);
  for (auto& m : members) {
    std::string id = m.value("id", "");
    std::string handle = m.value("name", "");
    if (id.empty() || handle.empty()) continue;
    cache_["users"][handle] = id;
    cache_["user_names"][id] = handle;
  }

  // channels:read, groups:read, im:read, mpim:read
  auto convos = client_.paginate(
      "users.conversations",
      {{"types", "public_channel,private_channel,mpim,im"},
       {"exclude_archived", "true"},
       {"limit", "200"}},
      "channels", 20);

  for (auto& c : convos) {
    std::string id = c.value("id", "");
    if (id.empty()) continue;

    if (c.value("is_im", false)) {
      std::string user = c.value("user", "");
      if (!user.empty()) {
        cache_["dms"][user] = id;
        std::string handle = cache_["user_names"].value(user, user);
        cache_["ids"][id] = "@" + handle;
      }
      continue;
    }

    std::string name = c.value("name", "");
    if (name.empty()) continue;
    cache_["channels"][name] = id;
    cache_["ids"][id] = name;
  }

  std::filesystem::create_directories(config_dir());
  std::ofstream out(cache_path(profile_), std::ios::trunc);
  out << cache_.dump() << "\n";
  loaded_ = true;
}

std::string Resolver::to_id(const std::string& input) {
  if (input.empty()) throw std::runtime_error("empty channel");
  if (looks_like_id(input)) return input;

  ensure_loaded();

  if (input[0] == '@') {
    const std::string handle = input.substr(1);
    if (!cache_["users"].contains(handle))
      throw std::runtime_error("unknown user: " + input + " (try: slkx channels --refresh)");
    const std::string uid = cache_["users"][handle].get<std::string>();
    if (!cache_["dms"].contains(uid))
      throw std::runtime_error("no existing DM with " + input +
                               "; open it once in Slack, then run: slkx channels --refresh");
    return cache_["dms"][uid].get<std::string>();
  }

  const std::string name = (input[0] == '#') ? input.substr(1) : input;
  if (cache_["channels"].contains(name)) return cache_["channels"][name].get<std::string>();

  // A brand new channel will not be in the cache yet.
  refresh();
  if (cache_["channels"].contains(name)) return cache_["channels"][name].get<std::string>();

  throw std::runtime_error("unknown channel: " + input);
}

std::string Resolver::name_for_id(const std::string& id) {
  ensure_loaded();
  return cache_["ids"].value(id, id);
}

std::string Resolver::display_name_for_user(const std::string& user_id) {
  if (user_id.empty()) return "(unknown)";
  ensure_loaded();
  return cache_["user_names"].value(user_id, user_id);
}

}  // namespace slk
