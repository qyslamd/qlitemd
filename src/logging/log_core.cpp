#include "logging/log_core.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <spdlog/async.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

// stdout_color_sink 在 Windows 上映射为 wincolor，其余平台映射为 ansicolor
#include <spdlog/sinks/stdout_color_sinks.h>

#include "logging/log_pattern.h"

namespace {

bool g_initialized = false;
std::mutex g_mutex;
// 模块名用 std::string_view 查：C++17 的 unordered_map 还不支持异构查找，
// 所以这里用带透明比较器的 map——模块只有十几个，几次比较比两次字符串分配便宜
std::map<std::string, spdlog::logger*, std::less<>> g_module_loggers;
spdlog::sink_ptr g_console_sink;
spdlog::sink_ptr g_file_sink;
std::vector<spdlog::sink_ptr> g_extra_sinks;

spdlog::level::level_enum ToSpdlogLevel(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return spdlog::level::debug;
    case LogLevel::kWarn:
      return spdlog::level::warn;
    case LogLevel::kError:
      return spdlog::level::err;
    case LogLevel::kFatal:
      return spdlog::level::critical;
    case LogLevel::kInfo:
    default:
      return spdlog::level::info;
  }
}

// 清掉 keep_days 天之前的 *.log。原来是 QDir + QFileInfo 遍历，这里是它的
// 标准库等价物
void RemoveExpiredLogs(const std::string& log_dir, int keep_days) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::file_time_type now = fs::file_time_type::clock::now();
  for (fs::directory_iterator it(log_dir, ec), end; !ec && it != end;
       it.increment(ec)) {
    const fs::directory_entry& entry = *it;
    std::error_code type_ec;
    if (!entry.is_regular_file(type_ec) || type_ec) {
      continue;
    }
    if (entry.path().extension() != ".log") {
      continue;
    }
    std::error_code time_ec;
    const fs::file_time_type mtime = fs::last_write_time(entry.path(), time_ec);
    if (time_ec) {
      continue;
    }
    const auto age_hours =
        std::chrono::duration_cast<std::chrono::hours>(now - mtime).count();
    if (age_hours / 24 > keep_days) {
      std::error_code remove_ec;
      fs::remove(entry.path(), remove_ec);
    }
  }
}

// Init() 之前打的日志先收在这里。
//
// 启动最早的那一段（SingleInstance 的构造、"是否已有实例"的判定）恰好是"程序为
// 什么一闪就退"最需要的线索，而那时 spdlog 还没建起来：以前 GetLogger 返回
// nullptr，宏体里的 if (_l) 把它们全部静默丢掉。改成先落进这个缓冲，Init 结束前
// 按原级别回放，顺序与调用顺序一致。
class BootstrapLog : public spdlog::sinks::base_sink<std::mutex> {
 public:
  struct Record {
    std::string module;
    spdlog::level::level_enum level = spdlog::level::info;
    std::string text;
  };

  // 取走全部记录并清空。只在 Init 的单线程阶段调用
  std::vector<Record> Drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Record> out;
    out.swap(records_);
    return out;
  }

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    // 启动期日志有上限：满了丢新的，不覆盖早的——最早的几条往往最要紧
    if (records_.size() >= kMaxRecords) {
      return;
    }
    Record record;
    record.module.assign(msg.logger_name.data(), msg.logger_name.size());
    record.level = msg.level;
    record.text.assign(msg.payload.data(), msg.payload.size());
    records_.push_back(std::move(record));
  }

  void flush_() override {}

 private:
  static constexpr std::size_t kMaxRecords = 512;
  std::vector<Record> records_;
};

// 缓冲 sink 只此一个，被启动期的各模块 logger 共用（spdlog 的 logger 要
// shared_ptr，所以这里也按 shared_ptr 持有，而不是裸引用）
std::shared_ptr<BootstrapLog> BootstrapSink() {
  static std::shared_ptr<BootstrapLog> sink = std::make_shared<BootstrapLog>();
  return sink;
}

// 每个模块一个**同步** logger：init_thread_pool 还没跑，这里不能用 async_logger
std::shared_ptr<spdlog::logger> MakeBootstrapLogger(const std::string& module) {
  auto logger = std::make_shared<spdlog::logger>(module, BootstrapSink());
  // 先全收下来，回放时由真正的 logger 按配置级别过滤
  logger->set_level(spdlog::level::trace);
  return logger;
}

spdlog::logger* GetBootstrapLogger(std::string_view module) {
  static std::mutex mutex;
  static std::map<std::string, std::shared_ptr<spdlog::logger>, std::less<>>
      loggers;
  std::lock_guard<std::mutex> lock(mutex);
  auto it = loggers.find(module);
  if (it != loggers.end()) {
    return it->second.get();
  }
  auto logger = MakeBootstrapLogger(std::string(module));
  spdlog::logger* raw = logger.get();
  loggers.emplace(std::string(module), std::move(logger));
  return raw;
}

// 预注册的模块名。Init 在**单线程阶段**按这张表把 logger 一次建好：运行期的
// 懒创建虽然能用，但要走注册表的查重（见 GetOrCreateModuleLogger），提前建好
// 就绕开了那段时序。新模块先登记在这里
constexpr const char* kPreRegisterModules[] = {
    "ACQUISITION", "ALARM",    "AUTH",    "COMM", "CONFIG",   "CURVE",
    "DASHBOARD",   "LOGIN",    "LOGGER",  "MAIN", "PRESSURE", "PROCESS",
    "REPLAY",      "SELFTEST", "STORAGE", "UI"};

}  // namespace

void LogCore::ApplyPatternToSinks(const LogInitOptions& options) {
  // 控制台与界面窗口用彩色级别，文件用非彩色，其余开关一致
  const std::string console_pattern = BuildLogPattern(options.console_pattern);
  const std::string file_pattern = BuildLogPattern(options.file_pattern);

  if (g_console_sink) {
    g_console_sink->set_pattern(console_pattern);
  }
  for (const spdlog::sink_ptr& sink : g_extra_sinks) {
    sink->set_pattern(console_pattern);
  }
  if (g_file_sink) {
    g_file_sink->set_pattern(file_pattern);
  }
}

void LogCore::Init(const LogInitOptions& options,
                   const std::vector<spdlog::sink_ptr>& extra_sinks) {
  if (g_initialized) {
    return;
  }

  // 日志目录由调用方算好（Qt 侧用 QStandardPaths），由这里创建：
  // rotating_file_sink 自己不建目录，目录不在时它会抛异常并被下面的降级路径
  // 吞掉，表现为"没有日志文件"这种难查的失败
  std::error_code ec;
  std::filesystem::create_directories(options.log_dir, ec);

  RemoveExpiredLogs(options.log_dir, options.keep_days);

  // 异步日志配置
  spdlog::init_thread_pool(8192, 1);

  std::vector<spdlog::sink_ptr> sinks;

  // 控制台彩色 sink（仅 Debug 构建启用）
#ifndef NDEBUG
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::debug);
  // pattern 统一在 sink 建齐后由 ApplyPatternToSinks 按配置设置
  g_console_sink = console_sink;
  sinks.push_back(console_sink);
#endif

  // 滚动文件 sink；文件名随应用名走，避免模板产物互相覆盖。
  //
  // 构造时的 rotate_on_open 会立刻轮转一次，而轮转要重命名日志文件——若另一个
  // 实例正开着它（Windows 不允许重命名被占用的文件），spdlog 会抛异常。正常
  // 路径下第二个实例早在 SingleInstance 那层就被挡回去了，但 UI 测试回放是有意
  // 绕过那层的，于是会撞上：异常没人接 → std::terminate → abort，进程连一行
  // 日志都来不及写。日志建不起来不该把主程序带走，所以这里降级处理：先退到
  // 带进程号的独立文件，再不行就不写文件（控制台与界面 sink 照常）
  const std::string file_name =
      options.file_name.empty() ? std::string("app") : options.file_name;
  const std::vector<std::string> candidates = {
      options.log_dir + "/" + file_name + ".log",
      options.log_dir + "/" + file_name + "-" + std::to_string(options.pid) +
          ".log",
  };
  for (const std::string& candidate : candidates) {
    try {
      auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          candidate, options.max_file_size, options.max_file_count, true);
      sink->set_level(spdlog::level::debug);
      g_file_sink = sink;
      sinks.push_back(sink);
      break;
    } catch (const std::exception& e) {
      // 此刻日志系统还没起来，只能直接写 stderr
      std::fprintf(stderr, "日志文件不可用（%s）：%s\n", e.what(),
                   candidate.c_str());
    }
  }

  // 调用方注入的额外 sink（例如转发到界面的 sink）
  for (const spdlog::sink_ptr& sink : extra_sinks) {
    if (!sink) {
      continue;
    }
    sink->set_level(spdlog::level::debug);
    g_extra_sinks.push_back(sink);
    sinks.push_back(sink);
  }

  // sink 已建齐，按配置统一设置 pattern
  ApplyPatternToSinks(options);

  // 注册默认 logger，所有模块共用。文件 sink 可能因上面降级而不存在
  auto default_logger = std::make_shared<spdlog::async_logger>(
      "default", sinks.begin(), sinks.end(), spdlog::thread_pool(),
      spdlog::async_overflow_policy::block);
  default_logger->flush_on(spdlog::level::err);
  spdlog::register_logger(default_logger);
  spdlog::set_default_logger(default_logger);

  // 从配置设置日志级别。set_level 会遍历 registry，之后创建的模块 logger
  // 也从 default logger 继承这个级别
  spdlog::set_level(ToSpdlogLevel(options.level));

  // 自动每秒 flush 一次
  spdlog::flush_every(std::chrono::seconds(1));

  g_initialized = true;

  // 预注册常用模块 logger：懒创建在首个模块首次实例化时偶发不稳定（logger
  // 静默丢弃全部输出），提前在单线程初始化阶段一次建好（表见
  // kPreRegisterModules）
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const char* name : kPreRegisterModules) {
      GetOrCreateModuleLogger(name);
    }
  }

  ReplayBootstrapLogs();

  LOG_INFO("LOGGER", "日志系统初始化完成，日志目录: {}", options.log_dir);
}

void LogCore::ReplayBootstrapLogs() {
  // 回放必须发生在默认 logger 与 sink 都建好之后，否则这些行会掉进空气里；
  // 也放在"初始化完成"那条之前，读起来才是时间顺序
  const std::vector<BootstrapLog::Record> records = BootstrapSink()->Drain();
  if (records.empty()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const BootstrapLog::Record& record : records) {
      // 同上面预注册的理由：别等异步线程跑起来才懒创建
      GetOrCreateModuleLogger(record.module);
    }
  }
  for (const BootstrapLog::Record& record : records) {
    spdlog::logger* logger = GetLogger(record.module);
    if (logger != nullptr) {
      // 前缀标出来源：这些行的时间戳早于"日志系统初始化完成"，不标记会让人
      // 以为是 Init 之后才发生的
      logger->log(record.level, "[启动期] {}", record.text);
    }
  }
}

void LogCore::Shutdown() {
  if (!g_initialized) {
    return;
  }
  LOG_INFO("LOGGER", "日志系统关闭");
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_module_loggers.clear();
  }
  spdlog::shutdown();
  // 放掉本库持有的 sink 引用：否则 sink 比 spdlog 注册表活得久，调用方回收
  // 自己的 history buffer 时，sink 里那个裸指针就悬垂了
  g_console_sink.reset();
  g_file_sink.reset();
  g_extra_sinks.clear();
  g_initialized = false;
}

void LogCore::SetLevel(std::string_view module, LogLevel level) {
  if (!g_initialized) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto it = g_module_loggers.find(module);
  if (it != g_module_loggers.end()) {
    it->second->set_level(ToSpdlogLevel(level));
  }
}

void LogCore::SetAllLevel(LogLevel level) {
  if (!g_initialized) {
    return;
  }
  // set_level 遍历 registry，模块 logger 一并跟随
  spdlog::set_level(ToSpdlogLevel(level));
  std::lock_guard<std::mutex> lock(g_mutex);
  for (auto& pair : g_module_loggers) {
    pair.second->set_level(ToSpdlogLevel(level));
  }
}

void LogCore::ApplyOptions(const LogInitOptions& options) {
  if (!g_initialized) {
    return;
  }
  // 整份重放：级别与两套 pattern 都按传进来的值重设。调用方每次传完整的一份，
  // 免得"改了一个格式开关却把级别按旧值重设了回去"
  spdlog::set_level(ToSpdlogLevel(options.level));
  ApplyPatternToSinks(options);
}

spdlog::logger* LogCore::GetOrCreateModuleLogger(
    const std::string& module_name) {
  const auto it = g_module_loggers.find(module_name);
  if (it != g_module_loggers.end()) {
    return it->second;
  }

  // 按需创建模块 logger，复用 default logger 的 sink
  auto default_logger = spdlog::default_logger();
  if (!default_logger) {
    return nullptr;
  }

  auto module_logger = std::make_shared<spdlog::async_logger>(
      module_name, default_logger->sinks().begin(),
      default_logger->sinks().end(), spdlog::thread_pool(),
      spdlog::async_overflow_policy::block);
  module_logger->set_level(default_logger->level());
  module_logger->flush_on(spdlog::level::err);

  // spdlog 注册表里已有同名 logger 时**复用它，绝不先 drop 再注册**：
  // Init 注册的默认 logger 名字就叫 "default"，而 Qt 的默认日志类别也叫
  // "default"（qtMessageHandler 把 context.category 原样当模块名传进来），
  // 所以这条分支真机上一定会走到。一旦 drop 掉它，spdlog::default_logger()
  // 就成了 nullptr——此后每个新建的模块 logger 都在上面那行拿到 nullptr，
  // 日志静默丢失、一条都不落盘（用例 KeepsLoggingAfterDefaultNameCollision）
  //
  // 空名字不走复用：注册表里那个同名的是 spdlog 自己建的内建 logger，只有
  // 控制台 sink、没有文件 sink，复用它等于日志写丢一半（正常调用不会传空
  // 名字，这只是别让一个退化输入悄悄变成"日志少了一半"）；按普通模块建又会
  // 撞上 register_logger 的重名抛异常，所以一律退回默认 logger
  if (module_name.empty()) {
    return spdlog::default_logger().get();
  }
  if (auto existing = spdlog::get(module_name)) {
    auto* existing_ptr = existing.get();
    g_module_loggers[module_name] = existing_ptr;
    return existing_ptr;
  }

  spdlog::register_logger(module_logger);

  auto* raw_ptr = module_logger.get();
  g_module_loggers[module_name] = raw_ptr;
  return raw_ptr;
}

spdlog::logger* LogCore::GetLogger(std::string_view module) {
  if (!g_initialized) {
    // Init 之前不返回 nullptr（那等于把这段日志扔掉），落进启动缓冲等回放
    return GetBootstrapLogger(module);
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto it = g_module_loggers.find(module);
  if (it != g_module_loggers.end()) {
    return it->second;
  }
  return GetOrCreateModuleLogger(std::string(module));
}
