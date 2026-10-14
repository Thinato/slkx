---
name: slkx
description: Read and act on Slack from the shell with the slkx CLI -- read channels and threads, send or edit messages, react, download attachments, list unreads, stream live events. Use whenever a task involves Slack (a Slack permalink, a channel like #general, "check Slack", "post in", "reply to that thread").
---

# slkx

`slkx` is a CLI that talks to Slack as the user, with their own session. Run
`slkx --help` first: it is the full reference for commands, flags and examples.
This file only adds what the help does not say.

## Ask before anything other people see

`send`, `edit`, `react` and `click --yes` act as the user, in front of their
coworkers, who may see the result before any undo. Show the exact text and
target and get an explicit yes before running them. Everything else only reads
from Slack.

`click` without `--yes` is a dry run that shows which button would be pressed;
run that first.

## Working with output

- Add `--json` when you need to parse the output. `read` lists messages oldest
  first, and each one gets a `user_name` field.
- A permalink (Slack's "Copy link") carries its channel and timestamp, so pass
  it to `--thread` or `--message` as is. `read` output gives you the timestamps
  for building follow-up commands.
- To catch up on a channel, `read --channel C --since <ts or permalink>
  --with-replies` returns only what came after that point, threads included.
- Channel and user names come from a local cache. If a name is unknown, run
  `slkx channels --refresh` and try again.

## Exit codes

| code | meaning | what to do |
| --- | --- | --- |
| 0 | ok | |
| 1 | other error (bad channel, spent button, ...) | read stderr |
| 2 | usage error | check `slkx --help` |
| 3 | Slack rejected the call; `invalid_auth` means the session expired | the user must re-copy `SLACK_CTOKEN` and `SLACK_DTOKEN` from their browser; you cannot fix it |
| 4 | rate limited | wait, then retry once |
| 5 | network error | retry once |
