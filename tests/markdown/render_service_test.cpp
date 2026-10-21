#include "render_service.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QDateTime>
#include <QGuiApplication>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QTest>

#include "code_highlighter.h"
#include "formula_renderer.h"
#include "markdown_render_tasks.h"
#include "mermaid_renderer.h"

// 服务判据：任务真的在 worker 上跑、结果按 request_id 回 GUI、批次收尾
// allFinished 只发一次。要 QGuiApplication：公式 worker 里的 QSvgRenderer
// 依赖绘图后端，无头环境用 offscreen
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("render-service-%1")
          .arg(QDateTime::currentMSecsSinceEpoch()));
  QGuiApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

MarkdownRenderTask HighlightTask(const QString& code, const QString& language) {
  MarkdownRenderTask task;
  task.kind = MarkdownRenderKind::Highlight;
  task.cache_key = HighlightRenderKey(code, language);
  task.payload = code;
  task.language = language;
  return task;
}

// 每个 request 各自收尾：重叠批次不能只给最后一个发 allFinished
TEST(RenderServiceTest, PerRequestBatchesFinishIndependently) {
  MarkdownRenderService service;
  QSignalSpy all_finished(&service, &MarkdownRenderService::allFinished);

  service.Submit(
      QStringLiteral("test"), 1,
      {HighlightTask(QStringLiteral("int a;"), QStringLiteral("cpp"))});
  service.Submit(
      QStringLiteral("test"), 2,
      {HighlightTask(QStringLiteral("int b;"), QStringLiteral("cpp"))});

  QTRY_VERIFY_WITH_TIMEOUT(all_finished.count() == 2, 20000);
  QSet<quint64> ids;
  for (const QList<QVariant>& args : all_finished) {
    ids.insert(args.at(1).toULongLong());
  }
  EXPECT_EQ(ids, (QSet<quint64>{1, 2}));
}

TEST(RenderServiceTest, RunsHighlightAndFormulaOnWorkers) {
  MarkdownRenderService service;
  QSignalSpy finished(&service, &MarkdownRenderService::taskFinished);
  QSignalSpy all_finished(&service, &MarkdownRenderService::allFinished);

  MarkdownRenderTask formula;
  formula.kind = MarkdownRenderKind::Formula;
  formula.cache_key = FormulaRenderKey(QStringLiteral("x^2"), false, QColor());
  formula.payload = QStringLiteral("x^2");
  formula.display = false;

  QVector<MarkdownRenderTask> tasks;
  tasks.append(
      HighlightTask(QStringLiteral("int a = 1;"), QStringLiteral("cpp")));
  tasks.append(formula);
  service.Submit(QStringLiteral("test"), 42, tasks);

  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 2, 20000);
  QTRY_VERIFY_WITH_TIMEOUT(all_finished.count() == 1, 20000);

  bool highlight_ok = false;
  bool formula_ok = false;
  for (const QList<QVariant>& args : finished) {
    const MarkdownRenderResult result =
        qvariant_cast<MarkdownRenderResult>(args.at(1));
    EXPECT_EQ(result.task.request_id, 42u);
    if (result.task.kind == MarkdownRenderKind::Highlight) {
      highlight_ok =
          result.ok && result.rendered.contains(QStringLiteral("<span"));
    } else if (result.task.kind == MarkdownRenderKind::Formula) {
      formula_ok =
          result.ok && result.rendered.startsWith(QStringLiteral("<img"));
    }
  }
  EXPECT_TRUE(highlight_ok);
  EXPECT_TRUE(formula_ok);
  EXPECT_EQ(all_finished.at(0).at(1).toULongLong(), 42u);
}

// 取消后不再向宿主发该代结果，服务仍能接下一代
TEST(RenderServiceTest, CancelSilencesBatchAndAllowsNext) {
  MarkdownRenderService service;
  QSignalSpy finished(&service, &MarkdownRenderService::taskFinished);
  QSignalSpy all_finished(&service, &MarkdownRenderService::allFinished);

  MarkdownRenderTask mermaid;
  mermaid.kind = MarkdownRenderKind::Mermaid;
  mermaid.payload = QStringLiteral("flowchart TD\n");
  for (int i = 0; i < 800; ++i) {
    mermaid.payload +=
        QStringLiteral("  n%1[节点] --> n%2[节点]\n").arg(i).arg((i + 1) % 800);
  }
  mermaid.cache_key = MermaidRenderKey(mermaid.payload, MarkdownTheme::Light);
  mermaid.theme = MarkdownTheme::Light;
  service.Submit(QStringLiteral("test"), 11, {mermaid});
  QTest::qWait(400);
  service.Cancel(QStringLiteral("test"), 11);
  QTest::qWait(300);
  EXPECT_EQ(finished.count(), 0);
  EXPECT_EQ(all_finished.count(), 0);

  MarkdownRenderTask small;
  small.kind = MarkdownRenderKind::Mermaid;
  small.payload = QStringLiteral("flowchart LR\n  A --> B\n");
  small.cache_key = MermaidRenderKey(small.payload, MarkdownTheme::Light);
  small.theme = MarkdownTheme::Light;
  service.Submit(QStringLiteral("test"), 12, {small});
  QTRY_VERIFY_WITH_TIMEOUT(all_finished.count() == 1, 30000);
  ASSERT_EQ(finished.count(), 1);
  const MarkdownRenderResult result =
      qvariant_cast<MarkdownRenderResult>(finished.at(0).at(1));
  EXPECT_EQ(result.task.request_id, 12u);
  EXPECT_TRUE(result.rendered.startsWith(QStringLiteral("<img")))
      << result.rendered.toStdString();
}

// mermaid worker：引擎状态确实在 worker 线程里创建、字体路径可用
TEST(RenderServiceTest, RunsMermaidOnWorker) {
  MarkdownRenderService service;
  QSignalSpy finished(&service, &MarkdownRenderService::taskFinished);
  MarkdownRenderTask task;
  task.kind = MarkdownRenderKind::Mermaid;
  task.cache_key = MermaidRenderKey(QStringLiteral("flowchart LR\n  A --> B\n"),
                                    MarkdownTheme::Light);
  task.payload = QStringLiteral("flowchart LR\n  A --> B\n");
  task.theme = MarkdownTheme::Light;
  service.Submit(QStringLiteral("test"), 7, {task});

  QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 30000);
  const MarkdownRenderResult result =
      qvariant_cast<MarkdownRenderResult>(finished.at(0).at(1));
  EXPECT_TRUE(result.rendered.startsWith(QStringLiteral("<img")))
      << result.rendered.toStdString();
}

}  // namespace
