#ifndef FILE_MARKDOWN_MARKDOWN_RENDER_CONTEXT_INTERNAL_H_
#define FILE_MARKDOWN_MARKDOWN_RENDER_CONTEXT_INTERNAL_H_

#include <limits>

#include <QCache>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QString>

#include "markdown_render_context.h"

// 渲染器内部的字符串缓存（高亮/公式 tag/Mermaid tag）。失败结果也缓存
// 空串：同一个坏输入不该每次编辑都重试。FIFO 淘汰，上限由上下文选项控制
struct RenderStringCache {
  QHash<QString, QString> entries;
  QList<QString> order;
  int max_entries = 256;
  // QString 底层是 UTF-16，按 2 字节/字符估算，供状态面板展示
  qint64 bytes = 0;

  static qint64 Cost(const QString& text) {
    return static_cast<qint64>(text.size()) * 2;
  }

  const QString* Find(const QString& key) const {
    const auto it = entries.constFind(key);
    return it == entries.constEnd() ? nullptr : &it.value();
  }

  void Insert(const QString& key, const QString& value) {
    const auto it = entries.constFind(key);
    if (it != entries.constEnd()) {
      bytes -= Cost(key) + Cost(it.value());
    } else {
      order.append(key);
      bytes += Cost(key);
    }
    entries.insert(key, value);
    bytes += Cost(value);
    Trim();
  }

  // 上限调小后立即生效，不必等下一次 Insert
  void Trim() {
    while (order.size() > max_entries && !order.isEmpty()) {
      const QString key = order.takeFirst();
      const auto it = entries.constFind(key);
      if (it != entries.constEnd()) {
        bytes -= Cost(key) + Cost(it.value());
        entries.remove(key);
      }
    }
  }

  void Clear() {
    entries.clear();
    order.clear();
    bytes = 0;
  }

  qint64 ByteSize() const { return bytes; }
};

// token → 已解码的 pixmap 与 token 化后的 img tag。宿主只按 token 查。
struct PixmapCacheEntry {
  QPixmap pixmap;
  QString img_tag;
};

// 渲染器能看到的上下文状态。引擎状态由本头下方的查询函数提供
struct MarkdownRenderContextState {
  // 诊断配置只存在 options 里，避免两份状态漂移
  MarkdownRenderContextOptions options;
  RenderStringCache highlight_cache;
  RenderStringCache formula_cache;
  RenderStringCache mermaid_cache;
  // 公式/图表图片，跨 ApplyDocument 复用；当前文档的 pin 在 MarkdownView
  QCache<QString, PixmapCacheEntry> pixmap_cache;
  MarkdownContextStatus highlight_stats;
  MarkdownContextStatus formula_stats;
  MarkdownContextStatus mermaid_stats;
  MarkdownContextStatus image_stats;

  // 缓存上限必须与对外 options 一致；RenderStringCache 自带的 256 只是
  // 兜底，不允许成为实际行为（否则 options() 与实际缓存分叉）
  MarkdownRenderContextState() {
    highlight_cache.max_entries = options.highlight_cache_entries;
    formula_cache.max_entries = options.formula_cache_entries;
    mermaid_cache.max_entries = options.mermaid_cache_entries;
    UpdatePixmapCacheLimit();
  }

  // QCache 的 cost 是 int；0/负值沿用模块惯例表示无上限
  void UpdatePixmapCacheLimit() {
    const qint64 bytes = options.image_cache_bytes;
    constexpr qint64 kMaxCost = std::numeric_limits<int>::max();
    pixmap_cache.setMaxCost(bytes > 0 ? static_cast<int>(qMin(bytes, kMaxCost))
                                      : INT_MAX);
  }
};

// 各渲染器的默认（线程局部）引擎状态。查询不触发加载：只读已加载标志与
// 耗时；异步 worker 直接查自己的 EngineState
MarkdownEngineStatus HighlightEngineStatus();
MarkdownEngineStatus FormulaEngineStatus();
MarkdownEngineStatus MermaidEngineStatus();

struct MarkdownRenderContextAccess {
  static MarkdownRenderContextState& State(MarkdownRenderContext& context);
};

#endif  // FILE_MARKDOWN_MARKDOWN_RENDER_CONTEXT_INTERNAL_H_
