#pragma once
// The two checks between a Slack file record and the local disk. Both inputs
// come from whoever uploaded the file, so both are self-checked in
// tests/filename_test.cpp. Header-only so the test links nothing.

#include <cctype>
#include <string>

namespace slk {

// True only for https://files.slack.com/... -- the one host that receives the
// session cookie. Parsed, not substring-matched: "files.slack.com.evil.io",
// "evil.io/?files.slack.com" and "files.slack.com@evil.io" all fail, because
// the authority must be exactly the host and end at '/', '?', '#' or the end.
inline bool is_slack_file_url(const std::string& url) {
  const std::string scheme = "https://";
  const std::string host = "files.slack.com";
  if (url.compare(0, scheme.size(), scheme) != 0) return false;
  const auto end = url.find_first_of("/?#", scheme.size());
  std::string authority = url.substr(scheme.size(), end - scheme.size());
  for (auto& c : authority) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return authority == host;
}

// Path separators and control characters (ESC included) become '_'. Used for
// the on-disk name and for anything from a file record echoed to a terminal.
inline std::string clean_name(const std::string& name) {
  std::string clean;
  for (unsigned char c : name) {
    clean += (c == '/' || c == '\\' || c < 0x20 || c == 0x7f) ? '_' : static_cast<char>(c);
  }
  return clean;
}

// "<id>-<name>". The id makes names unique (three screenshots in one channel
// were all "image.png"); the name keeps them readable. Path separators and
// control characters become '_', so nothing can leave the output directory.
// Returns "" for an id that is not plain alphanumeric: the caller skips it
// rather than build a path from something unexpected.
inline std::string safe_filename(const std::string& id, const std::string& name) {
  if (id.empty()) return "";
  for (unsigned char c : id) {
    if (!std::isalnum(c)) return "";
  }
  const std::string clean = clean_name(name);
  return clean.empty() ? id : id + "-" + clean;
}

}  // namespace slk
