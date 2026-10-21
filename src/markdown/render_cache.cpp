#include "render_cache.h"

#include <algorithm>
#include <cstring>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

#include "markdown_bytecode.h"

namespace {

// 只覆盖 C++ 后处理逻辑的变化：改了清理/内联/画图逻辑就加一
constexpr int kRenderPipelineVersion = 1;

// 每写 N 条才做一次目录扫描；上限小（测试）时每次写都查
int PruneCheckEvery(const RenderCacheConfig& config) {
  if (config.max_entries <= 0) {
    return 32;
  }
  return qBound(1, config.max_entries / 4, 32);
}

QHash<QString, int> g_writes_since_prune;

QString KindDirectory(const RenderCacheConfig& config, const QString& kind) {
  const QString root = RenderCacheRoot(config);
  if (root.isEmpty()) {
    return {};
  }
  return QDir(root).filePath(kind);
}

void PruneDirectory(const QString& directory, const RenderCacheConfig& config) {
  QFileInfoList files =
      QDir(directory).entryInfoList(QDir::Files, QDir::NoSort);
  if (files.isEmpty()) {
    return;
  }
  qint64 total_bytes = 0;
  for (const QFileInfo& info : files) {
    total_bytes += info.size();
  }
  const int target_entries =
      config.max_entries > 0 ? qMax(1, config.max_entries * 8 / 10) : 0;
  const qint64 target_bytes =
      config.max_bytes > 0 ? config.max_bytes * 8 / 10 : 0;
  const bool over_entries =
      config.max_entries > 0 && files.size() > config.max_entries;
  const bool over_bytes =
      config.max_bytes > 0 && total_bytes > config.max_bytes;
  if (!over_entries && !over_bytes) {
    return;
  }

  std::sort(files.begin(), files.end(),
            [](const QFileInfo& left, const QFileInfo& right) {
              return left.lastModified() < right.lastModified();
            });
  int remaining = files.size();
  for (const QFileInfo& info : files) {
    const bool entries_ok =
        config.max_entries <= 0 || remaining <= target_entries;
    const bool bytes_ok = config.max_bytes <= 0 || total_bytes <= target_bytes;
    if (entries_ok && bytes_ok) {
      break;
    }
    if (QFile::remove(info.absoluteFilePath())) {
      --remaining;
      total_bytes -= info.size();
    }
  }
}

QByteArray PayloadHash(const QString& kind) {
  static QHash<QString, QByteArray> cache;
  const auto cached = cache.constFind(kind);
  if (cached != cache.constEnd()) {
    return *cached;
  }

  // 载荷哈希对象是 qjsc 编出的字节码数组（渲染器实际执行的内容）。
  // 名字参与哈希：载荷集合/顺序变化也要换 key
  struct BytecodePayload {
    const char* name;
    const uint8_t* data;
    uint32_t size;
  };
  QList<BytecodePayload> payloads;
  if (kind == QLatin1String("formula")) {
    payloads << BytecodePayload{"mathjax-svg.min.js", md_mj, md_mj_size};
  } else if (kind == QLatin1String("mermaid")) {
    payloads << BytecodePayload{"path_bbox.js", md_pb, md_pb_size}
             << BytecodePayload{"dom_shim.js", md_ds, md_ds_size}
             << BytecodePayload{"mermaid.js", md_mm, md_mm_size};
  }

  QCryptographicHash hash(QCryptographicHash::Sha256);
  for (const BytecodePayload& payload : payloads) {
    hash.addData(payload.name, strlen(payload.name));
    hash.addData(reinterpret_cast<const char*>(payload.data), payload.size);
  }
  const QByteArray result = hash.result();
  cache.insert(kind, result);
  return result;
}

}  // namespace

RenderCacheConfig MakeRenderCacheRootConfig(
    const MarkdownRenderContextOptions& options) {
  RenderCacheConfig config;
  config.enabled = options.persistent_cache_enabled;
  config.root_directory = options.cache_directory;
  return config;
}

RenderCacheConfig MakeRenderCacheConfig(
    const MarkdownRenderContextOptions& options, const QString& kind) {
  RenderCacheConfig config = MakeRenderCacheRootConfig(options);
  if (kind == QLatin1String("formula")) {
    config.max_entries = options.formula_cache_entries;
    config.max_bytes = options.formula_cache_bytes;
  } else if (kind == QLatin1String("mermaid")) {
    config.max_entries = options.mermaid_cache_entries;
    config.max_bytes = options.mermaid_cache_bytes;
  } else {
    config.enabled = false;
  }
  return config;
}

QString RenderCacheRoot(const RenderCacheConfig& config) {
  if (!config.root_directory.isEmpty()) {
    return config.root_directory;
  }
  const QString location =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (location.isEmpty()) {
    return {};
  }
  return QDir(location).filePath(QStringLiteral("markdown-render"));
}

QString MakeRenderCacheKey(const QString& kind, const QString& payload,
                           const QString& variant) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(QByteArray::number(kRenderPipelineVersion));
  hash.addData(PayloadHash(kind));
  hash.addData(QByteArray(QT_VERSION_STR));
  hash.addData(kind.toUtf8());
  hash.addData(variant.toUtf8());
  hash.addData(payload.toUtf8());
  return QString::fromLatin1(hash.result().toHex());
}

bool LoadRenderCacheEntry(const RenderCacheConfig& config, const QString& kind,
                          const QString& key, QString* tag) {
  if (!config.enabled || tag == nullptr) {
    return false;
  }
  const QString directory = KindDirectory(config, kind);
  if (directory.isEmpty()) {
    return false;
  }
  const QString path = QDir(directory).filePath(key + QStringLiteral(".txt"));
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const qint64 size = file.size();
  // 空文件、超过单条预算、非 <img 开头或截断的内容都按 miss 处理，
  // 删掉让下一次写入覆盖
  const bool bad_size =
      size <= 0 || (config.max_bytes > 0 && size > config.max_bytes);
  const QByteArray data = bad_size ? QByteArray() : file.readAll();
  const QString text = QString::fromUtf8(data);
  if (bad_size || !text.startsWith(QLatin1String("<img")) ||
      !text.endsWith(QLatin1Char('>'))) {
    file.close();
    QFile::remove(path);
    return false;
  }
  *tag = text;
  return true;
}

void StoreRenderCacheEntry(const RenderCacheConfig& config, const QString& kind,
                           const QString& key, const QString& tag) {
  if (!config.enabled || tag.isEmpty()) {
    return;
  }
  const QString directory = KindDirectory(config, kind);
  if (directory.isEmpty() || !QDir().mkpath(directory)) {
    qWarning() << "markdown: cannot create render cache directory" << directory;
    return;
  }
  QSaveFile file(QDir(directory).filePath(key + QStringLiteral(".txt")));
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "markdown: cannot write render cache entry";
    return;
  }
  const QByteArray data = tag.toUtf8();
  if (file.write(data) != data.size() || !file.commit()) {
    qWarning() << "markdown: render cache commit failed";
    return;
  }

  int& writes = g_writes_since_prune[directory];
  ++writes;
  if (writes >= PruneCheckEvery(config)) {
    writes = 0;
    PruneDirectory(directory, config);
  }
}

void ClearRenderCacheDirectory(const RenderCacheConfig& config) {
  const QString root = RenderCacheRoot(config);
  if (root.isEmpty()) {
    return;
  }
  // 一律只清本模块的两个子目录：默认根目录虽然由本模块独占，但保留空
  // 根目录无害；显式目录则可能是宿主给的共享位置，更不能整棵删
  const QDir root_dir(root);
  for (const char* kind : {"formula", "mermaid"}) {
    QDir(root_dir.filePath(QLatin1String(kind))).removeRecursively();
  }
  g_writes_since_prune.clear();
}
