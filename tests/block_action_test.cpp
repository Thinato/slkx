// Checks find_buttons against a real payload captured from the Slack web
// client. A button is one-shot per thread, so the action object has to be
// right the first time.

#include "block_action.hpp"

#include <cassert>
#include <cstdio>

int main() {
  // A deploy bot's "are you available for deploy?" reply, shaped as
  // conversations.replies returns it.
  const auto messages = nlohmann::json::parse(R"([
    {"ts":"1788276288.369249","user":"U0000000001","text":"a PR link"},
    {"ts":"1788276337.050999","user":"U0000000002","bot_id":"B0000000001",
     "app_id":"A0000000001","team":"T0000000001",
     "blocks":[
       {"type":"rich_text","block_id":"wTO","elements":[]},
       {"type":"actions","block_id":"5Ynq6","elements":[
         {"type":"button",
          "text":{"type":"plain_text","text":":white_check_mark: Yes","emoji":true},
          "value":"yes","style":"primary","action_id":"dev_available_yes"}]}]}
  ])");

  const auto found = slk::find_buttons(messages);
  assert(found.size() == 1);

  const auto& b = found.front();
  assert(b.message_ts == "1788276337.050999");
  assert(b.bot_id == "B0000000001");   // -> service_id
  assert(b.app_id == "A0000000001");
  assert(b.team == "T0000000001");     // -> service_team_id
  assert(b.label == ":white_check_mark: Yes");

  // The assembled action must match what the browser sent, byte for byte
  // (nlohmann compares by content, so key order does not matter).
  const auto captured = nlohmann::json::parse(
      R"({"action_id":"dev_available_yes","block_id":"5Ynq6",)"
      R"("text":{"type":"plain_text","text":":white_check_mark: Yes","emoji":true},)"
      R"("value":"yes","style":"primary","type":"button"})");
  assert(b.action == captured);

  // A spent thread: the bot replaced the message, no actions block left.
  {
    const auto spent = nlohmann::json::parse(R"([
      {"ts":"1788276337.050999","bot_id":"B0000000001",
       "blocks":[{"type":"rich_text","block_id":"wTO","elements":[]}]}
    ])");
    assert(slk::find_buttons(spent).empty());
  }

  // Two buttons in one block -> both offered, so the caller can require --action.
  {
    const auto pair = nlohmann::json::parse(R"([
      {"ts":"1.1","blocks":[{"type":"actions","block_id":"blk","elements":[
        {"type":"button","action_id":"yes","text":{"type":"plain_text","text":"Yes"}},
        {"type":"button","action_id":"no","text":{"type":"plain_text","text":"No"}}]}]}
    ])");
    const auto two = slk::find_buttons(pair);
    assert(two.size() == 2);
    assert(two[0].action.value("action_id", "") == "yes");
    assert(two[1].action.value("block_id", "") == "blk");
  }

  // Malformed shapes must not throw.
  assert(slk::find_buttons(nlohmann::json::parse("{}")).empty());
  assert(slk::find_buttons(nlohmann::json::parse(R"([{"blocks":"nope"}])")).empty());
  assert(slk::find_buttons(nlohmann::json::parse(
             R"([{"blocks":[{"type":"actions"}]}])")).empty());

  std::puts("find_buttons: all checks passed");
  return 0;
}
