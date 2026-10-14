#pragma once
// The browser identity slkx presents to Slack. Shared by the HTTP transport
// (src/http.cpp) and the WebSocket handshake (src/rtm.cpp) so the two cannot
// drift into a contradictory signature.
//
// ponytail: Chrome 150 / macOS hardcoded. Bump the major and the platform hint
// if Slack starts rejecting the shape.

namespace slk {

inline constexpr const char* kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36";

inline constexpr const char* kOrigin = "https://app.slack.com";

}  // namespace slk
