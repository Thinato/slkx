#pragma once
#include <map>
#include <stdexcept>
#include <string>

namespace slk::http {

struct Response {
  long status = 0;
  std::string body;
  std::string retry_after;  // populated on 429
};

class TransportError : public std::runtime_error {
public:
  explicit TransportError(const std::string& what) : std::runtime_error(what) {}
};

// Call once at startup / shutdown.
void global_init();
void global_cleanup();

// application/x-www-form-urlencoded POST shaped like a Slack web client XHR:
// the `d` session cookie plus Chrome's header set. `cookie` is the raw xoxd
// value and is sent verbatim -- it arrives already percent-encoded.
Response post_form(const std::string& url,
                   const std::map<std::string, std::string>& form,
                   const std::string& cookie);

// GET with the same cookie and headers. Never follows redirects: files.slack.com
// answers an expired session with a 302 to the login page, and following it
// would replay the cookie and hand back HTML as if it were the file. Callers
// must treat any status but 200 as failure.
// ponytail: body buffered in memory. Fine for screenshots; stream to disk when
// video attachments matter.
Response get(const std::string& url, const std::string& cookie);

}  // namespace slk::http
