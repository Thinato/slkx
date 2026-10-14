#pragma once
#include "args.hpp"

namespace slk::commands {

// Each returns a process exit code.
int auth(const Args& args);
int send(const Args& args);
int react(const Args& args);
int edit(const Args& args);
int files(const Args& args);
int read(const Args& args);
int channels(const Args& args);
int users(const Args& args);
int click(const Args& args);
int watch(const Args& args);
int keepalive(const Args& args);
int notifications(const Args& args);

}  // namespace slk::commands
