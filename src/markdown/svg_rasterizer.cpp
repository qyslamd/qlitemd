#include "svg_rasterizer.h"

#include <QPainter>
#include <QSvgRenderer>

#if defined(QLITEMD_SVG_BACKEND_RESVG)
#include "resvg.h"
#endif

namespace {

QByteArray Utf8(const QString& text) { return text.toUtf8(); }

#if defined(QLITEMD_SVG_BACKEND_RESVG)
struct ResvgOptionsCache {
  resvg_options* options = nullptr;
  QString family;

  ~ResvgOptionsCache() {
    if (options != nullptr) {
      resvg_options_destroy(options);
    }
  }
};

resvg_options* OptionsFor(const QString& cjk_family) {
  thread_local ResvgOptionsCache cache;
  if (cache.options != nullptr && cache.family == cjk_family) {
    return cache.options;
  }
  if (cache.options != nullptr) {
    resvg_options_destroy(cache.options);
    cache.options = nullptr;
  }
  cache.options = resvg_options_create();
  cache.family = cjk_family;
  resvg_options_load_system_fonts(cache.options);
  const QByteArray sans =
      Utf8(cjk_family.isEmpty() ? QStringLiteral("Arial") : cjk_family);
  resvg_options_set_sans_serif_family(cache.options, sans.constData());
  resvg_options_set_serif_family(cache.options, "Times New Roman");
  resvg_options_set_monospace_family(cache.options, "Consolas");
  return cache.options;
}
#endif

}  // namespace

bool SvgIntrinsicSize(const QByteArray& svg, const QString& cjk_family,
                      QSizeF* size, QString* error) {
  if (size == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("null size output");
    }
    return false;
  }
#if defined(QLITEMD_SVG_BACKEND_RESVG)
  resvg_render_tree* tree = nullptr;
  const int32_t result = resvg_parse_tree_from_data(
      svg.constData(), static_cast<uintptr_t>(svg.size()),
      OptionsFor(cjk_family), &tree);
  if (result != 0 || tree == nullptr) {
    if (tree != nullptr) {
      resvg_tree_destroy(tree);
    }
    if (error != nullptr) {
      *error = QStringLiteral("resvg parse error %1").arg(result);
    }
    return false;
  }
  const resvg_size parsed = resvg_get_image_size(tree);
  resvg_tree_destroy(tree);
  *size = QSizeF(parsed.width, parsed.height);
  return size->width() > 0.0 && size->height() > 0.0;
#else
  Q_UNUSED(cjk_family);
  QSvgRenderer renderer(svg);
  if (!renderer.isValid()) {
    if (error != nullptr) {
      *error = QStringLiteral("QSvgRenderer rejected the svg");
    }
    return false;
  }
  const QSize intrinsic = renderer.defaultSize();
  *size = QSizeF(intrinsic);
  return size->width() > 0.0 && size->height() > 0.0;
#endif
}

bool RasterizeSvg(const QByteArray& svg, const QSizeF& logical_size,
                  qreal scale, const QString& cjk_family, QImage* image,
                  QString* error) {
  if (image == nullptr || logical_size.width() <= 0.0 ||
      logical_size.height() <= 0.0 || scale <= 0.0) {
    if (error != nullptr) {
      *error = QStringLiteral("invalid raster input");
    }
    return false;
  }
  const int width = qMax(1, qRound(logical_size.width() * scale));
  const int height = qMax(1, qRound(logical_size.height() * scale));
#if defined(QLITEMD_SVG_BACKEND_RESVG)
  resvg_render_tree* tree = nullptr;
  const int32_t result = resvg_parse_tree_from_data(
      svg.constData(), static_cast<uintptr_t>(svg.size()),
      OptionsFor(cjk_family), &tree);
  if (result != 0 || tree == nullptr) {
    if (tree != nullptr) {
      resvg_tree_destroy(tree);
    }
    if (error != nullptr) {
      *error = QStringLiteral("resvg parse error %1").arg(result);
    }
    return false;
  }
  QImage rendered(width, height, QImage::Format_RGBA8888_Premultiplied);
  rendered.fill(0);
  resvg_transform transform = resvg_transform_identity();
  transform.a = static_cast<float>(scale);
  transform.d = static_cast<float>(scale);
  resvg_render(tree, transform, static_cast<uint32_t>(width),
               static_cast<uint32_t>(height),
               reinterpret_cast<char*>(rendered.bits()));
  resvg_tree_destroy(tree);
  *image = std::move(rendered);
  return true;
#else
  Q_UNUSED(cjk_family);
  QSvgRenderer renderer(svg);
  if (!renderer.isValid()) {
    if (error != nullptr) {
      *error = QStringLiteral("QSvgRenderer rejected the svg");
    }
    return false;
  }
  QImage rendered(width, height, QImage::Format_ARGB32_Premultiplied);
  rendered.fill(Qt::transparent);
  QPainter painter(&rendered);
  renderer.render(&painter, QRectF(0.0, 0.0, width, height));
  painter.end();
  *image = std::move(rendered);
  return true;
#endif
}
