#include <thread>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QSizeF>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "markdown_engine_state.h"
#include "markdown_theme.h"
#include "markdown_types.h"
#include "svg_rasterizer.h"

// 公式渲染核心（RenderFormulaOnEngine）的端到端判据：加载 qrc 里的 MathJax
// 载荷 → QuickJS 里 tex→svg → 栅格化成 PNG 产物。整条链路要么全通要么全不通，
// 所以在无头环境把产物本身断言掉。缓存/计数/token 拼装归视图层，由
// markdown_view_test / render_cache_test 覆盖。
//
// 要 QGuiApplication 而不是 QCoreApplication：QSvgRenderer 走 QPainter，
// 没有 GUI application 建不出绘图后端。平台用 offscreen，CI/无显示器也能跑。
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  // 测试模式 + 唯一应用名：即便某个用例漏关持久缓存，也不会读写用户真实
  // 的 CacheLocation，更不会跨运行串数据
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("formula-%1").arg(QDateTime::currentMSecsSinceEpoch()));
  QGuiApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
       QStringLiteral("/markdown-render"))
      .removeRecursively();
  return result;
}

namespace {

const QByteArray kPngMagic = QByteArrayLiteral("\x89PNG\r\n\x1a\n");

// 产物不变量：非空、PNG 头、摆放尺寸合法且不超过位图（2x 超采样下位图
// 只会更大）
testing::AssertionResult ValidImage(const RenderedImage& image) {
  if (image.isNull()) {
    return testing::AssertionFailure() << "产物为空";
  }
  if (!image.png_bytes.startsWith(kPngMagic)) {
    return testing::AssertionFailure() << "PNG 头不符";
  }
  if (image.layout_width < 1 || image.layout_height < 1) {
    return testing::AssertionFailure() << "摆放尺寸非法";
  }
  if (image.image.width() < image.layout_width ||
      image.image.height() < image.layout_height) {
    return testing::AssertionFailure()
           << "位图 " << image.image.width() << "x" << image.image.height()
           << " 小于摆放 " << image.layout_width << "x" << image.layout_height;
  }
  return testing::AssertionSuccess();
}

// SVG 的 width/height 可能带 pt/ex 单位，resvg 的 intrinsic size 与
// viewBox 不一致；必须按目标位图/intrinsic 比例缩放，否则右侧内容会
// 被裁掉。这里用右边缘的矩形钉住这个回归。
TEST(SvgRasterizerTest, PtUnitsFillTargetWithoutClipping) {
  const QByteArray svg =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 20\" "
      "width=\"100pt\" height=\"20pt\">"
      "<rect x=\"90\" y=\"0\" width=\"10\" height=\"20\" "
      "fill=\"#000000\"/></svg>";
  QImage image;
  QString error;
  ASSERT_TRUE(
      RasterizeSvg(svg, QSizeF(100, 20), 2.0, QString(), &image, &error))
      << error.toStdString();
  ASSERT_EQ(image.size(), QSize(200, 40));

  bool right_ink = false;
  for (int y = 0; y < image.height() && !right_ink; ++y) {
    for (int x = 180; x < image.width(); ++x) {
      if (image.pixelColor(x, y).alpha() > 0) {
        right_ink = true;
        break;
      }
    }
  }
  EXPECT_TRUE(right_ink) << "pt 单位的 SVG 右侧被裁掉了";
}

TEST(FormulaRendererTest, RendersInlineFormulaToImage) {
  FormulaEngineState engine;
  const RenderedImage image =
      RenderFormulaOnEngine(engine, QStringLiteral("E = mc^2"), false, QColor(),
                            MarkdownDiagnostics{});
  EXPECT_TRUE(ValidImage(image));
}

// 显式 EngineState 必须能在非主线程独立加载并渲染
TEST(FormulaRendererTest, ExplicitEngineRendersOnWorkerThread) {
  RenderedImage image;
  std::thread worker([&image] {
    FormulaEngineState engine;
    const MarkdownDiagnostics diagnostics;
    image = RenderFormulaOnEngine(engine, QStringLiteral("x^2"), false,
                                  QColor(), diagnostics);
  });
  worker.join();
  EXPECT_TRUE(ValidImage(image));
}

// 分式是"必须走图"的理由：KaTeX 的 HTML 方案在 qlitehtml 下排不动它，
// 而 SVG 路线不挑内容——上下标、根号、求和都该出得来
TEST(FormulaRendererTest, HandlesStructuredFormulas) {
  FormulaEngineState engine;
  const QStringList texes = {
      QStringLiteral("\\sum_{i=1}^{n} x_i^2"),
      QStringLiteral("\\int_{-\\infty}^{\\infty} e^{-x^2}\\,dx = \\sqrt{\\pi}"),
      QStringLiteral(
          "\\nabla \\cdot \\mathbf{E} = \\frac{\\rho}{\\varepsilon_0}"),
  };
  for (const QString& tex : texes) {
    const RenderedImage image = RenderFormulaOnEngine(
        engine, tex, true, QColor(), MarkdownDiagnostics{});
    EXPECT_TRUE(ValidImage(image)) << tex.toStdString();
  }
}

// 渲染是确定的：同一引擎同输入两次，PNG 逐字节相同（缓存正确性的前提）。
// 前景色参与渲染：换色必须产出不同的字节
TEST(FormulaRendererTest, RendersDeterministicallyAndColorMatters) {
  FormulaEngineState engine;
  const QColor dark = ThemeFormulaColor(MarkdownTheme::Dark);
  const RenderedImage first =
      RenderFormulaOnEngine(engine, QStringLiteral("a^2 + b^2 = c^2"), false,
                            QColor(), MarkdownDiagnostics{});
  const RenderedImage second =
      RenderFormulaOnEngine(engine, QStringLiteral("a^2 + b^2 = c^2"), false,
                            QColor(), MarkdownDiagnostics{});
  ASSERT_TRUE(ValidImage(first));
  EXPECT_EQ(first.png_bytes, second.png_bytes);

  const RenderedImage tinted =
      RenderFormulaOnEngine(engine, QStringLiteral("a^2 + b^2 = c^2"), false,
                            dark, MarkdownDiagnostics{});
  EXPECT_TRUE(ValidImage(tinted));
  EXPECT_NE(first.png_bytes, tinted.png_bytes);
}

// display 只影响摆放（类名在 GUI 侧拼装），核心两种模式都要出图
TEST(FormulaRendererTest, DisplayModeRendersToo) {
  FormulaEngineState engine;
  const RenderedImage display =
      RenderFormulaOnEngine(engine, QStringLiteral("\\frac{a}{b}"), true,
                            QColor(), MarkdownDiagnostics{});
  EXPECT_TRUE(ValidImage(display));
}

// 畸形输入不抛、不崩。别指望畸形一定返回空：MathJax 相当宽容（`\frac{` 会
// 渲成缺参数的分数、`$$$` 直接当字面量），真正返回空的是载荷缺失、JS 异常、
// SVG 无效这类内部失败。所以只钉不变量：要么空，要么是合法产物
TEST(FormulaRendererTest, MalformedTexNeverCrashes) {
  FormulaEngineState engine;
  const QStringList bad = {QStringLiteral("\\frac{"), QStringLiteral("$$$"),
                           QStringLiteral("\\begin{matrix}"),
                           QStringLiteral("{}{}{")};
  for (const QString& tex : bad) {
    const RenderedImage image = RenderFormulaOnEngine(
        engine, tex, false, QColor(), MarkdownDiagnostics{});
    EXPECT_TRUE(image.isNull() || ValidImage(image)) << tex.toStdString();
  }
}

}  // namespace
