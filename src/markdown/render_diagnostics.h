#ifndef FILE_MARKDOWN_RENDER_DIAGNOSTICS_H_
#define FILE_MARKDOWN_RENDER_DIAGNOSTICS_H_

#include <QByteArray>
#include <QLoggingCategory>
#include <QString>

#include "markdown_types.h"

Q_DECLARE_LOGGING_CATEGORY(lcMarkdownRender)

// 诊断跟踪：diagnostics.trace 为真时输出到 qt.markdown.render。类别在首次
// 使用时启用一次（进程级），之后每个调用点由自己的 trace 标志门控，
// 多视图一开一关互不影响
void TraceRender(const MarkdownDiagnostics& diagnostics,
                 const QString& message);

// 产物转储：diagnostics.dump_directory 为空时回落到环境变量——
// MD_MARKDOWN_DUMP 控制全部产物，MD_MERMAID_DUMP 只控制 mermaid/ 子目录
// （保持旧语义：设了它不会把文档原文落盘）。每个基目录第一次写时创建
// <dir>/<yyyyMMdd-hhmmss>-<seq>/，name 可以带子目录（apply/、formula/、
// mermaid/）。目录映射内部串行化，可在 worker 调用；name 由调用方保证唯一
void DumpRenderArtifact(const MarkdownDiagnostics& diagnostics,
                        const QString& name, const QByteArray& data);

// 给定产物名判断会不会落盘。调用方用它避免"转储关闭时仍构造 toUtf8()/
// QByteArray"的浪费；name 的目录前缀参与 MD_MERMAID_DUMP 的判定
bool DumpEnabled(const MarkdownDiagnostics& diagnostics, const QString& name);

#endif  // FILE_MARKDOWN_RENDER_DIAGNOSTICS_H_
