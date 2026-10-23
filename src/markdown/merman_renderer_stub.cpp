#include "merman_renderer.h"

// Non-Windows builds keep the legacy QtSvg/QuickJS path; this stub keeps the
// MermaidEngineState layout linkable without the Windows merman DLL.
struct MermanEngine::Impl {};

MermanEngine::MermanEngine() : impl_(std::make_unique<Impl>()) {}

MermanEngine::~MermanEngine() = default;

bool MermanEngine::EnsureLoaded() { return false; }

MarkdownEngineStatus MermanEngine::status() const { return {}; }

RenderedImage MermanEngine::Render(
    const QString& /*code*/, MarkdownTheme /*theme*/,
    const MarkdownDiagnostics& /*diagnostics*/,
    QVector<MarkdownRenderArtifact>* /*artifacts*/) {
  return {};
}

MarkdownEngineStatus MermanEngineStatus(const MermanEngine& engine) {
  return engine.status();
}

QString MermanVersion() { return QStringLiteral("merman-disabled"); }
