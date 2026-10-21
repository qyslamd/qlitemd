#include "formula_renderer.h"

#include <gtest/gtest.h>
#include <thread>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "markdown_engine_state.h"
#include "markdown_render_context.h"
#include "markdown_render_context_internal.h"
#include "markdown_theme.h"
#include "markdown_types.h"

// 公式渲染的端到端判据：加载 qrc 里的 MathJax 载荷 → QuickJS 里 tex→svg →
// QSvgRenderer 画成 PNG → 内联成 <img>。整条链路要么全通要么全不通，中间任何
// 一环坏了都只能从"像素"看出来——所以在无头环境把产物本身断言掉。
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

// 自建上下文默认开启持久缓存；断言"每个上下文各自渲染"的用例要关掉，
// 否则第二个上下文会直接读盘（磁盘缓存本来就是跨上下文共享的）
void DisablePersistentCache(MarkdownRenderContext& context) {
  // SetOptions 是整体替换：先取当前值，只改目标字段，别把调用方已经设好
  // 的目录/诊断/容量清掉
  MarkdownRenderContextOptions options = context.options();
  options.persistent_cache_enabled = false;
  context.SetOptions(options);
}

TEST(FormulaRendererTest, RendersInlineFormulaToInlineImage) {
  ASSERT_TRUE(FormulaRendererReady()) << "qrc 里的 MathJax 载荷没加载起来";
  const QString tag = FormulaToImgTag(QStringLiteral("E = mc^2"), false);
  ASSERT_FALSE(tag.isEmpty()) << "渲染失败（会退回显示原文）";
  EXPECT_TRUE(tag.startsWith(QStringLiteral("<img"))) << tag.toStdString();
  EXPECT_TRUE(tag.contains(QStringLiteral("data:image/png;base64,")))
      << tag.toStdString();
  // 行内公式不加 display 类，随文排
  EXPECT_FALSE(tag.contains(QStringLiteral("math-display")))
      << tag.toStdString();
}

// 显式 EngineState 必须能在非主线程独立加载并渲染
TEST(FormulaRendererTest, ExplicitEngineRendersOnWorkerThread) {
  QString tag;
  std::thread worker([&tag] {
    FormulaEngineState engine;
    const MarkdownDiagnostics diagnostics;
    tag = RenderFormulaOnEngine(engine, QStringLiteral("x^2"), false, QColor(),
                                diagnostics);
  });
  worker.join();

  EXPECT_TRUE(tag.startsWith(QStringLiteral("<img"))) << tag.toStdString();
}

TEST(FormulaRendererTest, DisplayFormulaGetsDisplayClass) {
  const QString tag = FormulaToImgTag(QStringLiteral("\\frac{a}{b}"), true);
  ASSERT_FALSE(tag.isEmpty());
  EXPECT_TRUE(tag.contains(QStringLiteral("class=\"math-display\"")))
      << tag.toStdString();
}

// 分式是"必须走图"的理由：KaTeX 的 HTML 方案在 qlitehtml 下排不动它，
// 而 SVG 路线不挑内容——上下标、根号、求和都该出得来
TEST(FormulaRendererTest, HandlesStructuredFormulas) {
  const QStringList texes = {
      QStringLiteral("\\sum_{i=1}^{n} x_i^2"),
      QStringLiteral("\\int_{-\\infty}^{\\infty} e^{-x^2}\\,dx = \\sqrt{\\pi}"),
      QStringLiteral(
          "\\nabla \\cdot \\mathbf{E} = \\frac{\\rho}{\\varepsilon_0}"),
  };
  for (const QString& tex : texes) {
    EXPECT_FALSE(FormulaToImgTag(tex, true).isEmpty()) << tex.toStdString();
  }
}

// 结果按 (tex, display) 缓存：同一个公式再来一次应逐字相同（也顺带证明缓存
// 没把 display 与非 display 混在一起）
TEST(FormulaRendererTest, CachesByTexAndDisplay) {
  const QString first =
      FormulaToImgTag(QStringLiteral("a^2 + b^2 = c^2"), false);
  const QString second =
      FormulaToImgTag(QStringLiteral("a^2 + b^2 = c^2"), false);
  ASSERT_FALSE(first.isEmpty());
  EXPECT_EQ(first, second);
  EXPECT_NE(first, FormulaToImgTag(QStringLiteral("a^2 + b^2 = c^2"), true));
}

// 上下文隔离：每个视图一份缓存与计数，清一个不影响另一个
TEST(FormulaRendererTest, ContextsKeepCachesSeparate) {
  MarkdownRenderContext first;
  MarkdownRenderContext second;
  DisablePersistentCache(first);
  DisablePersistentCache(second);
  const QString tag = FormulaToImgTag(first, QStringLiteral("x^2"), false);
  ASSERT_FALSE(tag.isEmpty());
  EXPECT_EQ(first.status().formula_context.renders, 1U);
  EXPECT_EQ(second.status().formula_context.renders, 0U);

  EXPECT_EQ(FormulaToImgTag(second, QStringLiteral("x^2"), false), tag);
  EXPECT_EQ(second.status().formula_context.renders, 1U);

  EXPECT_EQ(FormulaToImgTag(first, QStringLiteral("x^2"), false), tag);
  EXPECT_EQ(first.status().formula_context.memory_cache_hits, 1U);

  first.ClearMemoryCaches();
  EXPECT_EQ(FormulaToImgTag(first, QStringLiteral("x^2"), false), tag);
  EXPECT_EQ(first.status().formula_context.renders, 2U);

  EXPECT_EQ(FormulaToImgTag(second, QStringLiteral("x^2"), false), tag);
  EXPECT_EQ(second.status().formula_context.renders, 1U);
  EXPECT_EQ(second.status().formula_context.memory_cache_hits, 1U);
}

// 旧 API 的语义要钉住：ClearFormulaCache() 只清进程默认上下文，
// 自定义/视图上下文必须走 ClearMemoryCaches()
TEST(FormulaRendererTest, ClearFormulaCacheOnlyTouchesDefaultContext) {
  MarkdownRenderContext custom;
  MarkdownRenderContextState& custom_state =
      MarkdownRenderContextAccess::State(custom);
  custom_state.formula_cache.Insert(QStringLiteral("key"),
                                    QStringLiteral("value"));
  ClearFormulaCache();
  EXPECT_EQ(custom_state.formula_cache.entries.size(), 1);

  MarkdownRenderContextState& default_state =
      MarkdownRenderContextAccess::State(DefaultRenderContext());
  default_state.formula_cache.Insert(QStringLiteral("key"),
                                     QStringLiteral("value"));
  ClearFormulaCache();
  EXPECT_TRUE(default_state.formula_cache.entries.isEmpty());
}

// 主题前景色参与 key：亮/暗主题各占一条，切换主题不会拿错图
TEST(FormulaRendererTest, ThemeColorIsPartOfCacheKey) {
  MarkdownRenderContext context;
  DisablePersistentCache(context);
  const QColor light = ThemeFormulaColor(MarkdownTheme::Light);
  const QColor dark = ThemeFormulaColor(MarkdownTheme::Dark);
  const QString light_tag =
      FormulaToImgTag(context, QStringLiteral("x"), false, light);
  const QString dark_tag =
      FormulaToImgTag(context, QStringLiteral("x"), false, dark);
  ASSERT_FALSE(light_tag.isEmpty());
  ASSERT_FALSE(dark_tag.isEmpty());
  EXPECT_NE(light_tag, dark_tag);
  EXPECT_EQ(context.status().formula_context.renders, 2U);
  EXPECT_EQ(context.status().formula_context.memory_cache_hits, 0U);

  EXPECT_EQ(FormulaToImgTag(context, QStringLiteral("x"), false, light),
            light_tag);
  EXPECT_EQ(context.status().formula_context.memory_cache_hits, 1U);
}

// 持久缓存跨上下文复用：同一 key 的第二个上下文直接读盘，不再执行渲染
TEST(FormulaRendererTest, DiskCacheSurvivesAcrossContexts) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  MarkdownRenderContextOptions options;
  options.cache_directory = dir.path();
  const QColor color = ThemeFormulaColor(MarkdownTheme::Dark);

  MarkdownRenderContext first;
  first.SetOptions(options);
  const QString tag =
      FormulaToImgTag(first, QStringLiteral("x+1"), false, color);
  ASSERT_FALSE(tag.isEmpty());
  EXPECT_EQ(first.status().formula_context.renders, 1U);
  EXPECT_EQ(first.status().formula_context.disk_cache_misses, 1U);
  EXPECT_EQ(QDir(dir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot),
            QStringList{QStringLiteral("formula")});

  MarkdownRenderContext second;
  second.SetOptions(options);
  EXPECT_EQ(FormulaToImgTag(second, QStringLiteral("x+1"), false, color), tag);
  EXPECT_EQ(second.status().formula_context.renders, 0U);
  EXPECT_EQ(second.status().formula_context.disk_cache_hits, 1U);
}

// MathJax 对首尾空白不敏感：key 与渲染输入都 trim，避免同一条公式存两份
TEST(FormulaRendererTest, TrimsTexForCacheKey) {
  MarkdownRenderContext context;
  DisablePersistentCache(context);
  const QString first = FormulaToImgTag(context, QStringLiteral(" x "), false);
  ASSERT_FALSE(first.isEmpty());
  EXPECT_EQ(FormulaToImgTag(context, QStringLiteral("x"), false), first);
  EXPECT_EQ(context.status().formula_context.renders, 1U);
  EXPECT_EQ(context.status().formula_context.memory_cache_hits, 1U);
}

// 畸形输入不抛、不崩。约定是"空串 = 渲不出来，调用方退回显示原文"，但别指望
// 畸形一定返回空：MathJax 相当宽容（`\frac{` 会渲成缺参数的分数、`$$$` 直接
// 当字面量），真正返回空的是载荷缺失、JS 异常、SVG 无效这类内部失败。
// 所以这里只钉不变量：要么空，要么是个well-formed 的 img
TEST(FormulaRendererTest, MalformedTexNeverCrashes) {
  const QStringList bad = {QStringLiteral("\\frac{"), QStringLiteral("$$$"),
                           QStringLiteral("\\begin{matrix}"),
                           QStringLiteral("{}{}{")};
  for (const QString& tex : bad) {
    const QString tag = FormulaToImgTag(tex, false);
    const bool well_formed =
        tag.startsWith(QStringLiteral("<img")) &&
        tag.contains(QStringLiteral("data:image/png;base64,"));
    EXPECT_TRUE(tag.isEmpty() || well_formed) << tex.toStdString();
  }
}

}  // namespace
