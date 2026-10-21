#include "js_runtime.h"

#include <cstdint>

#include <QDebug>
#include <QElapsedTimer>

namespace {

// 与 formula/mermaid 的递归深度配对：JS 配额是软上限，宿主线程栈是硬的
constexpr size_t kJsMaxStackSize = 16U << 20;

int JsInterruptHandler(JSRuntime*, void* opaque) {
  const auto* flag = static_cast<const std::atomic<bool>*>(opaque);
  return (flag != nullptr && flag->load(std::memory_order_relaxed)) ? 1 : 0;
}

// qjsc 字节码：ReadObject + EvalFunction。失败时 error 带原因；
// EvalFunction 成功后接管 obj 所有权，内部不再 Free obj
bool EvalBytecode(JSContext* context, const uint8_t* data, uint32_t size,
                  const QString& name, QString* error) {
  JSValue obj = JS_ReadObject(context, data, size, JS_READ_OBJ_BYTECODE);
  if (JS_IsException(obj)) {
    const QString message = JsRuntime::TakeExceptionMessage(context);
    qWarning() << "bytecode read failed:" << name << message;
    if (error != nullptr) {
      *error = message;
    }
    JS_FreeValue(context, obj);  // 异常哨兵，Free 是 no-op
    return false;
  }
  // EvalFunction 接管 obj 所有权，成功后不能再 Free obj
  const JSValue value = JS_EvalFunction(context, obj);
  if (JS_IsException(value)) {
    const QString message = JsRuntime::TakeExceptionMessage(context);
    qWarning() << "bytecode eval failed:" << name << message;
    if (error != nullptr) {
      *error = message;
    }
    JS_FreeValue(context, value);
    return false;
  }
  JS_FreeValue(context, value);
  return true;
}

}  // namespace

QString JsRuntime::TakeExceptionMessage(JSContext* context) {
  if (context == nullptr) {
    return QStringLiteral("no JS context");
  }
  const JSValue exception = JS_GetException(context);
  const char* raw = JS_ToCString(context, exception);
  const QString message = QString::fromUtf8(raw != nullptr ? raw : "?");
  if (raw != nullptr) {
    JS_FreeCString(context, raw);
  }
  JS_FreeValue(context, exception);
  return message;
}

bool JsRuntime::CheckResult(JSContext* context, JSValue value,
                            const QString& label, QString* error) {
  if (!JS_IsException(value)) {
    return true;
  }
  const QString message = TakeExceptionMessage(context);
  qWarning() << label << "failed:" << message;
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

JsRuntime::~JsRuntime() { FailLoad(); }

void JsRuntime::set_on_fail(std::function<void(JSContext*)> on_fail) {
  on_fail_ = std::move(on_fail);
}

void JsRuntime::SetRuntimeOpaque(void* opaque) {
  runtime_opaque_ = opaque;
  if (runtime_ != nullptr) {
    JS_SetRuntimeOpaque(runtime_, opaque);
  }
}

void JsRuntime::set_preserve_first_load(bool preserve) {
  preserve_first_load_ = preserve;
}

bool JsRuntime::EnsureLoaded(
    const QList<JsPayload>& payloads,
    const std::function<bool(JSContext*)>& prepare,
    const std::function<bool(JSContext*)>& after_load) {
  if (context_ != nullptr) {
    return true;
  }
  if (load_failed_) {
    return false;
  }
  if (!load_attempted_) {
    load_attempted_ = true;
    load_clock_.start();
  }

  runtime_ = JS_NewRuntime();
  if (runtime_ == nullptr) {
    return FailLoad();
  }
  JS_SetMaxStackSize(runtime_, kJsMaxStackSize);
  JS_SetRuntimeOpaque(runtime_, runtime_opaque_);
  if (interrupt_flag_ != nullptr) {
    JS_SetInterruptHandler(runtime_, &JsInterruptHandler, interrupt_flag_);
  }
  context_ = JS_NewContext(runtime_);
  if (context_ == nullptr) {
    return FailLoad();
  }

  if (prepare && !prepare(context_)) {
    return FailLoad();
  }
  for (const JsPayload& payload : payloads) {
    QString error;
    if (!EvalBytecode(context_, payload.data, payload.size,
                      QString::fromUtf8(payload.name), &error)) {
      return FailLoad();
    }
  }
  if (after_load && !after_load(context_)) {
    return FailLoad();
  }

  first_load_ms_ = load_clock_.elapsed();
  return true;
}

bool JsRuntime::Eval(const QByteArray& code, const char* script_name,
                     const QString& label) {
  if (context_ == nullptr) {
    qWarning() << label << "failed: no JS context";
    return false;
  }
  const JSValue value = JS_Eval(context_, code.constData(), code.size(),
                                script_name, JS_EVAL_TYPE_GLOBAL);
  QString error;
  const bool ok = CheckResult(context_, value, label, &error);
  JS_FreeValue(context_, value);
  return ok;
}

qint64 JsRuntime::MemoryBytes() const {
  if (runtime_ == nullptr) {
    return 0;
  }
  JSMemoryUsage usage{};
  JS_ComputeMemoryUsage(runtime_, &usage);
  return static_cast<qint64>(usage.malloc_size);
}

MarkdownEngineStatus JsRuntime::status() const {
  MarkdownEngineStatus status;
  status.loaded = context_ != nullptr;
  status.permanently_failed = load_failed_;
  status.first_load_ms = first_load_ms_;
  return status;
}

void JsRuntime::Abort() { FailLoad(); }

void JsRuntime::SetInterruptFlag(std::atomic<bool>* flag) {
  interrupt_flag_ = flag;
  if (runtime_ != nullptr) {
    JS_SetInterruptHandler(runtime_, &JsInterruptHandler, flag);
  }
}

void JsRuntime::ResetForReload() {
  if (context_ != nullptr) {
    if (on_fail_) {
      on_fail_(context_);
    }
    JS_FreeContext(context_);
    context_ = nullptr;
  }
  if (runtime_ != nullptr) {
    JS_FreeRuntime(runtime_);
    runtime_ = nullptr;
  }
  load_failed_ = false;
  load_attempted_ = false;
  first_load_ms_ = 0;
  load_clock_ = QElapsedTimer();
}

bool JsRuntime::FailLoad() {
  if (context_ != nullptr) {
    if (on_fail_) {
      on_fail_(context_);
    }
    JS_FreeContext(context_);
    context_ = nullptr;
  }
  if (runtime_ != nullptr) {
    JS_FreeRuntime(runtime_);
    runtime_ = nullptr;
  }
  load_failed_ = true;
  // mermaid 的主题 initialize 失败也会走到这里：保留已经测到的首次加载耗时；
  // 其他引擎沿用"失败时刻覆盖"的旧行为
  if (!preserve_first_load_ || first_load_ms_ == 0) {
    first_load_ms_ = load_clock_.isValid() ? load_clock_.elapsed() : 0;
  }
  return false;
}
