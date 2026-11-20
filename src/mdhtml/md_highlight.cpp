#include "md_highlight.h"

#include <quickjs.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "md_bytecode.h"
#include "md_js_runtime.h"

namespace {

// Escapes source that was not highlighted. The apostrophe form matches the
// themes' expectation and the previous Qt module byte for byte.
std::string EscapeHighlighted(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#x27;";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

void ReleaseHighlightFn(JSContext* context, JSValue* fn) {
  if (context != nullptr && !JS_IsUndefined(*fn)) {
    JS_FreeValue(context, *fn);
  }
  *fn = JS_UNDEFINED;
}

}  // namespace

struct MdHighlightEngine::Impl {
  MdJsRuntime runtime;
  // Parsed once after the payload loads; the payload exports __highlight.
  JSValue highlight_fn = JS_UNDEFINED;
};

MdHighlightEngine::MdHighlightEngine() : impl_(new Impl) {}

MdHighlightEngine::~MdHighlightEngine() {
  if (impl_) {
    ReleaseHighlightFn(impl_->runtime.context(), &impl_->highlight_fn);
  }
}

bool MdHighlightEngine::EnsureLoaded() {
  Impl& impl = *impl_;
  if (impl.runtime.status().loaded) {
    return true;
  }
  if (impl.runtime.status().permanently_failed) {
    return false;
  }
  impl.runtime.set_on_fail([&impl](JSContext* context) {
    ReleaseHighlightFn(context, &impl.highlight_fn);
  });
  return impl.runtime.EnsureLoaded(
      {MdJsPayload{md_hl, md_hl_size, "highlight.min.js"}}, nullptr,
      [&impl](JSContext* context) {
        // Only resolve the function once: a payload without __highlight means
        // the bundled resource and this code disagree.
        JSValue global = JS_GetGlobalObject(context);
        impl.highlight_fn = JS_GetPropertyStr(context, global, "__highlight");
        JS_FreeValue(context, global);
        return JS_IsFunction(context, impl.highlight_fn);
      });
}

void MdHighlightEngine::SetInterruptFlag(std::atomic<bool>* flag) {
  impl_->runtime.SetInterruptFlag(flag);
}

std::string MdHighlightCode(std::string_view code, std::string_view language,
                            MdHighlightEngine* engine) {
  if (code.empty()) {
    return {};
  }
  if (engine == nullptr) {
    // One-off calls (tests, diagnostics) still need a runtime; QuickJS is not
    // thread-safe, so the fallback lives on the calling thread.
    static thread_local MdHighlightEngine fallback;
    engine = &fallback;
  }
  if (!engine->EnsureLoaded()) {
    return EscapeHighlighted(code);
  }
  MdHighlightEngine::Impl& impl = *engine->impl_;
  JSContext* js = impl.runtime.context();
  if (js == nullptr) {
    return EscapeHighlighted(code);
  }

  // Arguments go in as JS strings: no script text, no globals, no escaping.
  JSValue argv[2] = {
      JS_NewStringLen(js, code.data(), static_cast<int>(code.size())),
      JS_NewStringLen(js, language.data(), static_cast<int>(language.size())),
  };
  if (JS_IsException(argv[0]) || JS_IsException(argv[1])) {
    // Allocation failure returns an exception sentinel; clear it and fall
    // back to plain escaping.
    (void)MdJsRuntime::TakeExceptionMessage(js);
    JS_FreeValue(js, argv[0]);
    JS_FreeValue(js, argv[1]);
    return EscapeHighlighted(code);
  }
  const JSValue value = JS_Call(js, impl.highlight_fn, JS_UNDEFINED, 2, argv);
  JS_FreeValue(js, argv[0]);
  JS_FreeValue(js, argv[1]);

  std::string result;
  if (MdJsRuntime::CheckResult(js, value, "highlight call", nullptr)) {
    // __highlight returns null for unregistered languages; JS_ToCString would
    // turn that into the literal "null".
    if (!JS_IsNull(value) && !JS_IsUndefined(value)) {
      const char* text = JS_ToCString(js, value);
      if (text != nullptr) {
        result.assign(text);
        JS_FreeCString(js, text);
      }
    }
  }
  JS_FreeValue(js, value);

  // Empty results (unknown language, JS exception) fall back to escaping; an
  // unknown language is a normal path, not a failure.
  if (result.empty()) {
    return EscapeHighlighted(code);
  }
  return result;
}

std::string MdHighlightRenderKey(std::string_view code,
                                 std::string_view language) {
  std::string key(language);
  key.push_back('\n');
  key.append(code);
  return key;
}
