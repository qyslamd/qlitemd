#include <gtest/gtest.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "markdown_test_wait.h"
#include "markdown_view.h"
#include "render_status_panel.h"

// 渲染状态面板的刷新链路：文档重建后 outlineChanged 触发 RefreshStatus，
// Mermaid 的计数必须已经落进上下文。独立进程方便控制环境
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("status-panel-%1")
          .arg(QDateTime::currentMSecsSinceEpoch()));
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
       QStringLiteral("/markdown-render"))
      .removeRecursively();
  return result;
}

namespace {

TEST(RenderStatusPanelTest, MermaidCountersReachTheStatusLabel) {
  MarkdownView view;
  // 关掉磁盘缓存：否则可能直接命中旧条目，renders 仍是 0
  view.SetPersistentRenderCacheEnabled(false);
  RenderStatusPanel panel(&view);

  RenderWaiter wait(&view);
  view.SetContent(
      QStringLiteral("```mermaid\nflowchart LR\n    A --> B\n```\n"),
      QString());
  ASSERT_TRUE(wait.Wait());

  auto* label = panel.findChild<QLabel*>(QStringLiteral("statusMermaid"));
  ASSERT_NE(label, nullptr);
  EXPECT_TRUE(label->text().contains(QStringLiteral("调用 1")))
      << label->text().toStdString();
  EXPECT_TRUE(label->text().contains(QStringLiteral("渲染 1")))
      << label->text().toStdString();
  EXPECT_TRUE(label->text().contains(QStringLiteral("已加载")))
      << label->text().toStdString();
}

// 磁盘命中时引擎不加载、renders 不增长；状态必须把"调用"和"命中缓存"
// 显示出来，否则看起来像完全没统计
TEST(RenderStatusPanelTest, DiskCacheHitStillShowsActivity) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  {
    MarkdownView warm;
    warm.SetRenderCacheDirectory(dir.path());
    RenderWaiter wait(&warm);
    warm.SetContent(
        QStringLiteral("```mermaid\nflowchart LR\n    C --> D\n```\n"),
        QString());
    ASSERT_TRUE(wait.Wait());
  }

  MarkdownView view;
  view.SetRenderCacheDirectory(dir.path());
  RenderStatusPanel panel(&view);
  RenderWaiter wait(&view);
  view.SetContent(
      QStringLiteral("```mermaid\nflowchart LR\n    C --> D\n```\n"),
      QString());
  ASSERT_TRUE(wait.Wait());

  auto* label = panel.findChild<QLabel*>(QStringLiteral("statusMermaid"));
  ASSERT_NE(label, nullptr);
  EXPECT_TRUE(label->text().contains(QStringLiteral("调用 1")))
      << label->text().toStdString();
  EXPECT_TRUE(label->text().contains(QStringLiteral("渲染 0")))
      << label->text().toStdString();
  EXPECT_TRUE(label->text().contains(QStringLiteral("磁盘命中 1")))
      << label->text().toStdString();
}

}  // namespace
