// Self-check for the frame guard. A frame with an embedded newline would split
// into two lines and silently corrupt a line-oriented consumer, so it must be
// dropped rather than emitted.

#include <cassert>
#include <cstdio>
#include <string>

// rtm.hpp pulls in slack.hpp -> nlohmann; is_emittable is pure string work, so
// declare the contract locally and include the header for the real definition.
#include "rtm.hpp"

int main() {
  using slk::rtm::is_emittable;

  // What Slack actually sends: compact, single line.
  assert(is_emittable(R"({"type":"hello","fast_reconnect":false})"));
  assert(is_emittable(
      R"({"type":"reaction_added","reaction":"eyes","item":{"channel":"C0123456789"}})"));

  // An escaped newline inside a string is fine -- it is two characters, not a
  // line break, and message text is full of them.
  assert(is_emittable(R"({"type":"message","text":"line one\nline two"})"));

  // A literal line break in the frame is not.
  assert(!is_emittable("{\"type\":\"message\",\n \"text\":\"x\"}"));
  assert(!is_emittable("{\"a\":1}\r\n{\"b\":2}"));
  assert(!is_emittable("{\"a\":1}\n"));
  assert(!is_emittable(""));

  std::puts("is_emittable: all checks passed");
  return 0;
}
