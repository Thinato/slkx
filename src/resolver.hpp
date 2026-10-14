#pragma once
#include <nlohmann/json.hpp>

#include <string>

#include "slack.hpp"

namespace slk {

// Turns human input into a Slack conversation ID, using an on-disk cache so
// the common path costs zero API calls.
//
//   "#general" / "general"  -> C0000000000
//   "@alice"                -> D0000000000  (existing DM only)
//   "C0000000000"           -> passthrough
class Resolver {
public:
  Resolver(slack::Client& client, std::string profile);

  std::string to_id(const std::string& input);
  std::string name_for_id(const std::string& id);
  std::string display_name_for_user(const std::string& user_id);

  void refresh();  // re-fetch conversations + users, rewrite the cache

private:
  void ensure_loaded();

  slack::Client& client_;
  std::string profile_;
  nlohmann::json cache_;  // { channels: {name:id}, ids: {id:name}, users: {...}, dms: {...} }
  bool loaded_ = false;
};

}  // namespace slk
