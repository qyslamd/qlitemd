#include "render_diagnostics.h"

#include <mutex>

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

Q_LOGGING_CATEGORY(lcMarkdownRender, "qt.markdown.render")

void TraceRender(const MarkdownDiagnostics& diagnostics,
                 const QString& message) {
  if (!diagnostics.trace) {
    return;
  }
  // 类别是进程级的：只启用一次，不随某个视图关闭而关掉（另一个视图可能
  // 还开着）。是否输出由各调用点的 trace 标志决定
  static std::once_flag category_once;
  std::call_once(category_once, [] {
    // Q_LOGGING_CATEGORY 的访问器返回 const 引用，但底下是函数级静态对象；
    // 打开调试级别是 setEnabled 的正当用法
    const_cast<QLoggingCategory&>(lcMarkdownRender())
        .setEnabled(QtDebugMsg, true);
  });
  qCDebug(lcMarkdownRender) << message;
}

namespace {

QString EnsureRunDir(const QString& base_dir) {
  // 基目录 → 本次进程的产物目录。同一目录下多次 ApplyDocument 复用同一个
  // run 目录，文件名里带序号避免覆盖
  static QHash<QString, QString> run_dirs;
  static int sequence = 0;
  static std::mutex mutex;
  const std::lock_guard<std::mutex> lock(mutex);
  const auto it = run_dirs.constFind(base_dir);
  if (it != run_dirs.constEnd()) {
    return *it;
  }
  const QString name =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")) +
      QStringLiteral("-%1").arg(++sequence, 2, 10, QLatin1Char('0'));
  const QString dir = QDir(base_dir).filePath(name);
  if (!QDir().mkpath(dir)) {
    qWarning() << "markdown: cannot create dump directory" << dir;
    return {};
  }
  run_dirs.insert(base_dir, dir);
  return dir;
}

}  // namespace

bool DumpEnabled(const MarkdownDiagnostics& diagnostics, const QString& name) {
  if (!diagnostics.dump_directory.isEmpty()) {
    return true;
  }
  if (!qEnvironmentVariable("MD_MARKDOWN_DUMP").isEmpty()) {
    return true;
  }
  return name.startsWith(QLatin1String("mermaid/")) &&
         !qEnvironmentVariable("MD_MERMAID_DUMP").isEmpty();
}

void DumpRenderArtifact(const MarkdownDiagnostics& diagnostics,
                        const QString& name, const QByteArray& data) {
  QString base_dir = diagnostics.dump_directory;
  if (base_dir.isEmpty()) {
    // 新变量控制全部产物（含文档原文）；旧的 MD_MERMAID_DUMP 只保留
    // mermaid 中间产物的语义，不能让设了旧变量的人意外落盘文档正文
    base_dir = qEnvironmentVariable("MD_MARKDOWN_DUMP");
  }
  if (base_dir.isEmpty() && name.startsWith(QLatin1String("mermaid/"))) {
    base_dir = qEnvironmentVariable("MD_MERMAID_DUMP");
  }
  if (base_dir.isEmpty()) {
    return;
  }
  const QString run_dir = EnsureRunDir(base_dir);
  if (run_dir.isEmpty()) {
    return;
  }
  const QString path = run_dir + QLatin1Char('/') + name;
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    qWarning() << "markdown: cannot create artifact directory for" << path;
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "markdown: cannot write artifact" << path;
    return;
  }
  file.write(data);
}
