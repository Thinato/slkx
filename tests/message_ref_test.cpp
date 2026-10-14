// Self-check for parse_message_ref. A mis-parse fails silently -- it reads a
// real channel, just the wrong one -- so this is the one piece of logic here
// worth pinning down.

#include "message_ref.hpp"

#include <cassert>
#include <cstdio>

namespace {

void expect_throw(const std::string& input) {
  try {
    slk::parse_message_ref(input);
  } catch (const std::runtime_error&) {
    return;
  }
  std::fprintf(stderr, "expected a throw for: %s\n", input.c_str());
  assert(false);
}

}  // namespace

int main() {
  // Parent permalink: channel and ts from the path, both timestamps agreeing.
  {
    auto r = slk::parse_message_ref(
        "https://acme.slack.com/archives/C0123456789/p1788272945669409");
    assert(r.channel == "C0123456789");
    assert(r.ts == "1788272945.669409");
    assert(r.message_ts == "1788272945.669409");
  }

  // Reply permalink: the two timestamps diverge, and that divergence is the
  // whole point. `ts` opens the thread (read, click, send); `message_ts` is the
  // reply itself, which is what a reaction must land on.
  {
    auto r = slk::parse_message_ref(
        "https://acme.slack.com/archives/C0123456789/p1788273999000111"
        "?thread_ts=1788272945.669409&cid=C0123456789");
    assert(r.channel == "C0123456789");
    assert(r.ts == "1788272945.669409");
    assert(r.message_ts == "1788273999.000111");
  }

  // A garbage thread_ts does not override a good path ts.
  {
    auto r = slk::parse_message_ref(
        "https://x.slack.com/archives/C0123456789/p1788272945669409?thread_ts=nope");
    assert(r.ts == "1788272945.669409");
    assert(r.message_ts == "1788272945.669409");
  }

  // Bare timestamp: no channel, caller must supply --channel. Nothing to
  // disagree with, so both timestamps are the input.
  {
    auto r = slk::parse_message_ref("1788272945.669409");
    assert(r.channel.empty());
    assert(r.ts == "1788272945.669409");
    assert(r.message_ts == r.ts);
  }

  // Host is ignored: grid and custom domains parse the same.
  {
    auto r = slk::parse_message_ref(
        "https://acme.enterprise.slack.com/archives/C0987654321/p1788272945669409");
    assert(r.channel == "C0987654321");
  }

  // DM channels are just another id.
  {
    auto r = slk::parse_message_ref(
        "https://x.slack.com/archives/D0123456789/p1788182565850869");
    assert(r.channel == "D0123456789");
    assert(r.ts == "1788182565.850869");
  }

  expect_throw("https://x.slack.com/archives/C0123456789/pNOPE");
  expect_throw("https://x.slack.com/archives/C0123456789/p17882729456694");  // 14 digits
  expect_throw("https://x.slack.com/archives/C0123456789");                  // no message id
  expect_throw("1788272945");                                                // no .subsecond
  expect_throw("1788272945.6694");                                           // short subsecond
  expect_throw("#code-review");
  expect_throw("");

  std::puts("parse_message_ref: all checks passed");
  return 0;
}
