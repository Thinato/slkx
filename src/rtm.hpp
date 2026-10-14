#pragma once
// Slack's real-time socket.
//
// rtm.connect hands back a wss:// URL, and the handshake MUST carry the `d`
// session cookie: without it Slack completes the upgrade, then sends
// {"type":"error","error":{"msg":"invalid_auth","code":401}} and closes. That
// failure looks exactly like "RTM is dead for user tokens" -- it is not.

#include <atomic>
#include <functional>
#include <string>

#include "slack.hpp"

namespace slk::rtm {

// Slack sends compact single-line JSON (verified across 178 captured frames).
// A frame carrying a literal newline would silently corrupt a line-oriented
// consumer, so it is dropped rather than emitted broken.
inline bool is_emittable(const std::string& frame) {
  return !frame.empty() && frame.find('\n') == std::string::npos &&
         frame.find('\r') == std::string::npos;
}

using FrameFn = std::function<void(const std::string&)>;

// Opens the socket and hands every text frame to `on_frame`. Blocks until
// `stop` turns true, reconnecting with backoff whenever the socket drops.
// Propagates slack::ApiError from rtm.connect (invalid_auth -> exit 3).
void stream(slack::Client& client, const FrameFn& on_frame, const std::atomic<bool>& stop);

}  // namespace slk::rtm
