#pragma once
// Parsing a Slack message reference. Header-only and dependency-free so the
// self-check in tests/ links nothing.
//
// A Slack message has no standalone id: its identity is the pair
// (channel, ts). A permalink is the only single string that carries both --
// which is why `read --thread` accepts one.

#include <cctype>
#include <stdexcept>
#include <string>

namespace slk {

struct MessageRef {
  std::string channel;     // empty when the input was a bare ts
  std::string ts;          // the thread parent when the link carries thread_ts
  std::string message_ts;  // always the message the reference points at
};

namespace detail {

inline bool all_digits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

// "1788272945.669409" -- Slack timestamps are always 10 digits, a dot, then 6.
inline bool looks_like_ts(const std::string& s) {
  const auto dot = s.find('.');
  if (dot != 10 || s.size() != 17) return false;
  return all_digits(s.substr(0, 10)) && all_digits(s.substr(11));
}

// "p1788272945669409" -> "1788272945.669409". Exactly 16 digits or nothing:
// guessing at a different length would silently read the wrong message.
inline std::string ts_from_path_segment(const std::string& seg) {
  if (seg.size() != 17 || seg[0] != 'p') return "";
  const std::string digits = seg.substr(1);
  if (!all_digits(digits)) return "";
  return digits.substr(0, 10) + "." + digits.substr(10);
}

// Pulls one parameter out of a query string, "" when absent.
inline std::string query_param(const std::string& query, const std::string& key) {
  const std::string needle = key + "=";
  size_t pos = 0;
  while (pos < query.size()) {
    size_t end = query.find('&', pos);
    if (end == std::string::npos) end = query.size();
    const std::string pair = query.substr(pos, end - pos);
    if (pair.rfind(needle, 0) == 0) return pair.substr(needle.size());
    pos = end + 1;
  }
  return "";
}

}  // namespace detail

// Accepts a Slack permalink:
//   https://<team>.slack.com/archives/C0123456789/p1788272945669409?thread_ts=...
// or a bare timestamp:
//   1788272945.669409
// The host is ignored, so Enterprise Grid and custom domains parse fine.
// Throws std::runtime_error on anything else.
inline MessageRef parse_message_ref(const std::string& input) {
  const std::string kArchives = "/archives/";
  const auto at = input.find(kArchives);
  if (at == std::string::npos) {
    if (!detail::looks_like_ts(input)) {
      throw std::runtime_error(
          "not a message reference: '" + input +
          "'. Expected a Slack permalink (Copy link in Slack) or a timestamp "
          "like 1788272945.669409");
    }
    return {"", input, input};  // nothing to disagree with
  }

  std::string rest = input.substr(at + kArchives.size());
  std::string query;
  if (const auto q = rest.find('?'); q != std::string::npos) {
    query = rest.substr(q + 1);
    rest = rest.substr(0, q);
  }

  const auto slash = rest.find('/');
  if (slash == std::string::npos) {
    throw std::runtime_error("permalink is missing its message id: " + input);
  }
  MessageRef ref;
  ref.channel = rest.substr(0, slash);
  ref.message_ts = detail::ts_from_path_segment(rest.substr(slash + 1));
  ref.ts = ref.message_ts;

  // A permalink to a reply carries the parent in thread_ts. Preferring it for
  // `ts` means a link to any message in a thread opens the whole thread, which
  // is what read/click/send want. `message_ts` stays pinned to the message
  // itself -- reacting to the parent instead would be a silent wrong answer.
  if (const std::string parent = detail::query_param(query, "thread_ts");
      detail::looks_like_ts(parent)) {
    ref.ts = parent;
  }

  if (ref.channel.empty() || ref.message_ts.empty()) {
    throw std::runtime_error("could not parse permalink: " + input);
  }
  return ref;
}

}  // namespace slk
