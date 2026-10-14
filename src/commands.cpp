#include "commands.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <csignal>
#include <filesystem>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include "config.hpp"
#include "block_action.hpp"
#include "filename.hpp"
#include "http.hpp"
#include "message_ref.hpp"
#include "resolver.hpp"
#include "rtm.hpp"
#include "slack.hpp"

namespace slk::commands {
namespace {

// Resolver holds a reference to the Client, so Session owns both and must not
// be copied or moved: member declaration order guarantees `client` outlives the
// reference inside `resolver`.
class Session {
public:
  explicit Session(const Args& args)
      : profile(load_profile(resolve_profile_name(args.value_or("workspace", "")),
                             args.value_or("token", ""),
                             args.value_or("cookie", ""))),
        client(profile.token, profile.cookie),
        resolver(client, profile.name) {}

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  Profile profile;
  slack::Client client;
  Resolver resolver;
};

bool want_json(const Args& args) { return args.has("json"); }

std::string read_stdin() {
  std::ostringstream ss;
  ss << std::cin.rdbuf();
  std::string s = ss.str();
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s;
}

// Slack timestamps look like "1717171717.123456".
std::string format_ts(const std::string& ts) {
  const auto dot = ts.find('.');
  std::time_t secs = 0;
  try {
    secs = static_cast<std::time_t>(std::stoll(ts.substr(0, dot)));
  } catch (...) {
    return ts;
  }
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &secs);
#else
  localtime_r(&secs, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
  return buf;
}

// Not only threads: `react` targets one message, so both timestamps travel
// together and each caller picks the one it means.
struct MessageTarget {
  std::string channel_id;
  std::string ts;          // the thread parent: read, click, send
  std::string message_ts;  // the message itself: react
};

// A permalink carries its own channel; a bare ts needs --channel. Prints the
// reason and returns nullopt, so callers just `return 2`.
std::optional<MessageTarget> resolve_thread(Session& s,
                                           const std::optional<std::string>& channel_flag,
                                           const std::string& thread_ref) {
  MessageRef ref;
  try {
    ref = parse_message_ref(thread_ref);
  } catch (const std::runtime_error& e) {
    std::cerr << e.what() << "\n";
    return std::nullopt;
  }
  if (!ref.channel.empty()) return MessageTarget{ref.channel, ref.ts, ref.message_ts};
  if (channel_flag) {
    return MessageTarget{s.resolver.to_id(*channel_flag), ref.ts, ref.message_ts};
  }
  std::cerr << "a bare timestamp needs --channel. Or pass the message "
               "permalink (Copy link in Slack), which carries the channel.\n";
  return std::nullopt;
}

}  // namespace

int auth(const Args& args) {
  const std::string sub = args.positional().empty() ? "status" : args.positional()[0];
  const std::string name = resolve_profile_name(args.value_or("workspace", ""));

  if (sub == "set") {
    auto token = args.value("token");
    auto cookie = args.value("cookie");
    if (!token || !cookie) {
      std::cerr << "usage: slkx auth set --token xoxc-... --cookie xoxd-...\n";
      return 2;
    }
    slack::Client probe(*token, *cookie);
    auto who = probe.call("auth.test");
    Profile p;
    p.name = name;
    p.token = *token;
    p.cookie = *cookie;
    p.team = who.value("team", "");
    save_profile(p);
    std::cout << "saved profile '" << name << "' for " << p.team << " as "
              << who.value("user", "?") << "\n";
    std::cout << profile_path(name).string() << "\n";
    return 0;
  }

  if (sub != "status") {
    std::cerr << "unknown subcommand: auth " << sub << "\n";
    return 2;
  }

  Session s(args);
  auto who = s.client.call("auth.test");
  if (want_json(args)) {
    std::cout << who.dump(2) << "\n";
  } else {
    std::cout << "profile:   " << s.profile.name << "\n"
              << "workspace: " << who.value("team", "?") << "\n"
              << "user:      " << who.value("user", "?") << " ("
              << who.value("user_id", "?") << ")\n";
  }
  return 0;
}

int send(const Args& args) {
  auto channel_flag = args.value("channel");
  if (!channel_flag && !args.positional().empty()) channel_flag = args.positional()[0];
  auto thread_flag = args.value("thread");
  // A thread permalink carries its own channel, so --channel is optional then.
  if (!channel_flag && !thread_flag) {
    std::cerr << "usage: slkx send --channel '#general' --message 'hi'\n"
                 "       slkx send --thread <permalink|ts> --message 'on it'\n"
                 "       echo 'hi' | slkx send --channel '#general'\n";
    return 2;
  }

  std::string text;
  if (auto m = args.value("message")) {
    text = *m;
  } else {
    text = read_stdin();
  }
  if (text.empty()) {
    std::cerr << "refusing to send an empty message\n";
    return 2;
  }

  Session s(args);

  std::string id;
  std::optional<std::string> thread_ts;
  if (thread_flag) {
    auto target = resolve_thread(s, channel_flag, *thread_flag);
    if (!target) return 2;
    id = target->channel_id;
    // The parent, not the linked message: a reply belongs to the thread, and
    // Slack re-parents a reply ts here anyway.
    thread_ts = target->ts;
  } else {
    id = s.resolver.to_id(*channel_flag);
  }

  std::map<std::string, std::string> params{{"channel", id}, {"text", text}};
  if (thread_ts) params["thread_ts"] = *thread_ts;
  // A user token posts as you, so mrkdwn/link parsing should match the app.
  params["link_names"] = "true";

  auto res = s.client.call("chat.postMessage", params);

  if (want_json(args)) {
    std::cout << res.dump() << "\n";
  } else {
    std::cout << "sent to " << s.resolver.name_for_id(id) << " ("
              << res.value("ts", "?") << ")\n";
  }
  return 0;
}

// Targets the message the reference points at, not its thread parent -- see the
// message_ts/ts split in src/message_ref.hpp. Using `ts` here would put the
// reaction on the wrong message whenever you pass a link to a reply, and
// nothing would report an error.
int react(const Args& args) {
  auto msg_flag = args.value("message");
  auto emoji = args.value("emoji");
  if (!msg_flag || !emoji) {
    std::cerr << "usage: slkx react --message <permalink|ts> --emoji eyes "
                 "[--channel C] [--remove]\n";
    return 2;
  }

  // Slack wants the bare name; ":eyes:" is what its own UI hands you.
  std::string name = *emoji;
  if (name.size() > 2 && name.front() == ':' && name.back() == ':') {
    name = name.substr(1, name.size() - 2);
  }

  Session s(args);
  auto target = resolve_thread(s, args.value("channel"), *msg_flag);
  if (!target) return 2;

  const bool remove = args.has("remove");
  try {
    s.client.call(remove ? "reactions.remove" : "reactions.add",
                  {{"channel", target->channel_id},
                   {"timestamp", target->message_ts},
                   {"name", name}});
  } catch (const slack::ApiError& e) {
    // Already in the requested state is the goal, not a failure: exiting 3
    // would make every re-run of a script look broken.
    if (e.code != (remove ? "no_reaction" : "already_reacted")) throw;
    std::cerr << ":" << name << (remove ? ": was not there\n" : ": was already there\n");
    return 0;
  }

  std::cout << ":" << name << (remove ? ": removed from " : ": on ") << target->message_ts
            << " in " << s.resolver.name_for_id(target->channel_id) << "\n";
  return 0;
}

namespace {

// Shared by the history and thread paths. `show_reply_counts` is off for a
// thread: the replies are already on screen, so pointing at them is noise.
// Replies fetched by --with-replies ride on their parent and print under it.
void print_messages(const nlohmann::json& messages, Session& s, bool show_reply_counts,
                    const std::string& indent = "") {
  for (const auto& m : messages) {
    const std::string author =
        m.contains("user") ? s.resolver.display_name_for_user(m.value("user", ""))
                           : m.value("username", "bot");
    std::cout << indent << "[" << format_ts(m.value("ts", "")) << "] " << author << ": "
              << m.value("text", "") << "\n";
    if (show_reply_counts && m.value("reply_count", 0) > 0) {
      std::cout << "    +" << m.value("reply_count", 0) << " replies (thread_ts "
                << m.value("ts", "") << ")\n";
    }
    if (m.contains("replies")) print_messages(m["replies"], s, false, "    ");
  }
}

// Slack ts strings are fixed width (10.6, enforced by parse_message_ref), so
// string order is time order. `count_replies` lets an old thread parent through
// when its latest reply is new -- how a channel poll notices thread activity.
nlohmann::json keep_newer(const nlohmann::json& messages, const std::string& since,
                          bool count_replies) {
  if (since.empty()) return messages;
  auto kept = nlohmann::json::array();
  for (const auto& m : messages) {
    if (m.value("ts", "") > since ||
        (count_replies && m.value("latest_reply", "") > since)) {
      kept.push_back(m);
    }
  }
  return kept;
}

// The text output shows names; --json gets them too. Only names the cache
// knows: display_name_for_user falls back to the id, and a name field holding
// an id is worse than no field. No refresh on a miss -- bot authors would
// trigger a full users.list walk on every read.
void add_user_names(nlohmann::json& messages, Session& s) {
  for (auto& m : messages) {
    const std::string uid = m.value("user", "");
    if (uid.empty()) continue;
    const std::string name = s.resolver.display_name_for_user(uid);
    if (name != uid) m["user_name"] = name;
    if (m.contains("replies")) add_user_names(m["replies"], s);
  }
}

// conversations.replies answers for thread parents only; a plain message comes
// back as thread_not_found. Falling back to a one-message history window lets
// `--thread` mean "this message, plus its thread if it has one".
// With `since`, only messages after it survive -- including the parent, which
// Slack always prepends however old it is.
nlohmann::json fetch_thread(Session& s, const std::string& id, const std::string& ts,
                            long limit, const std::string& since = "") {
  try {
    std::map<std::string, std::string> params{
        {"channel", id}, {"ts", ts}, {"limit", std::to_string(limit)}};
    if (!since.empty()) params["oldest"] = since;
    auto res = s.client.call("conversations.replies", params);
    auto messages = keep_newer(res.value("messages", nlohmann::json::array()), since, false);
    // Slack's `limit` counts replies and always prepends the parent, so a
    // thread arrives with up to limit+1 messages. --limit means total messages
    // on the history path; keep it meaning that here.
    if (messages.size() > static_cast<size_t>(limit)) {
      messages.erase(messages.begin() + limit, messages.end());
    }
    return messages;
  } catch (const slack::ApiError& e) {
    if (e.code != "thread_not_found") throw;
    auto res = s.client.call("conversations.history",
                             {{"channel", id},
                              {"latest", ts},
                              {"oldest", ts},
                              {"inclusive", "true"},
                              {"limit", "1"}});
    return keep_newer(res.value("messages", nlohmann::json::array()), since, false);
  }
}

}  // namespace

int read(const Args& args) {
  auto channel_flag = args.value("channel");
  if (!channel_flag && !args.positional().empty()) channel_flag = args.positional()[0];
  auto thread_flag = args.value("thread");
  if (!channel_flag && !thread_flag) {
    std::cerr << "usage: slkx read --channel '#general' [--limit 20] [--since TS] "
                 "[--with-replies]\n"
                 "       slkx read --thread <permalink|ts> [--since TS]\n";
    return 2;
  }

  // Parsed before anything touches the network. A permalink works too: "since
  // this message" is the natural way to say where the last poll stopped.
  // A bare --since (an unquoted empty $LAST, say) must not silently mean "no
  // filter": a poller would take the whole window as new.
  if (args.has("since") && !args.value("since")) {
    std::cerr << "--since needs a timestamp or permalink\n";
    return 2;
  }
  std::string since;
  if (auto flag = args.value("since")) {
    try {
      since = parse_message_ref(*flag).message_ts;
    } catch (const std::runtime_error& e) {
      std::cerr << "--since: " << e.what() << "\n";
      return 2;
    }
  }

  Session s(args);
  const long limit = std::clamp(args.number_or("limit", 20L), 1L, 1000L);

  if (thread_flag) {
    auto target = resolve_thread(s, channel_flag, *thread_flag);
    if (!target) return 2;
    const std::string& id = target->channel_id;

    auto messages = fetch_thread(s, id, target->ts, limit, since);
    if (want_json(args)) {
      add_user_names(messages, s);
      std::cout << messages.dump() << "\n";
      return 0;
    }
    // conversations.replies already returns oldest first, parent included: no
    // reverse here, unlike the history path below.
    print_messages(messages, s, false);
    return 0;
  }

  const std::string id = s.resolver.to_id(*channel_flag);

  // No `oldest` sent: it would drop an old parent whose thread got a new reply,
  // which is half of what a poll wants to see. Filter the window instead.
  // ponytail: only threads whose parent sits in the last --limit messages are
  // seen. Raise --limit to widen the window.
  auto res = s.client.call("conversations.history",
                           {{"channel", id}, {"limit", std::to_string(limit)}});
  auto messages = keep_newer(res.value("messages", nlohmann::json::array()), since, true);
  // Slack returns newest first. Both outputs go oldest first -- the way a
  // conversation reads, and the order the thread path already has.
  std::reverse(messages.begin(), messages.end());

  // Saves the caller one `read --thread` per active thread. With --since only
  // threads with news are fetched, and only their new replies are kept.
  // ponytail: one call per thread, sequential and uncapped. A busy window
  // without --since can hit Slack's rate limit (exit 4); --since keeps it small.
  if (args.has("with-replies")) {
    for (auto& m : messages) {
      if (m.value("reply_count", 0) == 0) continue;
      if (!since.empty() && !(m.value("latest_reply", "") > since)) continue;
      const std::string parent = m.value("ts", "");
      auto replies = nlohmann::json::array();
      for (const auto& r : fetch_thread(s, id, parent, 1000, since)) {
        if (r.value("ts", "") != parent) replies.push_back(r);  // parent is `m`
      }
      m["replies"] = replies;
    }
  }

  if (want_json(args)) {
    add_user_names(messages, s);
    std::cout << messages.dump() << "\n";
    return 0;
  }
  print_messages(messages, s, true);
  return 0;
}

// Saves every attachment on one message and prints where each landed, so a
// caller can open them. The session cookie goes to files.slack.com and nowhere
// else, and nothing is written until the download has answered 200.
int files(const Args& args) {
  auto msg_flag = args.value("message");
  if (!msg_flag) {
    std::cerr << "usage: slkx files --message <permalink|ts> [--channel C] [--out DIR]\n";
    return 2;
  }
  const std::filesystem::path out_dir = args.value_or("out", ".");

  Session s(args);
  auto target = resolve_thread(s, args.value("channel"), *msg_flag);
  if (!target) return 2;

  // Limit 1 is exact: replies returns the message itself first whether it is a
  // parent or a reply, and the history fallback covers an unthreaded one.
  const auto messages = fetch_thread(s, target->channel_id, target->message_ts, 1);
  if (messages.empty() || messages[0].value("ts", "") != target->message_ts) {
    std::cerr << "message " << target->message_ts << " not found\n";
    return 1;
  }
  const auto attached = messages[0].value("files", nlohmann::json::array());
  if (attached.empty()) {
    std::cerr << "no files on this message\n";
    return 1;
  }

  std::filesystem::create_directories(out_dir);
  auto saved = nlohmann::json::array();
  for (const auto& f : attached) {
    const std::string id = f.value("id", "");
    const std::string name = f.value("name", "");
    const std::string shown = clean_name(name);  // for the terminal
    const std::string mode = f.value("mode", "");
    const std::string url = f.value("url_private_download", "");
    const std::string local = safe_filename(id, name);

    std::string skip;
    if (mode != "hosted" && mode != "snippet") {
      skip = "not stored on Slack (mode " + clean_name(mode) + ")";
    } else if (!is_slack_file_url(url)) {
      skip = "not on files.slack.com";
    } else if (local.empty()) {
      skip = "unexpected file id";
    }
    if (!skip.empty()) {
      std::cerr << "skipped " << shown << ": " << skip << "\n";
      continue;
    }

    const auto resp = http::get(url, s.client.cookie());
    if (resp.status != 200) {
      // Whatever was saved before this file is on disk; say so in --json too,
      // since text mode already printed those paths as it went.
      if (want_json(args)) std::cout << saved.dump() << "\n";
      const std::string what =
          "files.slack.com answered HTTP " + std::to_string(resp.status) + " for " + shown;
      // An expired session is a 302 to the login page. Writing that body would
      // leave HTML named image.png and call it success.
      if (resp.status == 302 || resp.status == 401 || resp.status == 403) {
        throw slack::ApiError("invalid_auth", what + ". The d cookie has likely rotated -- "
                                                     "re-copy SLACK_DTOKEN from the Slack web client.");
      }
      throw slack::ApiError("download_failed", what);
    }

    const auto path = std::filesystem::absolute(out_dir / local);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(resp.body.data(), static_cast<std::streamsize>(resp.body.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());

    saved.push_back({{"id", id},
                     {"name", name},
                     {"mimetype", f.value("mimetype", "")},
                     {"path", path.string()}});
    if (!want_json(args)) std::cout << path.string() << "\n";
  }

  if (want_json(args)) std::cout << saved.dump() << "\n";
  return saved.empty() ? 1 : 0;
}

// Replaces the text of one of your own messages. Slack refuses anyone else's
// with cant_update_message, which maps to exit 3.
int edit(const Args& args) {
  auto msg_flag = args.value("message");
  if (!msg_flag) {
    std::cerr << "usage: slkx edit --message <permalink|ts> --text 'new text' [--channel C]\n"
                 "       echo 'new text' | slkx edit --message <permalink|ts>\n";
    return 2;
  }
  // Before the Session, as in send: a piped body is not lost to an auth error.
  const auto text_flag = args.value("text");
  const std::string text = text_flag ? *text_flag : read_stdin();
  if (text.empty()) {
    std::cerr << "refusing to replace a message with nothing\n";
    return 2;
  }

  Session s(args);
  auto target = resolve_thread(s, args.value("channel"), *msg_flag);
  if (!target) return 2;

  auto res = s.client.call("chat.update",
                           {{"channel", target->channel_id},
                            {"ts", target->message_ts},
                            {"text", text},
                            // Slack regenerates the rich_text block it derived
                            // from text either way (checked 2026-09-30). This
                            // covers explicitly posted blocks, which its docs
                            // say are retained -- and clients render blocks
                            // over text, so the edit would not show.
                            {"blocks", "[]"},
                            {"link_names", "true"}});

  if (want_json(args)) {
    std::cout << res.dump() << "\n";
  } else {
    std::cout << "edited " << target->message_ts << " in "
              << s.resolver.name_for_id(target->channel_id) << "\n";
  }
  return 0;
}

// `channels` and `users` are one listing over two cache keys, both filled by
// the same Resolver::refresh().
int list_cached(const Args& args, const char* key, const char* prefix) {
  Session s(args);
  const auto path = cache_path(s.profile.name);
  if (args.has("refresh") || !std::filesystem::exists(path)) s.resolver.refresh();

  std::ifstream in(path);
  nlohmann::json cache;
  in >> cache;

  if (want_json(args)) {
    std::cout << cache.dump(2) << "\n";
    return 0;
  }
  std::vector<std::string> names;
  for (auto it = cache[key].begin(); it != cache[key].end(); ++it) {
    names.push_back(it.key());
  }
  std::sort(names.begin(), names.end());
  for (const auto& n : names) {
    std::cout << prefix << n << "\t" << cache[key][n].get<std::string>() << "\n";
  }
  return 0;
}

int channels(const Args& args) { return list_cached(args, "channels", "#"); }

int users(const Args& args) { return list_cached(args, "users", "@"); }

int notifications(const Args& args) {
  // There is no "give me my notifications" endpoint. This walks your
  // conversations and asks each one whether it has unread messages, which is
  // one API call per conversation. Fine for a personal workspace, painful past
  // a few dozen. `watch` is the streaming alternative.
  Session s(args);
  const long cap = std::clamp(args.number_or("max-conversations", 25L), 1L, 500L);

  auto convos = s.client.paginate(
      "users.conversations",
      {{"types", "public_channel,private_channel,mpim,im"},
       {"exclude_archived", "true"},
       {"limit", "200"}},
      "channels", 5);

  nlohmann::json unread = nlohmann::json::array();
  long checked = 0;

  for (const auto& c : convos) {
    if (checked >= cap) break;
    const std::string id = c.value("id", "");
    if (id.empty()) continue;
    ++checked;

    nlohmann::json info;
    try {
      info = s.client.call("conversations.info", {{"channel", id}});
    } catch (const slack::ApiError& e) {
      if (e.code == "ratelimited") throw;
      continue;  // no access, or a conversation type we cannot inspect
    }

    const auto& ch = info["channel"];
    const int count = ch.value("unread_count_display", ch.value("unread_count", 0));
    if (count > 0) {
      unread.push_back({{"id", id},
                        {"name", s.resolver.name_for_id(id)},
                        {"unread", count},
                        {"last_read", ch.value("last_read", "")}});
    }
  }

  if (want_json(args)) {
    std::cout << unread.dump(2) << "\n";
    return 0;
  }
  if (unread.empty()) {
    std::cout << "nothing unread in the " << checked << " conversations checked\n";
    return 0;
  }
  for (const auto& u : unread) {
    std::cout << u.value("unread", 0) << "\t" << u.value("name", "") << "\n";
  }
  return 0;
}

// Every field blocks.actions needs is on the message carrying the button:
// service_id is its bot_id, service_team_id its team, and the action is the
// button element with its block's block_id folded in. Nothing is hardcoded,
// and nothing is cacheable -- the bot replaces the message once a button is
// used, regenerating block ids.
int click(const Args& args) {
  auto thread_flag = args.value("thread");
  if (!thread_flag) {
    std::cerr << "usage: slkx click --thread <permalink|ts> [--channel C] "
                 "[--action ID] [--yes]\n";
    return 2;
  }

  Session s(args);
  auto target = resolve_thread(s, args.value("channel"), *thread_flag);
  if (!target) return 2;

  auto res = s.client.call("conversations.replies", {{"channel", target->channel_id},
                                                     {"ts", target->ts},
                                                     {"limit", "200"}});
  const auto messages = res.value("messages", nlohmann::json::array());

  auto found = find_buttons(messages);

  if (auto want = args.value("action")) {
    std::vector<ButtonRef> filtered;
    for (auto& c : found) {
      if (c.action.value("action_id", "") == *want) filtered.push_back(c);
    }
    found = filtered;
  }

  if (found.empty()) {
    std::cerr << "no live button in this thread"
              << (args.value("action") ? " matching --action" : "")
              << ". The bot replaces the message once a button is used.\n";
    return 1;
  }
  if (found.size() > 1) {
    std::cerr << "several buttons here; pick one with --action:\n";
    for (const auto& c : found) {
      std::cerr << "  --action " << c.action.value("action_id", "?") << "\t" << c.label
                << "\n";
    }
    return 2;
  }

  const ButtonRef& c = found.front();
  if (!args.has("yes")) {
    std::cout << "would click: \"" << c.label << "\"  (" << c.action.value("action_id", "?")
              << ")\n"
              << "  on message " << c.message_ts << " in "
              << s.resolver.name_for_id(target->channel_id) << "\n"
              << "re-run with --yes to dispatch\n";
    return 0;
  }

  const nlohmann::json container{{"type", "message"},
                                 {"message_ts", c.message_ts},
                                 {"channel_id", target->channel_id},
                                 {"is_ephemeral", false}};
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  auto out = s.client.call(
      "blocks.actions",
      {{"service_id", c.bot_id},
       {"app_id", c.app_id},
       {"service_team_id", c.team},
       {"actions", nlohmann::json::array({c.action}).dump()},
       {"container", container.dump()},
       {"client_token", "slkx-" + std::to_string(now)},
       {"state", "{\"values\":{}}"},
       // Web-client telemetry. Sent verbatim: a button is one-shot per thread,
       // so matching the captured request beats finding out what is optional.
       {"_x_reason", "dispatch_action_to_developer"},
       {"_x_mode", "online"},
       {"_x_sonic", "true"},
       {"_x_app_name", "client"}});

  if (want_json(args)) {
    std::cout << out.dump() << "\n";
  } else {
    std::cout << "clicked " << c.action.value("action_id", "?") << " on " << c.message_ts
              << "\n";
  }
  return 0;
}

namespace {
// Signal handlers must be captureless, so the flag lives here.
std::atomic<bool> g_stop{false};
}  // namespace

// A filter, not a display: every frame Slack sends goes to stdout verbatim, one
// per line. Interpretation belongs to whatever consumes the stream.
int watch(const Args& args) {
  Session s(args);

  std::signal(SIGINT, [](int) { g_stop = true; });
  std::signal(SIGTERM, [](int) { g_stop = true; });

  rtm::stream(s.client, [](const std::string& frame) {
    // Flush per frame: stdout is block-buffered when piped, so without this a
    // consumer sees nothing until 4KB accumulates.
    std::cout << frame << "\n" << std::flush;
  }, g_stop);

  return 0;
}

// Slack expires a session that goes unused. Reading one message every --every
// seconds keeps it warm. Transient failures (network, rate limit) are logged
// and retried next tick; a rejected session ends the loop, since there is
// nothing left to keep alive.
int keepalive(const Args& args) {
  Session s(args);
  const std::string id = s.resolver.to_id(args.value_or("channel", "general"));
  const long every = std::max(args.number_or("every", 3600L), 60L);

  std::signal(SIGINT, [](int) { g_stop = true; });
  std::signal(SIGTERM, [](int) { g_stop = true; });

  while (!g_stop) {
    try {
      s.client.call("conversations.history", {{"channel", id}, {"limit", "1"}});
      std::cerr << "keepalive: ok\n";
    } catch (const slack::ApiError& e) {
      if (e.code == "invalid_auth" || e.code == "not_authed") throw;
      std::cerr << "keepalive: " << e.what() << "\n";
    } catch (const http::TransportError& e) {
      std::cerr << "keepalive: " << e.what() << "\n";
    }
    // Sleep in 1s steps so SIGTERM stops it promptly.
    for (long i = 0; i < every && !g_stop; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
  return 0;
}
}  // namespace slk::commands
