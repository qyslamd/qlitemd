#ifndef FILE_MARKDOWN_SVG_RASTERIZER_H_
#define FILE_MARKDOWN_SVG_RASTERIZER_H_

#include <QByteArray>
#include <QImage>
#include <QSizeF>
#include <QString>

// Rasterizes an SVG string into a premultiplied QImage. The backend is fixed
// at build time by QLITEMD_SVG_BACKEND (resvg or qtsvg). logical_size is the
// CSS pixel size used by the surrounding HTML; scale is the supersampling
// factor chosen by the caller.
bool RasterizeSvg(const QByteArray& svg, const QSizeF& logical_size,
                  qreal scale, const QString& cjk_family, QImage* image,
                  QString* error);

// Reads the SVG's intrinsic size without rasterizing it.
bool SvgIntrinsicSize(const QByteArray& svg, const QString& cjk_family,
                      QSizeF* size, QString* error);

#endif  // FILE_MARKDOWN_SVG_RASTERIZER_H_
