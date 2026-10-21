#include <gtest/gtest.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QWidget>

#include "markdown_test_wait.h"
#include "markdown_view.h"

// MD_MARKDOWN_CSS 的语义钉在独立进程里：LoadDefaultCss 只在首次调用时
// 读环境变量，同一进程中途设置无效。
//
// 覆盖生效时 CSS 保持覆盖文件、主题只切公式/Mermaid，切换非 Light 主题
// 必须打提示（否则"CSS 亮、公式暗"没有任何线索）
namespace {

QTemporaryDir* g_dir = nullptr;
QStringList g_warnings;

void CaptureWarnings(QtMsgType type, const QMessageLogContext&,
                     const QString& message) {
  if (type == QtWarningMsg) {
    g_warnings << message;
  }
}

QString HtmlOf(MarkdownView& view) {
  QWidget* html = view.findChild<QWidget*>(QStringLiteral("markdownHtml"));
  if (html == nullptr) {
    return {};
  }
  QString text;
  QMetaObject::invokeMethod(html, "html", Q_RETURN_ARG(QString, text));
  return text;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  g_dir = new QTemporaryDir();
  const QString css_path = QDir(g_dir->path()).filePath("override.css");
  {
    QFile css(css_path);
    css.open(QIODevice::WriteOnly | QIODevice::Text);
    css.write("body { color: #123456; }\n");
  }
  qputenv("MD_MARKDOWN_CSS", css_path.toUtf8());
  // 视图默认开启持久缓存：测试模式 + 唯一应用名，别读写真实缓存目录
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("css-override-%1")
          .arg(QDateTime::currentMSecsSinceEpoch()));

  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
       QStringLiteral("/markdown-render"))
      .removeRecursively();
  delete g_dir;
  return result;
}

namespace {

TEST(CssOverrideTest, ThemeSwitchKeepsOverrideCssAndWarns) {
  MarkdownView view;
  RenderWaiter first(&view);
  view.SetContent(QStringLiteral("$x$\n"), QString());
  ASSERT_TRUE(first.Wait());
  ASSERT_TRUE(HtmlOf(view).contains(QStringLiteral("#123456")));

  const quint64 renders = view.renderer_status().formula_context.renders;
  ASSERT_GE(renders, 1U);

  g_warnings.clear();
  const QtMessageHandler old_handler = qInstallMessageHandler(CaptureWarnings);
  RenderWaiter reapply(&view);
  view.SetTheme(MarkdownTheme::Dark);
  qInstallMessageHandler(old_handler);
  ASSERT_TRUE(reapply.Wait());

  EXPECT_EQ(view.theme(), MarkdownTheme::Dark);
  const QString html = HtmlOf(view);
  EXPECT_TRUE(html.contains(QStringLiteral("#123456")))
      << "覆盖文件仍是唯一 CSS 来源";
  EXPECT_FALSE(html.contains(QStringLiteral("#0d1117")))
      << "暗色主题 CSS 不该混进来";
  EXPECT_EQ(view.renderer_status().formula_context.renders, renders + 1)
      << "公式颜色随主题变了，应当重渲染";

  bool warned = false;
  for (const QString& message : g_warnings) {
    if (message.contains(QStringLiteral("MD_MARKDOWN_CSS"))) {
      warned = true;
    }
  }
  EXPECT_TRUE(warned) << "主题与覆盖 CSS 混用时必须提示";
}

}  // namespace
