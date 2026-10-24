#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "markdown_engine_state.h"
#include "markdown_types.h"

// mermaid 渲染核心（RenderMermaidOnEngine）的端到端判据：qrc 载荷/merman
// 原生库 → SVG → 栅格化成 PNG 产物。中间任何一环坏了都只能从"产物本身"看出
// 来，所以在无头环境把产物断言掉。缓存/计数/token 拼装归视图层，由
// markdown_view_test / render_cache_test 覆盖。
//
// 要 QGuiApplication 而不是 QCoreApplication：QSvgRenderer 走 QPainter，且
// 度量要查 Qt 字体库。
//
// **offscreen 平台没有字体库**（实测 QFontDatabase::families() 返回 0 个族）：
// 于是文字会被画成 notdef 方框、选族名也会落空。所以这里只断言"图出来了、
// 产物合法"，**不拿文字像素做判据**——像素验收需要真实界面环境。
//
// 首次调用要 eval 3.5MB 的 bundle（quickjs 后端，秒级），用例超时在 CMake
// 里放宽到 120 秒
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  // 测试模式 + 唯一应用名：即便某个用例漏关持久缓存，也不会读写用户真实
  // 的 CacheLocation，更不会跨运行串数据
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("mermaid-%1").arg(QDateTime::currentMSecsSinceEpoch()));
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

TEST(MermaidRendererTest, RendersFlowchartToImage) {
  MermaidEngineState engine;
  const RenderedImage image = RenderMermaidOnEngine(
      engine, QStringLiteral("flowchart TD\n    A[开始] --> B[结束]\n"),
      MarkdownTheme::Light, MarkdownDiagnostics{});
  EXPECT_TRUE(ValidImage(image));
}

// 另一条代码路径：sequence 的消息线是 <line> 而不是 <path>，箭头能不能出来
// 全看后处理有没有把它一起覆盖
TEST(MermaidRendererTest, RendersSequenceDiagramToImage) {
  MermaidEngineState engine;
  const RenderedImage image = RenderMermaidOnEngine(
      engine,
      QStringLiteral("sequenceDiagram\n    Alice->>Bob: hi\n    Bob-->>Alice: "
                     "ok\n"),
      MarkdownTheme::Light, MarkdownDiagnostics{});
  EXPECT_TRUE(ValidImage(image));
}

// 类图依赖 resvg-safe 文本回退与宿主度量；标题、构造型和关系标签都要能出图。
TEST(MermaidRendererTest, RendersClassDiagramToImage) {
  MermaidEngineState engine;
  const RenderedImage image =
      RenderMermaidOnEngine(engine,
                            QStringLiteral("classDiagram\n"
                                           "  class Base {\n"
                                           "    +Run() void\n"
                                           "  }\n"
                                           "  class Child {\n"
                                           "    -int count\n"
                                           "  }\n"
                                           "  Base <|-- Child\n"
                                           "  Base : <<widget>>\n"),
                            MarkdownTheme::Light, MarkdownDiagnostics{});
  EXPECT_TRUE(ValidImage(image));
}

// 渲染是确定的：同一引擎同输入两次，PNG 逐字节相同（缓存正确性的前提）。
// 换一段源码要比出不同**不能只改标签文字**——offscreen 下文字是方框、宽度也
// 一样，两张图会逐字节相同；改节点数量才是与字体无关的差异
TEST(MermaidRendererTest, RendersDeterministicallyAndSourceMatters) {
  MermaidEngineState engine;
  const RenderedImage first = RenderMermaidOnEngine(
      engine, QStringLiteral("flowchart LR\n    X --> Y\n"),
      MarkdownTheme::Light, MarkdownDiagnostics{});
  const RenderedImage second = RenderMermaidOnEngine(
      engine, QStringLiteral("flowchart LR\n    X --> Y\n"),
      MarkdownTheme::Light, MarkdownDiagnostics{});
  ASSERT_TRUE(ValidImage(first));
  EXPECT_EQ(first.png_bytes, second.png_bytes);

  const RenderedImage bigger = RenderMermaidOnEngine(
      engine, QStringLiteral("flowchart LR\n    X --> Y --> Z\n"),
      MarkdownTheme::Light, MarkdownDiagnostics{});
  EXPECT_TRUE(ValidImage(bigger));
  EXPECT_NE(first.png_bytes, bigger.png_bytes);
}

// 回归点：mermaid.render 走 .catch（图示类型都认不出来）时也必须置 C 侧
// settled 旗子。漏了的话泵循环要跑满二十万次 job 或 30 秒上限才放弃，一张
// 坏图能把界面卡半分钟——比空结果本身严重得多
TEST(MermaidRendererTest, RenderErrorSettlesPromptly) {
  MermaidEngineState engine;
  QElapsedTimer clock;
  clock.start();
  const RenderedImage image = RenderMermaidOnEngine(
      engine, QStringLiteral("notadiagram\n    A --> B\n"),
      MarkdownTheme::Light, MarkdownDiagnostics{});
  const qint64 elapsed = clock.elapsed();
  EXPECT_TRUE(image.isNull()) << "这类输入应当被 mermaid 拒绝";
  EXPECT_LT(elapsed, 5000) << "出错路径没有及时落定，泵循环跑满了预算";
}

// 主题进产物：同一张图 Light/Dark 出不同的字节（主题指令 per-diagram 注入）
TEST(MermaidRendererTest, ThemeChangesOutput) {
  MermaidEngineState engine;
  const RenderedImage light = RenderMermaidOnEngine(
      engine, QStringLiteral("flowchart LR\n    X --> Y\n"),
      MarkdownTheme::Light, MarkdownDiagnostics{});
  const RenderedImage dark = RenderMermaidOnEngine(
      engine, QStringLiteral("flowchart LR\n    X --> Y\n"),
      MarkdownTheme::Dark, MarkdownDiagnostics{});
  ASSERT_TRUE(ValidImage(light));
  ASSERT_TRUE(ValidImage(dark));
  EXPECT_NE(light.png_bytes, dark.png_bytes);
}

// 主题走 per-diagram 指令：同一引擎交替主题重渲，结果只由各自主题决定，
// 与渲染顺序无关（回归点：曾用引擎级 initialize 切主题导致污染）
TEST(MermaidRendererTest, ThemesDoNotLeakAcrossRerenders) {
  MermaidEngineState engine;
  const QString source = QStringLiteral("flowchart LR\n    X --> Y\n");
  const RenderedImage light = RenderMermaidOnEngine(
      engine, source, MarkdownTheme::Light, MarkdownDiagnostics{});
  const RenderedImage dark = RenderMermaidOnEngine(
      engine, source, MarkdownTheme::Dark, MarkdownDiagnostics{});
  ASSERT_TRUE(ValidImage(light));
  ASSERT_TRUE(ValidImage(dark));

  const RenderedImage light_again = RenderMermaidOnEngine(
      engine, source, MarkdownTheme::Light, MarkdownDiagnostics{});
  const RenderedImage dark_again = RenderMermaidOnEngine(
      engine, source, MarkdownTheme::Dark, MarkdownDiagnostics{});
  EXPECT_EQ(light.png_bytes, light_again.png_bytes);
  EXPECT_EQ(dark.png_bytes, dark_again.png_bytes);
}

// YAML frontmatter 必须留在文档最开头：主题指令要插到它后面，否则 mermaid
// 认不出 frontmatter，整张图会渲染失败
TEST(MermaidRendererTest, ThemeDirectiveKeepsFrontmatterFirst) {
  MermaidEngineState engine;
  const RenderedImage light = RenderMermaidOnEngine(
      engine,
      QStringLiteral("---\ntitle: demo\n---\nflowchart LR\n    X --> Y\n"),
      MarkdownTheme::Light, MarkdownDiagnostics{});
  EXPECT_TRUE(ValidImage(light)) << "带 frontmatter 的图渲染失败";
}

// 畸形输入不抛、不崩。约定是"空产物 = 渲不出来，调用方退回代码块"，但别指望
// 畸形一定返回空：mermaid 对某些写法相当宽容（会画出一张降级图）。所以这里只钉
// 不变量：要么空，要么是合法产物
TEST(MermaidRendererTest, MalformedCodeNeverCrashes) {
  MermaidEngineState engine;
  const QStringList bad = {
      QStringLiteral("flowchart TD\n    A[\n"),
      QStringLiteral("sequenceDiagram\n    ???\n"),
      QStringLiteral("notadiagram\n    A --> B\n"),
      QString(),
  };
  for (const QString& code : bad) {
    const RenderedImage image = RenderMermaidOnEngine(
        engine, code, MarkdownTheme::Light, MarkdownDiagnostics{});
    EXPECT_TRUE(image.isNull() || ValidImage(image)) << code.toStdString();
  }
}

}  // namespace
