#include "render_cache.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include "markdown_types.h"

// 持久缓存的判据：key 随输入变化、命中/损坏/关闭/淘汰/清理。目录一律用
// 临时目录，绝不碰用户真实的 CacheLocation
namespace {

RenderCacheConfig ConfigFor(const QString& root) {
  RenderCacheConfig config;
  config.enabled = true;
  config.root_directory = root;
  config.max_entries = 4;
  config.max_bytes = 1 << 20;
  return config;
}

QStringList CacheFiles(const QString& root, const QString& kind) {
  return QDir(QDir(root).filePath(kind)).entryList(QDir::Files);
}

const QString kTag = QStringLiteral("<img src=\"data:image/png;base64,AA\">");

}  // namespace

TEST(RenderCacheTest, KeyVariesWithInputs) {
  const QString base =
      MakeRenderCacheKey(QStringLiteral("formula"), QStringLiteral("x"),
                         QStringLiteral("D|#000000"));
  EXPECT_EQ(base.size(), 64);
  EXPECT_EQ(base,
            MakeRenderCacheKey(QStringLiteral("formula"), QStringLiteral("x"),
                               QStringLiteral("D|#000000")));
  // 同一个公式换个颜色/display、换个渲染器或换个源码都必须是新 key
  EXPECT_NE(base,
            MakeRenderCacheKey(QStringLiteral("formula"), QStringLiteral("x"),
                               QStringLiteral("I|#000000")));
  EXPECT_NE(base,
            MakeRenderCacheKey(QStringLiteral("formula"), QStringLiteral("y"),
                               QStringLiteral("D|#000000")));
  EXPECT_NE(base,
            MakeRenderCacheKey(QStringLiteral("mermaid"), QStringLiteral("x"),
                               QStringLiteral("D|#000000")));
}

TEST(RenderCacheTest, StoresAndLoadsEntry) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const RenderCacheConfig config = ConfigFor(dir.path());
  const QString key = MakeRenderCacheKey(
      QStringLiteral("formula"), QStringLiteral("x"), QStringLiteral("v"));

  StoreRenderCacheEntry(config, QStringLiteral("formula"), key, kTag);
  ASSERT_EQ(CacheFiles(dir.path(), QStringLiteral("formula")).size(), 1);

  QString loaded;
  EXPECT_TRUE(
      LoadRenderCacheEntry(config, QStringLiteral("formula"), key, &loaded));
  EXPECT_EQ(loaded, kTag);
}

TEST(RenderCacheTest, CorruptEntriesBecomeMissesAndAreRemoved) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const RenderCacheConfig config = ConfigFor(dir.path());
  const QString key = MakeRenderCacheKey(
      QStringLiteral("formula"), QStringLiteral("x"), QStringLiteral("v"));
  const QString path =
      QDir(QDir(dir.path()).filePath(QStringLiteral("formula")))
          .filePath(key + QStringLiteral(".txt"));
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));

  // 非 <img 内容
  {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("junk");
  }
  QString loaded;
  EXPECT_FALSE(
      LoadRenderCacheEntry(config, QStringLiteral("formula"), key, &loaded));
  EXPECT_FALSE(QFile::exists(path));

  // 空文件
  {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  }
  EXPECT_FALSE(
      LoadRenderCacheEntry(config, QStringLiteral("formula"), key, &loaded));
  EXPECT_FALSE(QFile::exists(path));
}

TEST(RenderCacheTest, EmptyTagIsNeverStored) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const RenderCacheConfig config = ConfigFor(dir.path());
  StoreRenderCacheEntry(
      config, QStringLiteral("formula"),
      MakeRenderCacheKey(QStringLiteral("formula"), QStringLiteral("x"),
                         QStringLiteral("v")),
      QString());
  EXPECT_TRUE(CacheFiles(dir.path(), QStringLiteral("formula")).isEmpty());
}

TEST(RenderCacheTest, DisabledCacheSkipsDisk) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  RenderCacheConfig config = ConfigFor(dir.path());
  config.enabled = false;
  const QString key = MakeRenderCacheKey(
      QStringLiteral("formula"), QStringLiteral("x"), QStringLiteral("v"));
  StoreRenderCacheEntry(config, QStringLiteral("formula"), key, kTag);
  EXPECT_FALSE(
      QDir(QDir(dir.path()).filePath(QStringLiteral("formula"))).exists());

  QString loaded;
  EXPECT_FALSE(
      LoadRenderCacheEntry(config, QStringLiteral("formula"), key, &loaded));
}

TEST(RenderCacheTest, PrunesDownToEightyPercent) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const RenderCacheConfig config = ConfigFor(dir.path());
  // max_entries = 4 → 每写一条就检查一次；超限时删到 80%，之后新写入
  // 可能把数量重新带到上限，但绝不会超过它
  for (int i = 0; i < 8; ++i) {
    const QString key = MakeRenderCacheKey(
        QStringLiteral("formula"), QString::number(i), QStringLiteral("v"));
    StoreRenderCacheEntry(config, QStringLiteral("formula"), key, kTag);
  }
  EXPECT_LE(CacheFiles(dir.path(), QStringLiteral("formula")).size(),
            config.max_entries);
}

TEST(RenderCacheTest, ConfigFollowsOptions) {
  MarkdownRenderContextOptions options;
  options.cache_directory = QStringLiteral("C:/cache-root");
  options.formula_cache_entries = 7;
  options.formula_cache_bytes = 1234;
  options.mermaid_cache_entries = 3;
  options.mermaid_cache_bytes = 4096;

  // 清理入口不区分 kind，只需要根目录
  const RenderCacheConfig root_config = MakeRenderCacheRootConfig(options);
  EXPECT_TRUE(root_config.enabled);
  EXPECT_EQ(root_config.root_directory, options.cache_directory);

  const RenderCacheConfig formula =
      MakeRenderCacheConfig(options, QStringLiteral("formula"));
  EXPECT_TRUE(formula.enabled);
  EXPECT_EQ(formula.root_directory, options.cache_directory);
  EXPECT_EQ(formula.max_entries, 7);
  EXPECT_EQ(formula.max_bytes, 1234);

  const RenderCacheConfig mermaid =
      MakeRenderCacheConfig(options, QStringLiteral("mermaid"));
  EXPECT_EQ(mermaid.max_entries, 3);
  EXPECT_EQ(mermaid.max_bytes, 4096);

  options.persistent_cache_enabled = false;
  EXPECT_FALSE(
      MakeRenderCacheConfig(options, QStringLiteral("formula")).enabled);
  EXPECT_FALSE(MakeRenderCacheConfig(options, QStringLiteral("other")).enabled);
}

TEST(RenderCacheTest, ClearKeepsForeignFilesInExplicitDirectory) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const RenderCacheConfig config = ConfigFor(dir.path());
  ASSERT_TRUE(
      QDir().mkpath(QDir(dir.path()).filePath(QStringLiteral("formula"))));
  ASSERT_TRUE(
      QDir().mkpath(QDir(dir.path()).filePath(QStringLiteral("mermaid"))));
  {
    QFile formula(QDir(dir.path()).filePath(QStringLiteral("formula/a.txt")));
    ASSERT_TRUE(formula.open(QIODevice::WriteOnly));
    formula.write(kTag.toUtf8());
  }
  {
    QFile mermaid(QDir(dir.path()).filePath(QStringLiteral("mermaid/b.txt")));
    ASSERT_TRUE(mermaid.open(QIODevice::WriteOnly));
    mermaid.write(kTag.toUtf8());
  }
  const QString foreign = QDir(dir.path()).filePath(QStringLiteral("keep.txt"));
  {
    QFile file(foreign);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("keep");
  }

  ClearRenderCacheDirectory(config);
  EXPECT_FALSE(
      QDir(QDir(dir.path()).filePath(QStringLiteral("formula"))).exists());
  EXPECT_FALSE(
      QDir(QDir(dir.path()).filePath(QStringLiteral("mermaid"))).exists());
  EXPECT_TRUE(QFile::exists(foreign)) << "显式目录不是本模块独占，不能整目录删";
}
