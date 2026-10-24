#include "md_js_runtime.h"

#include <chrono>
#include <cstdint>
#include <utility>

namespace {

// Matches the formula/mermaid recursion budget: the JS quota is soft, the
// host thread stack is hard.
constexpr std::size_t kJsMaxStackSize = 16U << 20;

int JsInterruptHandler(JSRuntime*, void* opaque) {
  const auto* flag = static_cast<const std::atomic<bool>*>(opaque);
  return (flag != nullptr && flag->load(std::memory_order_relaxed)) ? 1 : 0;
}

int64_t ElapsedMs(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}

// qjsc bytecode: ReadObject + EvalFunction. EvalFunction takes ownership of
// obj on success; obj must not be freed twice.
bool EvalBytecode(JSContext* context, const uint8_t* data, uint32_t size,
                  const char* name, std::string* error) {
  JSValue obj = JS_ReadObject(context, data, size, JS_READ_OBJ_BYTECODE);
  if (JS_IsException(obj)) {
    const std::string message = MdJsRuntime::TakeExceptionMessage(context);
    if (error != nullptr) {
      *error = "bytecode read failed: " + std::string(name) + ": " + message;
    }
    JS_FreeValue(context, obj);
    return false;
  }
  const JSValue value = JS_EvalFunction(context, obj);
  if (JS_IsException(value)) {
    const std::string message = MdJsRuntime::TakeExceptionMessage(context);
    if (error != nullptr) {
      *error = "bytecode eval failed: " + std::string(name) + ": " + message;
    }
    JS_FreeValue(context, value);
    return false;
  }
  JS_FreeValue(context, value);
  return true;
}

}  // namespace

std::string MdJsRuntime::TakeExceptionMessage(JSContext* context) {
  if (context == nullptr) {
    return "no JS context";
  }
  const JSValue exception = JS_GetException(context);
  const char* raw = JS_ToCString(context, exception);
  std::string message = raw != nullptr ? raw : "?";
  if (raw != nullptr) {
    JS_FreeCString(context, raw);
  }
  JS_FreeValue(context, exception);
  return message;
}

bool MdJsRuntime::CheckResult(JSContext* context, JSValue value,
                              std::string_view label, std::string* error) {
  if (!JS_IsException(value)) {
    return true;
  }
  const std::string message = TakeExceptionMessage(context);
  if (error != nullptr) {
    *error = std::string(label) + ": " + message;
  }
  return false;
}

MdJsRuntime::~MdJsRuntime() { FailLoad(); }

void MdJsRuntime::set_on_fail(std::function<void(JSContext*)> on_fail) {
  on_fail_ = std::move(on_fail);
}

void MdJsRuntime::SetRuntimeOpaque(void* opaque) {
  runtime_opaque_ = opaque;
  if (runtime_ != nullptr) {
    JS_SetRuntimeOpaque(runtime_, opaque);
  }
}

void MdJsRuntime::set_preserve_first_load(bool preserve) {
  preserve_first_load_ = preserve;
}

bool MdJsRuntime::EnsureLoaded(
    const std::vector<MdJsPayload>& payloads,
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
    load_start_ = std::chrono::steady_clock::now();
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
  for (const MdJsPayload& payload : payloads) {
    if (!EvalBytecode(context_, payload.data, payload.size, payload.name,
                      nullptr)) {
      return FailLoad();
    }
  }
  if (after_load && !after_load(context_)) {
    return FailLoad();
  }

  first_load_ms_ = ElapsedMs(load_start_);
  return true;
}

bool MdJsRuntime::Eval(std::string_view code, const char* script_name,
                       std::string* error) {
  if (context_ == nullptr) {
    if (error != nullptr) {
      *error = std::string(script_name) + ": no JS context";
    }
    return false;
  }
  const JSValue value = JS_Eval(context_, code.data(), code.size(), script_name,
                                JS_EVAL_TYPE_GLOBAL);
  const bool ok = CheckResult(context_, value, script_name, error);
  JS_FreeValue(context_, value);
  return ok;
}

MdJsEngineStatus MdJsRuntime::status() const {
  MdJsEngineStatus status;
  status.loaded = context_ != nullptr;
  status.permanently_failed = load_failed_;
  status.first_load_ms = first_load_ms_;
  return status;
}

void MdJsRuntime::Abort() { FailLoad(); }

void MdJsRuntime::SetInterruptFlag(std::atomic<bool>* flag) {
  interrupt_flag_ = flag;
  if (runtime_ != nullptr) {
    JS_SetInterruptHandler(runtime_, &JsInterruptHandler, flag);
  }
}

void MdJsRuntime::ResetForReload() {
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
}

bool MdJsRuntime::FailLoad() {
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
  if (!preserve_first_load_ || first_load_ms_ == 0) {
    first_load_ms_ = load_attempted_ ? ElapsedMs(load_start_) : 0;
  }
  return false;
}
