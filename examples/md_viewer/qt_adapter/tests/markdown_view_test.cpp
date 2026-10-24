#include "markdown_view.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include <gtest/gtest.h>

// "复制按钮"链路里属于本模块的那一段：copy: 链接 → 要复制的原文。
//
// 为什么只钉这一段：代码块是 qlitehtml 自绘的，合成鼠标事件到不了它的
// 链接命中判定。"点击 → linkClicked 信号"是 qlitehtml 的既有能力（内链
// 跳转一直在用）；信号之后的拦截与查表是本模块写的，所以在这一层钉死。
// 控件侧的接线（把回调挂上、每次换文档重建表）由人工确认。
namespace {

const QHash<QString, QString> kBlocks = {
    {QStringLiteral("0"), QStringLiteral("int a = 1;\n")},
    {QStringLiteral("1"), QStringLiteral("print(2)\n")},
};

TEST(MarkdownViewCopyTest, ResolvesRegisteredIdToRawSource) {
  EXPECT_EQ(CopyTextForLink(QUrl(QStringLiteral("copy:0")), kBlocks),
            QStringLiteral("int a = 1;\n"));
  EXPECT_EQ(CopyTextForLink(QUrl(QStringLiteral("copy:1")), kBlocks),
            QStringLiteral("print(2)\n"));
}

// 查不到的 id（旧文档的链接、手改过的 HTML）返回空串：调用方据此不动剪贴板，
// 宁可没反应，也不能把用户剪贴板里原来的东西清掉
TEST(MarkdownViewCopyTest, UnknownIdYieldsEmpty) {
  EXPECT_TRUE(
      CopyTextForLink(QUrl(QStringLiteral("copy:99")), kBlocks).isEmpty());
}

// 非 copy: 的链接不是复制按钮，一律返回空串（由调用方转发给宿主）
TEST(MarkdownViewCopyTest, OtherSchemesYieldEmpty) {
  EXPECT_TRUE(
      CopyTextForLink(QUrl(QStringLiteral("https://example.com/")), kBlocks)
          .isEmpty());
  EXPECT_TRUE(CopyTextForLink(QUrl(QStringLiteral("file:///tmp/a.md")), kBlocks)
                  .isEmpty());
}

// ---------- 资源回调的两道闸（ReadResourceForUrl） ----------

// 正常路径：文档目录内的图片照读
TEST(MarkdownViewResourceTest, ReadsFileInsideBaseDir) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString image = dir.filePath(QStringLiteral("pic.png"));
  {
    QFile file(image);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("PNG");
  }
  const QByteArray data =
      ReadResourceForUrl(QUrl::fromLocalFile(image), dir.path());
  EXPECT_EQ(data, QByteArray("PNG"));
}

// 文档目录之外的文件拒绝读：<img src="/etc/passwd"> 或 file:///…/.ssh/id_rsa
// 不该被塞进文档
TEST(MarkdownViewResourceTest, RejectsFileOutsideBaseDir) {
  QTemporaryDir base;
  QTemporaryDir other;
  ASSERT_TRUE(base.isValid() && other.isValid());
  const QString secret = other.filePath(QStringLiteral("id_rsa"));
  {
    QFile file(secret);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("secret");
  }
  EXPECT_TRUE(
      ReadResourceForUrl(QUrl::fromLocalFile(secret), base.path()).isEmpty());
  // 前缀相同但不是子目录（/a/b 与 /a/bc）也不许放行
  const QString trick = base.path() + QStringLiteral("x/evil.png");
  QDir().mkpath(QFileInfo(trick).absolutePath());
  {
    QFile file(trick);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("x");
  }
  EXPECT_TRUE(
      ReadResourceForUrl(QUrl::fromLocalFile(trick), base.path()).isEmpty());
}

// 没有基准目录：本地文件一律拒绝（只能吃 data: URI）
TEST(MarkdownViewResourceTest, RejectsLocalFileWithoutBaseDir) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QString image = dir.filePath(QStringLiteral("pic.png"));
  {
    QFile file(image);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("PNG");
  }
  EXPECT_TRUE(
      ReadResourceForUrl(QUrl::fromLocalFile(image), QString()).isEmpty());
}

// data: URI 在 16MB 解码量内正常解
TEST(MarkdownViewResourceTest, DecodesSmallDataUri) {
  const QByteArray png("PNGDATA");
  const QUrl url(QStringLiteral("data:image/png;base64,") +
                 QString::fromLatin1(png.toBase64()));
  EXPECT_EQ(ReadResourceForUrl(url, QString()), png);
}

// 超大 data: URI 拒绝解码：恶意 md 塞 GB 级 base64 会在解码那刻吃满内存
TEST(MarkdownViewResourceTest, RejectsOversizedDataUri) {
  // 编码后 23MB 多，超过 (16MB/3+1)*4 的闸
  QString big = QStringLiteral("data:image/png;base64,");
  big += QString(23 * 1024 * 1024, QLatin1Char('A'));
  EXPECT_TRUE(ReadResourceForUrl(QUrl(big), QString()).isEmpty());
}

}  // namespace
