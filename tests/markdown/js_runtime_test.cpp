#include "js_runtime.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <thread>

#include <QElapsedTimer>
#include <QString>

// JsRuntime 错误处理的判据：异常消息真的被消费、坏字节码不崩且作废引擎、
// 非异常结果原样放行
namespace {

class ScopedJsContext {
 public:
  ScopedJsContext() : runtime_(JS_NewRuntime()) {
    if (runtime_ != nullptr) {
      context_ = JS_NewContext(runtime_);
    }
  }
  ~ScopedJsContext() {
    if (context_ != nullptr) {
      JS_FreeContext(context_);
    }
    if (runtime_ != nullptr) {
      JS_FreeRuntime(runtime_);
    }
  }
  ScopedJsContext(const ScopedJsContext&) = delete;
  ScopedJsContext& operator=(const ScopedJsContext&) = delete;
  JSContext* context() const { return context_; }

 private:
  JSRuntime* runtime_ = nullptr;
  JSContext* context_ = nullptr;
};

TEST(JsRuntimeTest, TakesPendingExceptionMessage) {
  ScopedJsContext js;
  ASSERT_NE(js.context(), nullptr);
  JS_ThrowTypeError(js.context(), "boom");
  const QString message = JsRuntime::TakeExceptionMessage(js.context());
  EXPECT_TRUE(message.contains(QStringLiteral("boom")))
      << message.toStdString();
}

TEST(JsRuntimeTest, CorruptBytecodeFailsLoad) {
  const uint8_t garbage[] = {0xde, 0xad, 0xbe, 0xef};
  const QList<JsPayload> payloads = {
      JsPayload{garbage, static_cast<uint32_t>(sizeof(garbage)), "garbage"}};
  JsRuntime runtime;
  EXPECT_FALSE(runtime.EnsureLoaded(payloads, {}, {}));
  EXPECT_TRUE(runtime.status().permanently_failed);
}

// 取消后 runtime 不能只留在半死状态：ResetForReload() 必须允许重新加载
TEST(JsRuntimeTest, InterruptStopsEvalAndResetAllowsReload) {
  JsRuntime runtime;
  ASSERT_TRUE(runtime.EnsureLoaded({}, {}, {}));
  std::atomic<bool> cancel{false};
  runtime.SetInterruptFlag(&cancel);

  std::thread canceller([&cancel] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancel.store(true, std::memory_order_relaxed);
  });
  QElapsedTimer clock;
  clock.start();
  const bool eval_ok = runtime.Eval("while (true) {}", "spin", "spin");
  const qint64 elapsed = clock.elapsed();
  canceller.join();

  EXPECT_FALSE(eval_ok);
  EXPECT_LT(elapsed, 2000);
  EXPECT_TRUE(runtime.status().loaded);

  runtime.ResetForReload();
  EXPECT_FALSE(runtime.status().loaded);
  EXPECT_FALSE(runtime.status().permanently_failed);

  cancel.store(false, std::memory_order_relaxed);
  EXPECT_TRUE(runtime.EnsureLoaded({}, {}, {}));
  EXPECT_TRUE(runtime.Eval("1 + 1", "ok", "ok"));
}

// ResetForReload 必须走 on_fail，让渲染器释放自己持有的 JS 引用
TEST(JsRuntimeTest, ResetForReloadInvokesOnFail) {
  JsRuntime runtime;
  int calls = 0;
  runtime.set_on_fail([&calls](JSContext*) { ++calls; });
  ASSERT_TRUE(runtime.EnsureLoaded({}, {}, {}));
  runtime.ResetForReload();
  EXPECT_EQ(calls, 1);
}

TEST(JsRuntimeTest, CheckResultAcceptsNonException) {
  ScopedJsContext js;
  ASSERT_NE(js.context(), nullptr);
  const JSValue value = JS_NewInt32(js.context(), 1);
  QString error;
  EXPECT_TRUE(JsRuntime::CheckResult(js.context(), value,
                                     QStringLiteral("probe"), &error));
  EXPECT_TRUE(error.isEmpty());
  JS_FreeValue(js.context(), value);
}

}  // namespace
