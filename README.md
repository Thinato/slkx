# slkx

Slack from the shell. C++20, libcurl, nlohmann/json. One binary, no runtime deps
beyond libcurl.

## Install

Prebuilt binaries for Linux x86_64 and macOS arm64:

```sh
curl --proto '=https' --tlsv1.2 -sSf \
  https://raw.githubusercontent.com/Thinato/slkx/main/install.sh | sh
```

It installs to `~/.local/bin` (override with `SLKX_INSTALL_DIR`) and fetches the
latest release (pin one with `SLKX_VERSION=v0.1.0-beta`). The Linux binary needs
`libcurl4` and `libssl3` (Ubuntu 22.04+, Debian 12+). Anything else: build from
source.

### AI agents

`install.sh` also installs an agent skill into `~/.claude/skills/slkx/` when
Claude Code is present, so Claude knows slkx exists and reaches for it on Slack
tasks. The skill points the agent at `slkx --help` and adds what the help does
not cover: ask before posting, parse with `--json`, what each exit code means.
For other agents, copy [`skills/slkx/SKILL.md`](skills/slkx/SKILL.md) wherever
they read skills from, or add a line to their instructions file such as "for
Slack, use `slkx`; run `slkx --help` first".

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
cmake --build build -j
./build/slkx --help
```

`OPENSSL_ROOT_DIR` is needed by IXWebSocket, which `watch` uses: the system
libcurl has no `ws`/`wss` support, and IXWebSocket's Apple default backend
(SecureTransport) has no header in the Command Line Tools SDK.

Needs a C++20 compiler, CMake 3.20+, and libcurl development headers
(`libcurl4-openssl-dev` / `brew install curl`). nlohmann/json is used from the
system if installed, otherwise fetched at configure time.

## Setup

slkx authenticates as *you*, with the same credentials your Slack web client
uses -- there is no Slack app to create and no scopes to request.

```sh
export SLACK_CTOKEN=xoxc-...   # web-client token
export SLACK_DTOKEN=xoxd-...   # session cookie
slkx auth status
slkx channels --refresh        # builds the name -> id cache
```

Both values come from a signed-in `app.slack.com` tab in your browser:

| value | where |
| --- | --- |
| `SLACK_CTOKEN` | DevTools > Application > Local Storage > `localConfig_v2`, then `teams.<team id>.token` |
| `SLACK_DTOKEN` | DevTools > Application > Cookies > `.slack.com` > `d` (copy the value verbatim, it is already percent-encoded) |

Precedence, per credential: `--token`/`--cookie` > `SLACK_CTOKEN`/`SLACK_DTOKEN`
> `~/.config/slkx/<profile>.json`. To pin a pair to a named workspace instead of
the environment:

```sh
slkx --workspace clientco auth set --token xoxc-... --cookie xoxd-...
```

Optional: Slack expires a session that sits unused, after which you re-copy
both values. If you use slkx only now and then, leave a keepalive running in
the background (details in [Keeping the session alive](#keeping-the-session-alive)):

```sh
nohup slkx keepalive >/dev/null 2>&1 &   # reads #general hourly; --channel to change
```

## Usage

```sh
slkx send --channel '#general' --message 'deploy finished'
make 2>&1 | slkx send --channel '@alice'          # body from stdin
slkx read --channel '#incidents' --limit 50
slkx read --channel C0123456789 --json | jq -r '.[].text'

# a message and its thread. The permalink (Copy link in Slack) carries both the
# channel and the timestamp, so --channel is not needed:
slkx read --thread 'https://acme.slack.com/archives/C0123456789/p1788272945669409'
slkx read --channel '#code-review' --thread 1788272945.669409

# reply in that thread. The permalink supplies the channel here too, so reading
# a thread and answering it compose -- no copying the ts across by hand:
slkx send --thread 'https://acme.slack.com/archives/C0123456789/p1788272945669409' \
  --message 'on it'
slkx send --channel '#code-review' --thread 1788272945.669409 --message 'on it'

# react to one message. `:eyes:` and `eyes` both work; --remove undoes it:
slkx react --message 'https://acme.slack.com/archives/C0123456789/p1788272945669409' \
  --emoji eyes
slkx react --message '...' --emoji eyes --remove

# rewrite one of your own messages -- keep a single roll-up current instead of
# posting a new one each time. Body from --text or stdin:
slkx edit --channel '#status' --message 1788272945.669409 --text 'updated'
render-status | slkx edit --message 'https://acme.slack.com/archives/...'

# save a message's attachments and print where they landed:
slkx files --message 'https://acme.slack.com/archives/C0123456789/p1788272945669409' \
  --out ./shots
slkx users                                        # @handle -> user id
# press a Block Kit button (a bot's "available for deploy?" prompt, say).
# Dry-run by default; --yes dispatches:
slkx click --thread 'https://acme.slack.com/archives/C0123456789/p1788276349696629'
slkx click --thread '...' --action dev_available_yes --yes

slkx watch                            # live events as JSONL on stdout
slkx keepalive --channel '#general'   # keep an idle session from expiring
slkx notifications
slkx --workspace clientco send --channel '#status' --message 'done'
```

Everything human-readable goes to stdout; errors go to stderr. `--json` on any
read command emits the raw Slack payload, with two additions and one change:

- `user_name` on each message whose author is in the cache (`slkx users
  --refresh` after someone joins). An unknown author gets no field rather than
  the id in a name slot.
- `replies` on each thread parent, with `--with-replies`.
- `read` lists messages **oldest first**, like its text output. Slack's own
  `conversations.history` answers newest first; slkx reverses it so the two
  outputs never disagree.

A channel (`--channel`) is `'#name'`, a bare `name`, an id, or `'@handle'` for a
DM. Names resolve through the cache, which lists channels you have joined.

One asymmetry worth knowing, because Slack permalinks are ambiguous about it: a
link to a *reply* resolves to the whole thread for `read`, `click` and `send`,
but to that exact reply for `edit`, `react` and `files`. Reacting to, editing or
downloading from the thread parent when you meant the reply would be a silent
wrong answer, so `src/message_ref.hpp` keeps both timestamps and each command
picks the one it means.

### Polling

`--since` takes a timestamp or a permalink and keeps only what came after it:

```sh
# new messages in the channel, plus any older thread that got a new reply
slkx read --channel '#status' --since "$LAST" --json

# or both at once: each active thread arrives with its new replies nested
slkx read --channel '#status' --since "$LAST" --with-replies --json

# the new replies of one thread
slkx read --channel '#status' --thread 1788272945.669409 --since "$LAST"
```

`--with-replies` puts a `replies` array (oldest first, `user_name` included) on
each thread parent and prints them indented under it. With `--since` it fetches
only threads whose `latest_reply` is newer and keeps only the new replies;
without it, every thread in the window comes back whole. It costs one
`conversations.replies` call per thread, so a large window without `--since`
can hit Slack's rate limit (exit 4).

On a channel, a thread parent older than `--since` still comes back when its
`latest_reply` is newer -- that is how a poll notices thread activity. The
`latest_reply`, `reply_count` and `reply_users` fields are Slack's own, already
in `--json`. Only the last `--limit` messages are examined, so widen `--limit`
if a poll can fall far behind.

### Files

`files` saves each attachment as `<file id>-<name>` (three screenshots in one
channel were all `image.png`) and prints absolute paths, one per line. Two
rules hold on the way in:

- **The session cookie goes to `https://files.slack.com` and nowhere else.**
  A file stored elsewhere -- a Google Drive link, say -- is skipped with a
  reason on stderr rather than fetched with your session.
- **Only a `200` is written.** An expired session makes `files.slack.com` answer
  `302` to the login page; slkx never follows redirects, so that exits 3
  instead of saving the login page as `image.png`.

Both checks live in `src/filename.hpp` and are self-checked.

### Exit codes

| code | meaning |
| --- | --- |
| 0 | success |
| 1 | local error (no token, unknown channel) |
| 2 | usage error |
| 3 | Slack returned `ok: false` |
| 4 | rate limited |
| 5 | network / transport failure |

## Layout

```
src/args.hpp        argv parser (swap for CLI11 when you want generated help)
src/http.{hpp,cpp}  libcurl wrapper, form POST / GET + d cookie + browser headers
src/config.{hpp,cpp} profiles, token load/save, XDG paths
src/slack.{hpp,cpp} Web API client, token in body, ok:false -> ApiError, pagination
src/resolver.{hpp,cpp} #name / @handle -> conversation id, disk-cached
src/message_ref.hpp permalink / ts -> (channel, thread ts, message ts); self-checked
src/filename.hpp    which URLs get the cookie; safe local names; self-checked
src/block_action.hpp finds Block Kit buttons in a thread; self-checked
src/browser.hpp     the Chrome UA/Origin both transports present
src/rtm.{hpp,cpp}   RTM socket: connect, d-cookie handshake, reconnect
src/commands.cpp    one function per subcommand
src/main.cpp        dispatch and error-to-exit-code mapping
```

### Clicking buttons

`click` drives `blocks.actions`, the endpoint the web client hits when you press
a Block Kit button. Everything it sends is read from the message carrying the
button -- `service_id` is its `bot_id`, `service_team_id` its `team`, and the
action is the button element with its block's `block_id` folded in -- so no app
ids are configured anywhere.

A button is one-shot: the posting app replaces its message once the button is
used, which regenerates the block ids. So `click` always reads the thread fresh,
and a spent thread exits 1.

## Live events

`watch` streams Slack's real-time socket. It is a filter, not a display: every
frame goes to stdout verbatim, one JSON object per line and flushed per line,
while connect/reconnect notices go to stderr. Interpretation belongs to whatever
consumes it.

```sh
slkx watch 2>/dev/null | jq -c 'select(.type=="reaction_added")'

# someone put :eyes: on one of your messages in #code-review
slkx watch 2>/dev/null | jq -c 'select(.type=="reaction_added"
  and .reaction=="eyes" and .item_user=="'"$SLACK_USER_ID"'")'
```

`reaction_added` carries `reaction`, `item.channel`, `item.ts` and `item_user`,
so "on *my* message" is a field comparison rather than a lookup.

Two things to know:

- **A running watcher shows you as active in Slack.** Connecting the socket
  yields `manual_presence_change: {"presence":"active"}`. That is inherent to
  RTM, not configurable here.
- **Ctrl-C stops it.** A shell-backgrounded `watch` inherits `SIGINT: SIG_IGN`
  like any background job, so stop those with `SIGTERM` (`kill %1`) instead.

The socket handshake must carry the `d` cookie. Without it Slack completes the
upgrade and *then* sends `{"type":"error","error":{"msg":"invalid_auth"}}` and
closes -- which looks exactly like RTM being unavailable to user tokens. It is
not; see `src/rtm.cpp`.

## Keeping the session alive

Slack expires a session that goes unused for long enough, and then every call
fails with `invalid_auth`. `keepalive` prevents that by reading the latest
message of one channel (`general` unless `--channel` says otherwise) every
`--every` seconds (default 3600, minimum 60). It logs one line per tick to
stderr.

```sh
nohup slkx keepalive --channel '#random' >/dev/null 2>&1 &
```

A network error or a rate limit is logged and tried again on the next tick. If
Slack rejects the session, `keepalive` exits with code 3, because there is
nothing left to keep alive. Stop it with `SIGTERM`. For it to survive reboots,
run it from a launchd agent or a systemd user unit.

## Known gaps

- **Retries.** `slack::Client::call` detects 429 and throws with the
  `Retry-After` value but does not sleep. When you want backoff, that function
  is the only place to change.
- **Credential storage.** Token and cookie sit in plaintext at 0600 (and in
  your shell environment). Move to Keychain Services / libsecret / DPAPI before
  anyone else installs this.
- **Sessions rotate.** Logging out of Slack, or its periodic re-auth,
  invalidates both values; calls then fail with `invalid_auth` and you re-copy
  them. This is inherent to session credentials, not a bug to fix.
- **Terms of service.** Browser session credentials are not an approved API
  path. This is your own session, and the same approach `slk` and
  `rneatherway/slack` take, but it carries some account risk.
- **Enterprise Grid.** Calls go to `slack.com/api`. A grid workspace needs the
  host from `auth.test`'s `url` field; `slack::kBase` is the only place to
  change.
- **`notifications` is N+1.** One `conversations.info` call per conversation,
  capped by `--max-conversations`. Slack has no endpoint that returns unreads
  directly, and `unread_count_display` is only populated for DMs.
- **Attachments are buffered in memory** before being written. Fine for
  screenshots; `http::get` is the place to stream to disk if video matters.
- **No event persistence.** `watch` streams; it stores nothing. A consumer that
  needs history across restarts has to keep its own log.
- **Legacy RTM.** `watch` uses `rtm.connect`, which Slack deprecated for apps.
  It works for a user session today. If it is withdrawn, the fallback is the
  web client's own socket, which speaks identical frames but requires
  constructing the URL rather than asking for it.

## License

MIT, see [LICENSE](LICENSE).
