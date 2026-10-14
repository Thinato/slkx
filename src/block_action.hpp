#pragma once
// Finding the Block Kit button in a thread, and building the `actions` payload
// blocks.actions expects.
//
// Split out from commands.cpp so it can be checked offline: a button is
// one-shot per thread, so there is no room to get the payload wrong and retry.

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace slk {

struct ButtonRef {
  nlohmann::json action;   // button element with its block's block_id folded in
  std::string message_ts;  // container.message_ts
  std::string bot_id;      // -> service_id
  std::string app_id;      // -> app_id
  std::string team;        // -> service_team_id
  std::string label;       // human text, for the dry run
};

// Scans a conversations.replies `messages` array for live buttons. The bot
// replaces a message once its button is used, so anything found here is
// unclicked.
inline std::vector<ButtonRef> find_buttons(const nlohmann::json& messages) {
  std::vector<ButtonRef> found;
  if (!messages.is_array()) return found;

  for (const auto& m : messages) {
    if (!m.contains("blocks") || !m["blocks"].is_array()) continue;
    for (const auto& b : m["blocks"]) {
      if (b.value("type", "") != "actions") continue;
      if (!b.contains("elements") || !b["elements"].is_array()) continue;
      for (const auto& el : b["elements"]) {
        if (el.value("type", "") != "button") continue;
        nlohmann::json action = el;
        // block_id lives on the block, but blocks.actions wants it inside the
        // action object.
        action["block_id"] = b.value("block_id", "");
        std::string label;
        if (el.contains("text") && el["text"].is_object()) {
          label = el["text"].value("text", "");
        }
        found.push_back({action, m.value("ts", ""), m.value("bot_id", ""),
                         m.value("app_id", ""), m.value("team", ""), label});
      }
    }
  }
  return found;
}

}  // namespace slk
