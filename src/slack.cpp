#include "slack.hpp"

#include "http.hpp"

namespace slk::slack {
namespace {
// ponytail: Enterprise Grid needs the host from auth.test's "url" field; add
// that when a grid workspace shows up.
constexpr const char* kBase = "https://slack.com/api/";
}

nlohmann::json Client::call(const std::string& method,
                            const std::map<std::string, std::string>& params) {
  // The web client puts the token in the body, never in an Authorization
  // header: a bearer header alongside browser-shaped headers is a contradictory
  // signature that trips Slack's anomaly detection.
  auto p = params;
  p["token"] = token_;
  auto resp = http::post_form(kBase + method, p, cookie_);

  // Not retrying here on purpose. When you do want backoff, this is the only
  // place that needs to change: sleep resp.retry_after seconds and re-issue.
  if (resp.status == 429) {
    throw ApiError("ratelimited", "rate limited by Slack; Retry-After: " +
                                      (resp.retry_after.empty() ? "unknown"
                                                                : resp.retry_after));
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(resp.body);
  } catch (const std::exception& e) {
    throw ApiError("bad_response", std::string("could not parse response from ") +
                                       method + ": " + e.what());
  }

  if (!j.value("ok", false)) {
    std::string code = j.value("error", "unknown_error");
    if (code == "invalid_auth" || code == "not_authed") {
      throw ApiError(code, method +
                               ": Slack rejected the session. The xoxc token or "
                               "d cookie has rotated -- re-copy both from the "
                               "Slack web client and update SLACK_CTOKEN / "
                               "SLACK_DTOKEN.");
    }
    std::string detail = method + ": " + code;
    if (j.contains("needed")) detail += " (needs scope: " + j["needed"].get<std::string>() + ")";
    throw ApiError(code, detail);
  }
  return j;
}

nlohmann::json Client::paginate(const std::string& method,
                                std::map<std::string, std::string> params,
                                const std::string& collection,
                                int page_limit) {
  nlohmann::json all = nlohmann::json::array();
  std::string cursor;

  for (int page = 0; page < page_limit; ++page) {
    if (!cursor.empty()) params["cursor"] = cursor;
    auto j = call(method, params);

    if (j.contains(collection) && j[collection].is_array()) {
      for (auto& item : j[collection]) all.push_back(item);
    }

    cursor.clear();
    if (j.contains("response_metadata")) {
      cursor = j["response_metadata"].value("next_cursor", "");
    }
    if (cursor.empty()) break;
  }
  return all;
}

}  // namespace slk::slack
