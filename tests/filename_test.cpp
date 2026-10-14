// Self-check for the file-download trust boundary: which URLs get the session
// cookie, and what a remote filename may turn into on disk.

#include "filename.hpp"

#include <cassert>
#include <cstdio>

int main() {
  using slk::is_slack_file_url;
  using slk::safe_filename;

  // The real shape of a Slack-hosted file URL.
  assert(is_slack_file_url("https://files.slack.com/files-pri/T04T-F0B/download/image.png"));
  assert(is_slack_file_url("https://files.slack.com"));
  assert(is_slack_file_url("https://FILES.SLACK.COM/x"));
  assert(is_slack_file_url("https://files.slack.com?x=1"));

  // Everything else keeps the cookie.
  assert(!is_slack_file_url("http://files.slack.com/x"));                 // no TLS
  assert(!is_slack_file_url("https://files.slack.com.evil.io/x"));        // suffix
  assert(!is_slack_file_url("https://evil.io/files.slack.com"));          // in the path
  assert(!is_slack_file_url("https://evil.io/?u=https://files.slack.com"));  // in the query
  assert(!is_slack_file_url("https://files.slack.com@evil.io/x"));        // userinfo
  assert(!is_slack_file_url("https://evil.io@files.slack.com/x"));        // userinfo, reversed
  assert(!is_slack_file_url("https://files.slack.com:8443/x"));           // port
  assert(!is_slack_file_url("https://files.slack.com\\@evil.io/x"));      // backslash trick
  assert(!is_slack_file_url("https://docs.google.com/document/d/1"));     // external file
  assert(!is_slack_file_url("https://slack.com/x"));
  assert(!is_slack_file_url(""));

  // Names: prefixed, and unable to leave the output directory.
  assert(safe_filename("F0B1", "image.png") == "F0B1-image.png");
  assert(safe_filename("F0B1", "Captura de Tela 2026-09-29 às 15.44.32.png") ==
         "F0B1-Captura de Tela 2026-09-29 às 15.44.32.png");       // unicode kept
  assert(safe_filename("F0B1", "../../.ssh/id_rsa") == "F0B1-.._.._.ssh_id_rsa");
  assert(safe_filename("F0B1", "..\\..\\x") == "F0B1-.._.._x");
  assert(safe_filename("F0B1", "a\nb\tc") == "F0B1-a_b_c");
  assert(safe_filename("F0B1", "..") == "F0B1-..");                // a name, not a parent dir
  assert(safe_filename("F0B1", "") == "F0B1");
  assert(safe_filename("", "x.png").empty());
  assert(safe_filename("../F", "x.png").empty());
  assert(safe_filename("F/1", "x.png").empty());

  // What reaches the terminal: no escape sequences from an uploader's name.
  assert(slk::clean_name("\x1b]0;pwned\x07x.png") == "_]0;pwned_x.png");

  std::puts("filename: all checks passed");
  return 0;
}
