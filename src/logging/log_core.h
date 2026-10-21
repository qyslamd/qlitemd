#ifndef FILE_LOG_LOG_CORE_H_
#define FILE_LOG_LOG_CORE_H_

#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "logging/log_types.h"

// 日志核心：**不依赖 Qt**，只链 spdlog与标准库。
//
// 与 Qt 有关的三件事都被挡在门外：
//   - 路径：日志目录由调用方算好放进 LogInitOptions
//   - 配置：级别与 pattern 开关由调用方填好；变更后调 ApplyOptions 重新应用
//   - 界面 sink：通过 extra_sinks 注入，核心只认 spdlog::sink_ptr
//
// 这样不依赖 Qt 的调用方也能直接复用日志核心。
class LogCore {
 public:
  // extra_sinks 是调用方注入的额外 sink（例如转发到界面的 sink）。
  // 它们的 pattern 一律按 options.console_pattern 设置——核心无从判断哪个
  // 额外 sink 是"控制台"，约定就是这么定的。
  static void Init(const LogInitOptions& options,
                   const std::vector<spdlog::sink_ptr>& extra_sinks = {});
  static void Shutdown();

  // 模块名是 std::string_view：调用点多数字面量直接传，省掉
  // QString 构造 + toStdString 两次分配
  static spdlog::logger* GetLogger(std::string_view module);

  static void SetLevel(std::string_view module, LogLevel level);
  static void SetAllLevel(LogLevel level);

  // 配置热更新：调用方读完配置后整份传进来重新应用（级别与两套 pattern）。
  // **实现必须经 spdlog::set_level**——它遍历 registry，已存在的模块 logger
  // 才会跟着变；只改全局变量的话，设置页改级别不会生效
  static void ApplyOptions(const LogInitOptions& options);

 private:
  // 按需创建模块 logger，线程安全（调用方需持有 s_mutex_）
  static spdlog::logger* GetOrCreateModuleLogger(const std::string& module_name);

  static void ApplyPatternToSinks(const LogInitOptions& options);

  // 回放 Init 之前攒下的日志（见 log_core.cpp 的 BootstrapLog）
  static void ReplayBootstrapLogs();
};

// 全局日志调用宏，使用 fmt 格式化语法 ({})
#define LOG_DEBUG(module, ...)                            \
  do {                                                    \
    auto* _l = LogCore::GetLogger(module);                \
    if (_l) {                                             \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, ""}, \
              spdlog::level::debug, __VA_ARGS__);         \
    }                                                     \
  } while (0)

#define LOG_INFO(module, ...)                                                  \
  do {                                                                         \
    auto* _l = LogCore::GetLogger(module);                                     \
    if (_l) {                                                                  \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, ""}, spdlog::level::info, \
              __VA_ARGS__);                                                    \
    }                                                                          \
  } while (0)

#define LOG_WARN(module, ...)                                                  \
  do {                                                                         \
    auto* _l = LogCore::GetLogger(module);                                     \
    if (_l) {                                                                  \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, ""}, spdlog::level::warn, \
              __VA_ARGS__);                                                    \
    }                                                                          \
  } while (0)

#define LOG_ERROR(module, ...)                                                \
  do {                                                                        \
    auto* _l = LogCore::GetLogger(module);                                    \
    if (_l) {                                                                 \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, ""}, spdlog::level::err, \
              __VA_ARGS__);                                                   \
    }                                                                         \
  } while (0)

#define LOG_FATAL(module, ...)                            \
  do {                                                    \
    auto* _l = LogCore::GetLogger(module);                \
    if (_l) {                                             \
      _l->log(spdlog::source_loc{__FILE__, __LINE__, ""}, \
              spdlog::level::critical, __VA_ARGS__);      \
    }                                                     \
  } while (0)

#endif  // FILE_LOG_LOG_CORE_H_
