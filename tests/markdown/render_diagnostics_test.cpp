#include "render_diagnostics.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

namespace {

QStringList g_messages;

void CaptureMessages(QtMsgType, const QMessageLogContext& context,
                     const QString& message) {
  g_messages << QString::fromUtf8(
                    context.category == nullptr ? "" : context.category) +
                    QLatin1Char('|') + message;
}

QString FirstDumpedFile(const QString& root) {
  QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
  return it.hasNext() ? it.next() : QString();
}

QStringList DumpedRelativePaths(const QString& root) {
  QStringList names;
  const QDir root_dir(root);
  QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    names << root_dir.relativeFilePath(it.next());
  }
  names.sort();
  return names;
}

// 只在 trace 打开时输出；类别名固定，方便宿主过滤
TEST(RenderDiagnosticsTest, TraceIsGatedByFlag) {
  g_messages.clear();
  QtMessageHandler old_handler = qInstallMessageHandler(CaptureMessages);
  MarkdownDiagnostics off;
  TraceRender(off, QStringLiteral("should not appear"));
  MarkdownDiagnostics on;
  on.trace = true;
  TraceRender(on, QStringLiteral("should appear"));
  qInstallMessageHandler(old_handler);

  ASSERT_EQ(g_messages.size(), 1);
  EXPECT_TRUE(g_messages.first().contains(QStringLiteral("qt.markdown.render")))
      << g_messages.first().toStdString();
  EXPECT_TRUE(g_messages.first().contains(QStringLiteral("should appear")))
      << g_messages.first().toStdString();
}

TEST(RenderDiagnosticsTest, DumpWritesNamedArtifact) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  MarkdownDiagnostics diagnostics;
  diagnostics.dump_directory = dir.path();
  DumpRenderArtifact(diagnostics, QStringLiteral("formula/a.svg"),
                     QByteArray("<svg/>"));

  const QString path = FirstDumpedFile(dir.path());
  ASSERT_FALSE(path.isEmpty());
  EXPECT_TRUE(path.endsWith(QStringLiteral("formula/a.svg")))
      << path.toStdString();
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), QByteArray("<svg/>"));
}

// 旧调试习惯：只设 MD_MERMAID_DUMP 也能落盘
TEST(RenderDiagnosticsTest, DumpFallsBackToEnvironment) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QByteArray saved_markdown = qgetenv("MD_MARKDOWN_DUMP");
  qunsetenv("MD_MARKDOWN_DUMP");
  qputenv("MD_MERMAID_DUMP", dir.path().toUtf8());
  MarkdownDiagnostics diagnostics;
  DumpRenderArtifact(diagnostics, QStringLiteral("mermaid/x.svg"),
                     QByteArray("x"));
  qunsetenv("MD_MERMAID_DUMP");
  if (!saved_markdown.isEmpty()) {
    qputenv("MD_MARKDOWN_DUMP", saved_markdown);
  }

  EXPECT_FALSE(FirstDumpedFile(dir.path()).isEmpty());
}

// 旧的 MD_MERMAID_DUMP 只覆盖 mermaid/：不能把文档正文等产物也带落盘
TEST(RenderDiagnosticsTest, MermaidDumpVariableOnlyCoversMermaid) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QByteArray saved_markdown = qgetenv("MD_MARKDOWN_DUMP");
  qunsetenv("MD_MARKDOWN_DUMP");
  qputenv("MD_MERMAID_DUMP", dir.path().toUtf8());
  MarkdownDiagnostics diagnostics;

  EXPECT_TRUE(DumpEnabled(diagnostics, QStringLiteral("mermaid/x.svg")));
  EXPECT_FALSE(DumpEnabled(diagnostics, QStringLiteral("apply/source.md")));
  DumpRenderArtifact(diagnostics, QStringLiteral("apply/1-source.md"),
                     QByteArray("doc"));
  DumpRenderArtifact(diagnostics, QStringLiteral("mermaid/x.svg"),
                     QByteArray("x"));

  qunsetenv("MD_MERMAID_DUMP");
  if (!saved_markdown.isEmpty()) {
    qputenv("MD_MARKDOWN_DUMP", saved_markdown);
  }

  const QStringList names = DumpedRelativePaths(dir.path());
  ASSERT_EQ(names.size(), 1) << names.join(QLatin1Char(',')).toStdString();
  EXPECT_TRUE(names.first().endsWith(QStringLiteral("mermaid/x.svg")))
      << names.first().toStdString();
}

// 新变量覆盖全部产物（含文档原文）；显式 dump_directory 优先级最高
TEST(RenderDiagnosticsTest, MarkdownDumpVariableCoversAllArtifacts) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QByteArray saved_mermaid = qgetenv("MD_MERMAID_DUMP");
  qputenv("MD_MARKDOWN_DUMP", dir.path().toUtf8());
  MarkdownDiagnostics diagnostics;

  EXPECT_TRUE(DumpEnabled(diagnostics, QStringLiteral("apply/source.md")));
  EXPECT_TRUE(DumpEnabled(diagnostics, QStringLiteral("mermaid/x.svg")));
  DumpRenderArtifact(diagnostics, QStringLiteral("apply/1-source.md"),
                     QByteArray("doc"));
  DumpRenderArtifact(diagnostics, QStringLiteral("mermaid/x.svg"),
                     QByteArray("x"));

  qunsetenv("MD_MARKDOWN_DUMP");
  if (!saved_mermaid.isEmpty()) {
    qputenv("MD_MERMAID_DUMP", saved_mermaid);
  }

  const QStringList names = DumpedRelativePaths(dir.path());
  ASSERT_EQ(names.size(), 2) << names.join(QLatin1Char(',')).toStdString();
  EXPECT_TRUE(names.at(0).endsWith(QStringLiteral("apply/1-source.md")))
      << names.at(0).toStdString();
  EXPECT_TRUE(names.at(1).endsWith(QStringLiteral("mermaid/x.svg")))
      << names.at(1).toStdString();
}

TEST(RenderDiagnosticsTest, ExplicitDirectoryOverridesEnvironment) {
  QTemporaryDir explicit_dir;
  QTemporaryDir env_dir;
  ASSERT_TRUE(explicit_dir.isValid());
  ASSERT_TRUE(env_dir.isValid());
  qputenv("MD_MARKDOWN_DUMP", env_dir.path().toUtf8());
  MarkdownDiagnostics diagnostics;
  diagnostics.dump_directory = explicit_dir.path();

  EXPECT_TRUE(DumpEnabled(diagnostics, QStringLiteral("apply/source.md")));
  DumpRenderArtifact(diagnostics, QStringLiteral("apply/1-source.md"),
                     QByteArray("doc"));

  qunsetenv("MD_MARKDOWN_DUMP");
  EXPECT_FALSE(FirstDumpedFile(explicit_dir.path()).isEmpty());
  EXPECT_TRUE(FirstDumpedFile(env_dir.path()).isEmpty());
}

}  // namespace
