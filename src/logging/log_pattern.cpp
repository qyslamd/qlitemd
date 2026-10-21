#include "logging/log_pattern.h"

#include <vector>

std::string BuildLogPattern(const LogPatternOptions& options) {
  std::vector<std::string> parts;

  if (options.time) {
    parts.emplace_back("[%Y-%m-%d %H:%M:%S.%e]");
  }

  if (options.level) {
    parts.emplace_back(options.colored_level ? "[%^%l%$]" : "[%l]");
  }

  if (options.thread) {
    parts.emplace_back("[%t]");
  }

  if (options.module) {
    parts.emplace_back("[%n]");
  }

  // 源文件与函数名是两个独立开关，各自占一段，可同时打开
  if (options.source) {
    parts.emplace_back("[%s:%#]");
  }
  if (options.func) {
    parts.emplace_back("[%!]");
  }

  if (options.message) {
    parts.emplace_back("%v");
  }

  std::string pattern;
  for (const std::string& part : parts) {
    if (!pattern.empty()) {
      pattern += ' ';
    }
    pattern += part;
  }
  return pattern;
}
