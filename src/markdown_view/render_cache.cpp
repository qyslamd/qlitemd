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
#include <QtEndian>

#include "markdown_bytecode.h"

namespace {

// 只覆盖 C++ 后处理逻辑的变化：改了清理/内联/画图逻辑就加一。
// v2：条目从 base64 tag 文本换成 CachedImage 二进制
// v3：SVG 按 intrinsic/target 缩放，修复 pt 单位公式右侧裁切
constexpr int kRenderPipelineVersion = 3;

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
#if !defined(QLITEMD_FORMULA_BACKEND_RATEX)
  if (kind == QLatin1String("formula")) {
    payloads << BytecodePayload{"mathjax-svg.min.js", md_mj, md_mj_size};
  }
#endif
#if !defined(QLITEMD_MERMAID_BACKEND_MERMAN)
  if (kind == QLatin1String("mermaid")) {
    payloads << BytecodePayload{"path_bbox.js", md_pb, md_pb_size}
             << BytecodePayload{"dom_shim.js", md_ds, md_ds_size}
             << BytecodePayload{"mermaid.js", md_mm, md_mm_size};
  }
#endif
  QCryptographicHash hash(QCryptographicHash::Sha256);
  for (const BytecodePayload& payload : payloads) {
    hash.addData(payload.name, strlen(payload.name));
    hash.addData(reinterpret_cast<const char*>(payload.data), payload.size);
  }
  if (payloads.isEmpty()) {
    // 原生后端：把引擎标识写进 key，引擎升级时旧条目自然失效
#if defined(QLITEMD_FORMULA_BACKEND_RATEX)
    if (kind == QLatin1String("formula")) {
      hash.addData("ratex-0.1.15");
    }
#endif
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

namespace {

// 条目布局：magic[8] + layout_w[i32le] + layout_h[i32le] + png_len[i32le]
// + PNG 字节。自包含，无 sidecar
constexpr char kImageMagic[8] = {'Q', 'L', 'I', 'T', 'I', 'M', 'G', '1'};
constexpr int kImageHeaderSize = 8 + 3 * 4;

void AppendInt32(QByteArray* out, qint32 value) {
  char raw[4];
  qToLittleEndian(value, raw);
  out->append(raw, 4);
}

qint32 ReadInt32(const char* data) { return qFromLittleEndian<qint32>(data); }

}  // namespace

bool LoadRenderCacheEntry(const RenderCacheConfig& config, const QString& kind,
                          const QString& key, CachedImage* image) {
  if (!config.enabled || image == nullptr) {
    return false;
  }
  const QString directory = KindDirectory(config, kind);
  if (directory.isEmpty()) {
    return false;
  }
  const QString path = QDir(directory).filePath(key + QStringLiteral(".bin"));
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const qint64 size = file.size();
  // 空文件、超过单条预算、magic/长度不符都按 miss 处理，删掉让下一次
  // 写入覆盖（v1 的 .txt base64 条目因此自然淘汰）
  const bool bad_size = size <= kImageHeaderSize ||
                        (config.max_bytes > 0 && size > config.max_bytes);
  const QByteArray data = bad_size ? QByteArray() : file.readAll();
  if (bad_size || std::memcmp(data.constData(), kImageMagic, 8) != 0) {
    file.close();
    QFile::remove(path);
    return false;
  }
  const qint32 width = ReadInt32(data.constData() + 8);
  const qint32 height = ReadInt32(data.constData() + 12);
  const qint32 png_len = ReadInt32(data.constData() + 16);
  if (width < 1 || height < 1 || png_len < 1 ||
      qint64(png_len) != size - kImageHeaderSize) {
    file.close();
    QFile::remove(path);
    return false;
  }
  image->png_bytes = data.mid(kImageHeaderSize, png_len);
  image->layout_width = width;
  image->layout_height = height;
  return true;
}

void StoreRenderCacheEntry(const RenderCacheConfig& config, const QString& kind,
                           const QString& key, const CachedImage& image) {
  if (!config.enabled || image.isNull()) {
    return;
  }
  const QString directory = KindDirectory(config, kind);
  if (directory.isEmpty() || !QDir().mkpath(directory)) {
    qWarning() << "markdown: cannot create render cache directory" << directory;
    return;
  }
  QByteArray data;
  data.reserve(kImageHeaderSize + image.png_bytes.size());
  data.append(kImageMagic, 8);
  AppendInt32(&data, image.layout_width);
  AppendInt32(&data, image.layout_height);
  AppendInt32(&data, static_cast<qint32>(image.png_bytes.size()));
  data.append(image.png_bytes);
  QSaveFile file(QDir(directory).filePath(key + QStringLiteral(".bin")));
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "markdown: cannot write render cache entry";
    return;
  }
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
