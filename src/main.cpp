#include <iostream>

#include "args.hpp"
#include "commands.hpp"
#include "http.hpp"
#include "slack.hpp"

namespace {

void usage() {
  std::cerr <<
      R"(slkx - slack from the shell

usage: slkx <command> [options]

commands
  auth status                    show who the stored token belongs to
  auth set --token xoxc-... --cookie xoxd-...
                                 store a session for the current profile

  send --channel C --message M   post a message (reads stdin if --message absent)
  send --thread <permalink|ts>   reply in a thread (--channel only for a bare ts)
  edit --message <permalink|ts> --text T
                                 replace the text of one of your messages
                                 (reads stdin if --text absent)
  react --message <permalink|ts> --emoji eyes [--remove]
                                 add, or remove, an emoji reaction

  read --channel C [--limit 20] [--since TS] [--with-replies]
                                 recent messages, oldest first. --since keeps
                                 newer messages and threads with newer replies;
                                 --with-replies includes those replies too
  read --thread <permalink|ts> [--since TS]
                                 a message and its thread; --since keeps newer
                                 replies only
  files --message <permalink|ts> [--out DIR]
                                 save a message's attachments, print their paths

  click --thread <permalink|ts>  press a Block Kit button (needs --yes to fire)
  watch                          stream live RTM events as JSONL on stdout
  keepalive [--channel C] [--every 3600]
                                 read C (default general) every N seconds so
                                 Slack does not expire an idle session
  channels [--refresh]           list cached channel names and ids
  users [--refresh]              list cached user handles and ids
  notifications                  conversations with unread messages

arguments
  C                  a channel: '#name', name, id, or '@handle' for a DM
  <permalink|ts>     Copy link in Slack, or a timestamp like 1788272945.669409.
                     A permalink carries its channel; a bare ts needs --channel C.
                     A link to a reply means the whole thread for read, click
                     and send, but that exact reply for edit, react and files.
  TS (--since)       a timestamp or permalink; only what came after it

global options
  --workspace NAME   profile to use (env SLKX_WORKSPACE, default "default")
  --token TOKEN      xoxc web-client token (env SLACK_CTOKEN)
  --cookie COOKIE    xoxd session cookie (env SLACK_DTOKEN)
  --json             machine-readable output on stdout: the raw Slack payload.
                     read adds user_name to messages and, with --with-replies,
                     a replies array on each thread parent; oldest first,
                     like the text output

examples
  slkx send --channel '#general' --message 'deploy finished'
  make 2>&1 | slkx send --channel '@alice'
  slkx read --channel '#incidents' --limit 50 --json | jq -r '.[].text'
  slkx read --thread 'https://acme.slack.com/archives/C123/p1788272945669409'
  slkx read --channel '#status' --since 1788272945.669409 --with-replies --json
  slkx send --thread 'https://acme.slack.com/archives/C123/p1788272945669409' --message 'on it'
  slkx edit --channel '#status' --message 1788272945.669409 --text 'updated roll-up'
  slkx react --message 'https://acme.slack.com/archives/C123/p1788272945669409' --emoji eyes
  slkx files --message 'https://acme.slack.com/archives/C123/p1788272945669409' --out ./shots
  slkx watch | jq -c 'select(.type=="reaction_added")'
)";
}

}  // namespace

int main(int argc, char** argv) {
  slk::Args args(argc, argv);

  if (args.command().empty() || args.command() == "help" || args.has("help")) {
    usage();
    return args.command().empty() ? 2 : 0;
  }

  slk::http::global_init();
  int rc = 0;

  try {
    const std::string& cmd = args.command();
    if (cmd == "auth") {
      rc = slk::commands::auth(args);
    } else if (cmd == "send") {
      rc = slk::commands::send(args);
    } else if (cmd == "edit") {
      rc = slk::commands::edit(args);
    } else if (cmd == "files") {
      rc = slk::commands::files(args);
    } else if (cmd == "react") {
      rc = slk::commands::react(args);
    } else if (cmd == "read") {
      rc = slk::commands::read(args);
    } else if (cmd == "click") {
      rc = slk::commands::click(args);
    } else if (cmd == "watch") {
      rc = slk::commands::watch(args);
    } else if (cmd == "keepalive") {
      rc = slk::commands::keepalive(args);
    } else if (cmd == "channels") {
      rc = slk::commands::channels(args);
    } else if (cmd == "users") {
      rc = slk::commands::users(args);
    } else if (cmd == "notifications") {
      rc = slk::commands::notifications(args);
    } else {
      std::cerr << "unknown command: " << cmd << "\n\n";
      usage();
      rc = 2;
    }
  } catch (const slk::slack::ApiError& e) {
    std::cerr << "slack error: " << e.what() << "\n";
    rc = (e.code == "ratelimited") ? 4 : 3;
  } catch (const slk::http::TransportError& e) {
    std::cerr << "network error: " << e.what() << "\n";
    rc = 5;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    rc = 1;
  }

  slk::http::global_cleanup();
  return rc;
}
