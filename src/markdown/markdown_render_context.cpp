#include "markdown_render_context.h"

#include "markdown_render_context_internal.h"

struct MarkdownRenderContext::Impl {
  MarkdownRenderContextState state;
};

MarkdownRenderContext::MarkdownRenderContext() : impl_(new Impl) {}

MarkdownRenderContext::~MarkdownRenderContext() = default;

MarkdownRenderContext& DefaultRenderContext() {
  static std::unique_ptr<MarkdownRenderContext> context = [] {
    auto created = std::make_unique<MarkdownRenderContext>();
    MarkdownRenderContextOptions options;
    options.persistent_cache_enabled = false;
    created->SetOptions(options);
    return created;
  }();
  return *context;
}

void MarkdownRenderContext::SetOptions(
    const MarkdownRenderContextOptions& options) {
  impl_->state.options = options;
  impl_->state.highlight_cache.max_entries = options.highlight_cache_entries;
  impl_->state.formula_cache.max_entries = options.formula_cache_entries;
  impl_->state.mermaid_cache.max_entries = options.mermaid_cache_entries;
  // 调小上限立即生效，不让旧条目继续占着
  impl_->state.highlight_cache.Trim();
  impl_->state.formula_cache.Trim();
  impl_->state.mermaid_cache.Trim();
  impl_->state.UpdatePixmapCacheLimit();
}

const MarkdownRenderContextOptions& MarkdownRenderContext::options() const {
  return impl_->state.options;
}

void MarkdownRenderContext::SetDiagnostics(
    const MarkdownDiagnostics& diagnostics) {
  impl_->state.options.diagnostics = diagnostics;
}

MarkdownDiagnostics MarkdownRenderContext::diagnostics() const {
  return impl_->state.options.diagnostics;
}

MarkdownRendererStatus MarkdownRenderContext::status() const {
  MarkdownRendererStatus status;
  status.highlighter = HighlightEngineStatus();
  status.formula = FormulaEngineStatus();
  status.mermaid = MermaidEngineStatus();
  status.highlight_context = impl_->state.highlight_stats;
  status.formula_context = impl_->state.formula_stats;
  status.mermaid_context = impl_->state.mermaid_stats;
  status.image_context = impl_->state.image_stats;
  return status;
}

MarkdownCacheUsage MarkdownRenderContext::cache_usage() const {
  MarkdownCacheUsage usage;
  usage.highlight_entries = impl_->state.highlight_cache.entries.size();
  usage.highlight_bytes = impl_->state.highlight_cache.ByteSize();
  usage.formula_entries = impl_->state.formula_cache.entries.size();
  usage.formula_bytes = impl_->state.formula_cache.ByteSize();
  usage.mermaid_entries = impl_->state.mermaid_cache.entries.size();
  usage.mermaid_bytes = impl_->state.mermaid_cache.ByteSize();
  usage.pixmap_entries = impl_->state.pixmap_cache.size();
  usage.pixmap_bytes = impl_->state.pixmap_cache.totalCost();
  usage.pixmap_limit_bytes = impl_->state.pixmap_cache.maxCost();
  return usage;
}

void MarkdownRenderContext::ClearMemoryCaches() {
  impl_->state.highlight_cache.Clear();
  impl_->state.formula_cache.Clear();
  impl_->state.mermaid_cache.Clear();
  impl_->state.pixmap_cache.clear();
}

void MarkdownRenderContext::ResetStats() {
  impl_->state.highlight_stats = MarkdownContextStatus();
  impl_->state.formula_stats = MarkdownContextStatus();
  impl_->state.mermaid_stats = MarkdownContextStatus();
  impl_->state.image_stats = MarkdownContextStatus();
}

MarkdownRenderContextState& MarkdownRenderContextAccess::State(
    MarkdownRenderContext& context) {
  return context.impl_->state;
}
