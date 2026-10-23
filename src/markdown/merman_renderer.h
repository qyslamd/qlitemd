#ifndef FILE_MARKDOWN_MERMAN_RENDERER_H_
#define FILE_MARKDOWN_MERMAN_RENDERER_H_

#include <memory>

#include <QString>

#include "markdown_types.h"

// Native Mermaid backend used when QLITEMD_MERMAID_BACKEND=merman.
// One instance per worker/thread; the C ABI engine is not safe to share
// between concurrent operations when a text-measure callback is configured.
class MermanEngine {
 public:
  MermanEngine();
  ~MermanEngine();
  MermanEngine(const MermanEngine&) = delete;
  MermanEngine& operator=(const MermanEngine&) = delete;

  bool EnsureLoaded();
  MarkdownEngineStatus status() const;
  // isNull() = 失败。产物带位图与 PNG，GUI 直接 QPixmap::fromImage
  RenderedImage Render(const QString& code, MarkdownTheme theme,
                       const MarkdownDiagnostics& diagnostics,
                       QVector<MarkdownRenderArtifact>* artifacts = nullptr);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

MarkdownEngineStatus MermanEngineStatus(const MermanEngine& engine);
QString MermanVersion();

#endif  // FILE_MARKDOWN_MERMAN_RENDERER_H_
