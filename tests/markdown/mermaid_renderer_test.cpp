#include "mermaid_renderer.h"

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

#include "markdown_render_context.h"
#include "markdown_types.h"

// 端到端判据：qrc 载荷 → QuickJS 里跑 mermaid → SVG 后处理 → QSvgRenderer 画成
// PNG → 内联成 <img>。中间任何一环坏了都只能从"产物本身"看出来，所以在无头环境
// 把产物断言掉。
//
// 要 QGuiApplication 而不是 QCoreApplication：QSvgRenderer 走 QPainter，且度量
// 要查 Qt 字体库。
//
// **offscreen 平台没有字体库**（实测 QFontDatabase::families() 返回 0 个族）：
// 于是文字会被画成 notdef 方框、选族名也会落空。所以这里只断言"图出来了、是
// well-formed 的 <img>"，**不拿文字像素做判据**——像素验收需要真实界面
// 环境，那里的字体库是全的。
//
// 首次调用要 eval 3.5MB 的 bundle（秒级），用例超时在 CMake 里放宽到 120 秒
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

// 自建上下文默认开启持久缓存；断言"每个上下文各自渲染"的用例要关掉，
// 否则第二个上下文会直接读盘（磁盘缓存本来就是跨上下文共享的）
void DisablePersistentCache(MarkdownRenderContext& context) {
  // SetOptions 是整体替换：先取当前值，只改目标字段，别把调用方已经设好
  // 的目录/诊断/容量清掉
  MarkdownRenderContextOptions options = context.options();
  options.persistent_cache_enabled = false;
  context.SetOptions(options);
}

TEST(MermaidRendererTest, RendersFlowchartToInlineImage) {
  ASSERT_TRUE(MermaidRendererReady()) << "qrc 里的三份载荷没加载起来";
  const QString tag = MermaidToImgTag(
      QStringLiteral("flowchart TD\n    A[开始] --> B[结束]\n"));
  ASSERT_FALSE(tag.isEmpty()) << "渲染失败（会退回代码块显示）";
  EXPECT_TRUE(tag.startsWith(QStringLiteral("<img"))) << tag.toStdString();
  EXPECT_TRUE(tag.contains(QStringLiteral("class=\"mermaid\"")))
      << tag.toStdString();
  EXPECT_TRUE(tag.contains(QStringLiteral("data:image/png;base64,")))
      << tag.toStdString();
}

// 另一条代码路径：sequence 的消息线是 <line> 而不是 <path>，箭头能不能出来
// 全看后处理有没有把它一起覆盖
TEST(MermaidRendererTest, RendersSequenceDiagramToInlineImage) {
  const QString tag = MermaidToImgTag(QStringLiteral(
      "sequenceDiagram\n    Alice->>Bob: hi\n    Bob-->>Alice: ok\n"));
  ASSERT_FALSE(tag.isEmpty());
  EXPECT_TRUE(tag.contains(QStringLiteral("data:image/png;base64,")));
}

// 类图依赖 merman 的 resvg-safe 文本回退与宿主度量；标题、构造型和
// 关系标签都要能出图。
TEST(MermaidRendererTest, RendersClassDiagramToInlineImage) {
  const QString tag =
      MermaidToImgTag(QStringLiteral("classDiagram\n"
                                     "  class Base {\n"
                                     "    +Run() void\n"
                                     "  }\n"
                                     "  class Child {\n"
                                     "    -int count\n"
                                     "  }\n"
                                     "  Base <|-- Child\n"
                                     "  Base : <<widget>>\n"));
  ASSERT_FALSE(tag.isEmpty());
  EXPECT_TRUE(tag.contains(QStringLiteral("data:image/png;base64,")));
}

// 结果按源码缓存：同一段源码再来一次应逐字相同。
// 换一段源码要比出不同**不能只改标签文字**——offscreen 下文字是方框、宽度也
// 一样，两张图会逐字节相同；改节点数量才是与字体无关的差异
TEST(MermaidRendererTest, CachesBySource) {
  const QString source = QStringLiteral("flowchart LR\n    X --> Y\n");
  const QString first = MermaidToImgTag(source);
  const QString second = MermaidToImgTag(source);
  ASSERT_FALSE(first.isEmpty());
  EXPECT_EQ(first, second);
  EXPECT_NE(first, MermaidToImgTag(
                       QStringLiteral("flowchart LR\n    X --> Y --> Z\n")));
}

// 回归点：mermaid.render 走 .catch（图示类型都认不出来）时也必须置 C 侧
// settled 旗子。漏了的话泵循环要跑满二十万次 job 或 30 秒上限才放弃，一张
// 坏图能把界面卡半分钟——比空结果本身严重得多
TEST(MermaidRendererTest, RenderErrorSettlesPromptly) {
  ASSERT_TRUE(MermaidRendererReady());
  QElapsedTimer clock;
  clock.start();
  const QString tag =
      MermaidToImgTag(QStringLiteral("notadiagram\n    A --> B\n"));
  const qint64 elapsed = clock.elapsed();
  EXPECT_TRUE(tag.isEmpty()) << "这类输入应当被 mermaid 拒绝";
  EXPECT_LT(elapsed, 5000) << "出错路径没有及时落定，泵循环跑满了预算";
}

// 上下文隔离：每个视图一份缓存与计数，清一个不影响另一个
TEST(MermaidRendererTest, ContextsKeepCachesSeparate) {
  MarkdownRenderContext first;
  MarkdownRenderContext second;
  DisablePersistentCache(first);
  DisablePersistentCache(second);
  const QString source = QStringLiteral("flowchart LR\n    A --> B\n");
  const QString tag = MermaidToImgTag(first, source, MarkdownTheme::Light);
  ASSERT_FALSE(tag.isEmpty());
  EXPECT_EQ(first.status().mermaid_context.renders, 1U);
  EXPECT_EQ(second.status().mermaid_context.renders, 0U);

  EXPECT_EQ(MermaidToImgTag(second, source, MarkdownTheme::Light), tag);
  EXPECT_EQ(second.status().mermaid_context.renders, 1U);

  EXPECT_EQ(MermaidToImgTag(first, source, MarkdownTheme::Light), tag);
  EXPECT_EQ(first.status().mermaid_context.memory_cache_hits, 1U);

  first.ClearMemoryCaches();
  EXPECT_EQ(MermaidToImgTag(first, source, MarkdownTheme::Light), tag);
  EXPECT_EQ(first.status().mermaid_context.renders, 2U);

  EXPECT_EQ(MermaidToImgTag(second, source, MarkdownTheme::Light), tag);
  EXPECT_EQ(second.status().mermaid_context.memory_cache_hits, 1U);
}

// 持久缓存跨上下文复用：主题与字体指纹都在 key 里，第二个上下文直接读盘
TEST(MermaidRendererTest, DiskCacheSurvivesAcrossContexts) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  MarkdownRenderContextOptions options;
  options.cache_directory = dir.path();
  const QString source = QStringLiteral("flowchart LR\n    P --> Q\n");

  MarkdownRenderContext first;
  first.SetOptions(options);
  const QString tag = MermaidToImgTag(first, source, MarkdownTheme::Dark);
  ASSERT_FALSE(tag.isEmpty());
  EXPECT_EQ(first.status().mermaid_context.renders, 1U);
  EXPECT_EQ(first.status().mermaid_context.disk_cache_misses, 1U);

  MarkdownRenderContext second;
  second.SetOptions(options);
  EXPECT_EQ(MermaidToImgTag(second, source, MarkdownTheme::Dark), tag);
  EXPECT_EQ(second.status().mermaid_context.renders, 0U);
  EXPECT_EQ(second.status().mermaid_context.disk_cache_hits, 1U);
}

// 畸形输入不抛、不崩。约定是"空串 = 渲不出来，调用方退回代码块"，但别指望畸形
// 一定返回空：mermaid 对某些写法相当宽容（会画出一张降级图）。所以这里只钉
// 不变量：要么空，要么是个 well-formed 的 img
TEST(MermaidRendererTest, MalformedCodeNeverCrashes) {
  const QStringList bad = {
      QStringLiteral("flowchart TD\n    A[\n"),
      QStringLiteral("sequenceDiagram\n    ???\n"),
      QStringLiteral("notadiagram\n    A --> B\n"),
      QString(),
  };
  for (const QString& code : bad) {
    const QString tag = MermaidToImgTag(code);
    const bool well_formed =
        tag.startsWith(QStringLiteral("<img")) &&
        tag.contains(QStringLiteral("data:image/png;base64,"));
    EXPECT_TRUE(tag.isEmpty() || well_formed) << code.toStdString();
  }
}

// 主题进缓存 key：同一张图在 Light/Dark 下各占一条
TEST(MermaidRendererTest, ThemeIsPartOfCacheKey) {
  MarkdownRenderContext context;
  DisablePersistentCache(context);
  const QString source = QStringLiteral("flowchart LR\n    X --> Y\n");
  const QString light = MermaidToImgTag(context, source, MarkdownTheme::Light);
  const QString dark = MermaidToImgTag(context, source, MarkdownTheme::Dark);
  ASSERT_FALSE(light.isEmpty());
  ASSERT_FALSE(dark.isEmpty());
  EXPECT_NE(light, dark);
  EXPECT_EQ(context.status().mermaid_context.renders, 2U);
  EXPECT_EQ(context.status().mermaid_context.memory_cache_hits, 0U);

  EXPECT_EQ(MermaidToImgTag(context, source, MarkdownTheme::Dark), dark);
  EXPECT_EQ(context.status().mermaid_context.memory_cache_hits, 1U);
}

// 主题走 per-diagram 指令，initialize 只做一次：两个视图交替不同主题时，
// 后渲染的不会污染前面的（回归点：曾用引擎级 initialize 切主题）
TEST(MermaidRendererTest, ThemesDoNotLeakAcrossContexts) {
  MarkdownRenderContext light_context;
  MarkdownRenderContext dark_context;
  DisablePersistentCache(light_context);
  DisablePersistentCache(dark_context);
  const QString source = QStringLiteral("flowchart LR\n    X --> Y\n");
  const QString light =
      MermaidToImgTag(light_context, source, MarkdownTheme::Light);
  const QString dark =
      MermaidToImgTag(dark_context, source, MarkdownTheme::Dark);
  ASSERT_FALSE(light.isEmpty());
  ASSERT_FALSE(dark.isEmpty());
  EXPECT_NE(light, dark);

  // 清缓存重渲：结果只由各自主题决定，与渲染顺序无关
  light_context.ClearMemoryCaches();
  dark_context.ClearMemoryCaches();
  EXPECT_EQ(MermaidToImgTag(light_context, source, MarkdownTheme::Light),
            light);
  EXPECT_EQ(MermaidToImgTag(dark_context, source, MarkdownTheme::Dark), dark);
}

// YAML frontmatter 必须留在文档最开头：主题指令要插到它后面，否则 mermaid
// 认不出 frontmatter，整张图会渲染失败
TEST(MermaidRendererTest, ThemeDirectiveKeepsFrontmatterFirst) {
  MarkdownRenderContext context;
  const QString source =
      QStringLiteral("---\ntitle: demo\n---\nflowchart LR\n    X --> Y\n");
  const QString light = MermaidToImgTag(context, source, MarkdownTheme::Light);
  const QString dark = MermaidToImgTag(context, source, MarkdownTheme::Dark);
  ASSERT_FALSE(light.isEmpty()) << "带 frontmatter 的图在 Light 下失败";
  ASSERT_FALSE(dark.isEmpty()) << "主题指令破坏了 frontmatter";
  EXPECT_NE(light, dark);
}

}  // namespace
