#ifndef FILE_MARKDOWN_RENDER_CACHE_H_
#define FILE_MARKDOWN_RENDER_CACHE_H_

#include <QString>

#include "markdown_types.h"

// 持久化渲染缓存的配置快照。目录为空时用
// QStandardPaths::CacheLocation + "/markdown-render"
struct RenderCacheConfig {
  bool enabled = false;
  QString root_directory;
  int max_entries = 0;
  qint64 max_bytes = 0;
};

// 只取根目录的配置（清理等不区分 kind 的操作）。容量字段为 0，enabled
// 跟随选项
RenderCacheConfig MakeRenderCacheRootConfig(
    const MarkdownRenderContextOptions& options);

// 从上下文选项取某个渲染器的配置。kind 只认 "formula" / "mermaid"，
// 其他值返回 disabled（避免误把别的缓存写进不该写的目录）
RenderCacheConfig MakeRenderCacheConfig(
    const MarkdownRenderContextOptions& options, const QString& kind);

QString RenderCacheRoot(const RenderCacheConfig& config);

// key = SHA-256(流水线版本 + qrc 载荷哈希 + Qt 版本 + kind + variant +
// payload)。载荷只读字节做哈希，不求值，新进程首次查盘前就能算出来
QString MakeRenderCacheKey(const QString& kind, const QString& payload,
                           const QString& variant);

// 条目是自包含二进制：8B magic + layout_w/h/png_len（各 4B 小端）+ PNG。
// magic 不符（含 v1 的 base64 tag 文本格式）按 miss 处理并删文件
bool LoadRenderCacheEntry(const RenderCacheConfig& config, const QString& kind,
                          const QString& key, CachedImage* image);

// 空 image（渲染失败）不落盘；QSaveFile 原子替换，失败只记日志
void StoreRenderCacheEntry(const RenderCacheConfig& config, const QString& kind,
                           const QString& key, const CachedImage& image);

// 只删本模块的 formula/ 与 mermaid/ 子目录，不动根目录里的其他文件
void ClearRenderCacheDirectory(const RenderCacheConfig& config);

#endif  // FILE_MARKDOWN_RENDER_CACHE_H_
