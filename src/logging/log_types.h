#ifndef FILE_LOG_LOG_TYPES_H_
#define FILE_LOG_LOG_TYPES_H_

#include <cstddef>
#include <string>

// 日志核心的公共类型。**本模块不依赖 Qt**，只有 std 类型与 spdlog。

// 日志级别。核心只认强类型，配置格式由调用方负责转换
enum class LogLevel {
  kDebug = 0,
  kInfo = 1,
  kWarn = 2,
  kError = 3,
  kFatal = 4,
};

// 日志 pattern 的字段开关，每项独立控制一段是否输出。
// 控制台与文件共用同一组开关，只有级别段是否着色不同。
struct LogPatternOptions {
  bool time = true;
  bool level = true;
  bool thread = false;
  bool module = true;
  bool source = false;  // 源文件:行号（%s:%#）
  bool func = false;    // 函数名（%!）
  bool message = true;

  // 控制台与界面 sink 用彩色级别（%^%l%$），文件 sink 用 %l
  bool colored_level = false;
};

// 启动参数。**日志目录、应用名、进程号都由调用方算好传进来**：
// 这一层不做平台路径推导，否则它算出来的目录可能和 Qt 的 AppLocalDataLocation
// 差一点，日志就会悄悄搬到别处去
struct LogInitOptions {
  std::string log_dir;
  std::string file_name;  // 不含扩展名，一般是 applicationName
  // 主日志文件被别的实例占用时，退到 <file_name>-<pid>.log 用它
  int pid = 0;
  std::size_t max_file_size = 10 * 1024 * 1024;  // 10 MB
  std::size_t max_file_count = 20;
  int keep_days = 7;  // 启动时清掉更早的 *.log
  LogLevel level = LogLevel::kInfo;

  // 控制台与界面 sink 用这一套，文件 sink 用下面那一套（差别在是否着色）
  LogPatternOptions console_pattern;
  LogPatternOptions file_pattern;
};

#endif  // FILE_LOG_LOG_TYPES_H_
