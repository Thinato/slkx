#pragma once
// Minimal argv parser: <program> <subcommand> [--flag value | --flag=value | --bool] [positional...]
// Deliberately dependency-free. Swap for CLI11 when you want generated help
// text, subcommand trees and typed validation.

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace slk {

class Args {
public:
  Args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) argv_.emplace_back(argv[i]);
    parse();
  }

  const std::string& command() const { return command_; }
  const std::vector<std::string>& positional() const { return positional_; }

  bool has(const std::string& name) const { return flags_.count(name) > 0; }

  // Returns nullopt when absent or when the flag was used as a boolean.
  std::optional<std::string> value(const std::string& name) const {
    auto it = flags_.find(name);
    if (it == flags_.end() || !it->second) return std::nullopt;
    return it->second;
  }

  std::string value_or(const std::string& name, std::string fallback) const {
    auto v = value(name);
    return v ? *v : std::move(fallback);
  }

  // Accepts --limit 20 and --limit=20; returns fallback on absence or garbage.
  long number_or(const std::string& name, long fallback) const {
    auto v = value(name);
    if (!v) return fallback;
    try {
      return std::stol(*v);
    } catch (...) {
      return fallback;
    }
  }

private:
  void parse() {
    size_t i = 0;
    if (i < argv_.size() && argv_[i].rfind("--", 0) != 0) command_ = argv_[i++];

    for (; i < argv_.size(); ++i) {
      const std::string& tok = argv_[i];
      if (tok.rfind("--", 0) != 0) {
        positional_.push_back(tok);
        continue;
      }
      std::string body = tok.substr(2);
      auto eq = body.find('=');
      if (eq != std::string::npos) {
        flags_[body.substr(0, eq)] = body.substr(eq + 1);
        continue;
      }
      // A following token that is not a flag becomes this flag's value.
      if (i + 1 < argv_.size() && argv_[i + 1].rfind("--", 0) != 0) {
        flags_[body] = argv_[++i];
      } else {
        flags_[body] = std::nullopt;  // boolean flag
      }
    }
  }

  std::vector<std::string> argv_;
  std::string command_;
  std::vector<std::string> positional_;
  std::map<std::string, std::optional<std::string>> flags_;
};

}  // namespace slk
