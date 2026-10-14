#pragma once
#include <nlohmann/json.hpp>

#include <map>
#include <stdexcept>
#include <string>

namespace slk::slack {

// Thrown when Slack replies with ok:false. `code` is Slack's error string
// (missing_scope, channel_not_found, not_in_channel, ratelimited, ...).
class ApiError : public std::runtime_error {
public:
  ApiError(std::string code, const std::string& detail)
      : std::runtime_error(detail), code(std::move(code)) {}
  std::string code;
};

class Client {
public:
  // token is the xoxc web-client token, cookie the raw xoxd `d` cookie value.
  Client(std::string token, std::string cookie)
      : token_(std::move(token)), cookie_(std::move(cookie)) {}

  // POSTs to https://slack.com/api/<method>. Throws ApiError on ok:false.
  nlohmann::json call(const std::string& method,
                      const std::map<std::string, std::string>& params = {});

  // Follows response_metadata.next_cursor, concatenating `collection`.
  // `page_limit` caps pages so a typo cannot walk a 10k-channel workspace.
  nlohmann::json paginate(const std::string& method,
                          std::map<std::string, std::string> params,
                          const std::string& collection,
                          int page_limit = 10);

  // The WebSocket handshake in src/rtm.cpp needs the same session cookie.
  const std::string& cookie() const { return cookie_; }

private:
  std::string token_;
  std::string cookie_;
};

}  // namespace slk::slack
