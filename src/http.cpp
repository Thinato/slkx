#include "http.hpp"

#include <curl/curl.h>

#include "browser.hpp"

#include <algorithm>
#include <cctype>
#include <memory>

namespace slk::http {
namespace {

size_t write_body(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

size_t read_header(char* buffer, size_t size, size_t nitems, void* userdata) {
  auto* resp = static_cast<Response*>(userdata);
  std::string line(buffer, size * nitems);
  std::string lower = line;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  const std::string key = "retry-after:";
  if (lower.rfind(key, 0) == 0) {
    std::string v = line.substr(key.size());
    // trim
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    v.erase(v.begin(), std::find_if(v.begin(), v.end(), not_space));
    v.erase(std::find_if(v.rbegin(), v.rend(), not_space).base(), v.end());
    resp->retry_after = v;
  }
  return size * nitems;
}

struct CurlDeleter {
  void operator()(CURL* h) const { curl_easy_cleanup(h); }
};
using CurlHandle = std::unique_ptr<CURL, CurlDeleter>;

std::string url_encode(CURL* h, const std::string& s) {
  char* esc = curl_easy_escape(h, s.c_str(), static_cast<int>(s.size()));
  std::string out = esc ? esc : "";
  curl_free(esc);
  return out;
}

// The headers a Chrome tab sends on a same-site XHR to Slack, copied from
// gammons/slk internal/slackhttp/transport.go browserHeaderPairs().
//
// Deliberately no Referer: the real web client sends none on /api/ calls, and
// slk measured that adding one made their traffic separable from it.
//
// The User-Agent and Origin live in browser.hpp: src/rtm.cpp sends the same
// pair on the WebSocket handshake.
constexpr const char* kBrowserHeaders[] = {
    "Accept: */*",
    "Accept-Language: en-US,en;q=0.9",
    "Sec-Fetch-Site: same-site",
    "Sec-Fetch-Mode: cors",
    "Sec-Fetch-Dest: empty",
    R"(Sec-Ch-Ua: "Chromium";v="150", "Not:A-Brand";v="24", "Google Chrome";v="150")",
    "Sec-Ch-Ua-Mobile: ?0",
    R"(Sec-Ch-Ua-Platform: "macOS")",
    "Cache-Control: no-cache",
    "Pragma: no-cache",
    "Priority: u=1, i",
};

}  // namespace

void global_init() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void global_cleanup() { curl_global_cleanup(); }

namespace {

// Everything but the verb: browser headers, the d cookie, body capture and the
// redirect guard. Both verbs come through here so the guard cannot be dropped
// from one of them. Takes ownership of `headers`.
Response perform(CURL* h, const std::string& url, struct curl_slist* headers,
                 const std::string& cookie) {
  for (const char* hdr : kBrowserHeaders) headers = curl_slist_append(headers, hdr);
  const std::string ua = std::string("User-Agent: ") + kUserAgent;
  const std::string origin = std::string("Origin: ") + kOrigin;
  headers = curl_slist_append(headers, ua.c_str());
  headers = curl_slist_append(headers, origin.c_str());

  // The xoxd value is already percent-encoded; url_encode here would double
  // encode it and Slack would answer invalid_auth.
  const std::string cookie_header = "d=" + cookie;

  Response resp;
  curl_easy_setopt(h, CURLOPT_URL, url.c_str());
  curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
  if (!cookie.empty()) curl_easy_setopt(h, CURLOPT_COOKIE, cookie_header.c_str());
  curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_body);
  curl_easy_setopt(h, CURLOPT_WRITEDATA, &resp.body);
  curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, read_header);
  curl_easy_setopt(h, CURLOPT_HEADERDATA, &resp);
  // Off on purpose: CURLOPT_COOKIE is replayed on every redirect hop, so
  // following one off slack.com would hand the session cookie to whatever host
  // the Location names. The Slack API does not redirect, and files.slack.com
  // only redirects an expired session to the login page.
  curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 0L);

  CURLcode rc = curl_easy_perform(h);
  curl_slist_free_all(headers);
  if (rc != CURLE_OK) throw TransportError(curl_easy_strerror(rc));

  curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &resp.status);
  return resp;
}

}  // namespace

Response post_form(const std::string& url,
                   const std::map<std::string, std::string>& form,
                   const std::string& cookie) {
  CurlHandle handle(curl_easy_init());
  if (!handle) throw TransportError("curl_easy_init failed");
  CURL* h = handle.get();

  std::string payload;
  for (const auto& [k, v] : form) {
    if (!payload.empty()) payload += '&';
    payload += url_encode(h, k) + "=" + url_encode(h, v);
  }

  curl_easy_setopt(h, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
  curl_easy_setopt(h, CURLOPT_TIMEOUT, 30L);
  return perform(h, url,
                 curl_slist_append(nullptr, "Content-Type: application/x-www-form-urlencoded; charset=utf-8"),
                 cookie);
}

Response get(const std::string& url, const std::string& cookie) {
  CurlHandle handle(curl_easy_init());
  if (!handle) throw TransportError("curl_easy_init failed");
  CURL* h = handle.get();

  // No total timeout: a large attachment is slow, not stuck. Abort only when
  // the transfer stalls below 1 KB/s for 30s.
  curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1024L);
  curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 30L);
  return perform(h, url, nullptr, cookie);
}

}  // namespace slk::http
