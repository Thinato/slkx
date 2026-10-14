#include "rtm.hpp"

#include <ixwebsocket/IXWebSocket.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "browser.hpp"

namespace slk::rtm {
namespace {

// 1, 2, 4, 8, 16, then 30s forever. Reconnects re-hit rtm.connect, which is
// rate limited, so this must not spin.
int backoff_seconds(int attempt) {
  if (attempt <= 0) return 1;
  if (attempt >= 5) return 30;
  return 1 << attempt;
}

}  // namespace

void stream(slack::Client& client, const FrameFn& on_frame, const std::atomic<bool>& stop) {
  int attempt = 0;

  while (!stop) {
    // Slack expires an unused rtm.connect URL in ~30s, so fetch it immediately
    // before starting the socket and never cache it.
    auto info = client.call("rtm.connect");
    const std::string url = info.value("url", "");
    if (url.empty()) throw slack::ApiError("bad_response", "rtm.connect returned no url");

    ix::WebSocket ws;
    ws.setUrl(url);

    ix::WebSocketHttpHeaders headers;
    headers["Cookie"] = "d=" + client.cookie();
    headers["Origin"] = kOrigin;
    headers["User-Agent"] = kUserAgent;
    ws.setExtraHeaders(headers);

    // IXWebSocket's own reconnect would retry this same, by then expired, URL.
    ws.disableAutomaticReconnection();
    // Matches the web client's measured 10s cadence; the library's pong timeout
    // then surfaces a silently dead socket as a Close.
    // ponytail: WS-level ping. Switch to app-level {"type":"ping"} with pong
    // tracking if Slack ever stops answering these.
    ws.setPingInterval(10);

    std::atomic<bool> down{false};
    ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
      switch (msg->type) {
        case ix::WebSocketMessageType::Message:
          if (is_emittable(msg->str)) {
            on_frame(msg->str);
          } else {
            std::cerr << "slkx watch: dropped an unprintable frame ("
                      << msg->str.size() << " bytes)\n";
          }
          break;
        case ix::WebSocketMessageType::Open:
          std::cerr << "slkx watch: connected\n";
          attempt = 0;
          break;
        case ix::WebSocketMessageType::Close:
          std::cerr << "slkx watch: socket closed (" << msg->closeInfo.code << " "
                    << msg->closeInfo.reason << ")\n";
          down = true;
          break;
        case ix::WebSocketMessageType::Error:
          std::cerr << "slkx watch: socket error: " << msg->errorInfo.reason << "\n";
          down = true;
          break;
        default:
          break;
      }
    });

    ws.start();
    while (!stop && !down) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ws.stop();
    if (stop) break;

    const int wait = backoff_seconds(attempt++);
    std::cerr << "slkx watch: reconnecting in " << wait << "s\n";
    for (int i = 0; i < wait * 5 && !stop; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
}

}  // namespace slk::rtm
