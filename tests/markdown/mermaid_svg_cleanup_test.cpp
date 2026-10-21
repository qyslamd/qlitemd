#include "mermaid_svg_cleanup.h"

#include <gtest/gtest.h>

#include <QString>

// 后处理两层的判据。都是纯字符串→字符串，毫秒级、可逐条断言——渲染出来长什么样
// 是像素的事（见 test_mermaid_renderer），这里钉的是"位置有没有烘焙对""箭头插
// 没插在正确的位置"这类一旦错了就静默出怪图的地方
namespace {

// mermaid 把真正的基线写在定位 tspan 上（em 单位），而 Qt 的 QSvgTspan 只存
// 文字与样式、不存位置：不烘焙回来，标签会整体偏高
TEST(MermaidSvgCleanupTest, BakesTspanPositionIntoText) {
  const QString svg = QStringLiteral(
      "<svg><text y=\"-10.1\">"
      "<tspan class=\"text-outer-tspan row\" x=\"0\" y=\"-0.1em\" dy=\"1.1em\">"
      "<tspan class=\"text-inner-tspan\">A</tspan>"
      "</tspan></text></svg>");

  const QString out = CleanSvg(svg);

  // -0.1em + 1.1em = 1em = 16px（x/y 绝对覆盖、dy 相对累加）
  EXPECT_TRUE(out.contains(QStringLiteral("y=\"16\""))) << out.toStdString();
  // 行 tspan 的 class 要并到 <text> 上：CSS 可能按它定位
  EXPECT_TRUE(out.contains(QStringLiteral("text-outer-tspan")))
      << out.toStdString();
  // run 摊平：Qt 只接受 tspan 作为 <text> 的直接孩子，嵌套会报
  // "Could not add child element ... types are incorrect"
  EXPECT_FALSE(out.contains(QStringLiteral("</tspan></tspan")))
      << out.toStdString();
  // run 之间的空格写在后一个 run 开头，Qt 默认空白模式会吃掉
  EXPECT_TRUE(out.contains(QStringLiteral("xml:space=\"preserve\"")))
      << out.toStdString();
}

// 叶子行（如 sequence 的 actor 名字）：外壳必须留着，CSS 按 "text.actor>tspan"
// 定位，少一层规则就匹配不上，名字会退回盒子的填充色
TEST(MermaidSvgCleanupTest, KeepsTspanShellForLeafRows) {
  const QString svg = QStringLiteral(
      "<svg><text class=\"actor actor-box\" x=\"75\" y=\"201.5\">"
      "<tspan x=\"75\" dy=\"0\">Alice</tspan></text></svg>");

  const QString out = CleanSvg(svg);

  EXPECT_TRUE(out.contains(QStringLiteral("<tspan>Alice</tspan>")))
      << out.toStdString();
  EXPECT_TRUE(out.contains(QStringLiteral("y=\"201.5\""))) << out.toStdString();
}

// 无尺寸的 <rect class="background"> 会被 QSvgRenderer 画成黑块；
// stroke-dasharray="0" 会被理解成"虚线长 0"，连线整条不画
TEST(MermaidSvgCleanupTest, DropsBackgroundRectsAndFixesDashArray) {
  const QString svg = QStringLiteral(
      "<svg><rect class=\"background\" style=\"stroke: none\"></rect>"
      "<path stroke-dasharray=\"0\" d=\"M0,0 L1,1\"></path></svg>");

  const QString out = CleanSvg(svg);

  EXPECT_FALSE(out.contains(QStringLiteral("class=\"background\"")))
      << out.toStdString();
  EXPECT_TRUE(out.contains(QStringLiteral("stroke-dasharray=\"none\"")))
      << out.toStdString();
}

// marker 展开：几何按 marker 自己的定义算（viewBox → markerWidth 缩放、
// refX/refY 对齐端点），展开出来的 <g> 必须是原图形的**兄弟**节点——
// 塞进 <path> 里面 SVG 根本不画
TEST(MermaidSvgCleanupTest, ExpandsMarkerEndIntoASiblingGroup) {
  const QString svg = QStringLiteral(
      "<svg><defs>"
      "<marker id=\"a\" viewBox=\"0 0 10 10\" refX=\"5\" refY=\"5\""
      " markerWidth=\"8\" markerHeight=\"8\" markerUnits=\"userSpaceOnUse\""
      " orient=\"auto\"><path d=\"M 0 0 L 10 5 L 0 10 z\"></path></marker>"
      "</defs><path d=\"M0,0 L100,0\" marker-end=\"url(#a)\"></path></svg>");

  const QString out = InlineSvgMarkers(svg);

  EXPECT_FALSE(out.contains(QStringLiteral("<marker"))) << out.toStdString();
  EXPECT_FALSE(out.contains(QStringLiteral("marker-end"))) << out.toStdString();
  EXPECT_TRUE(out.contains(QStringLiteral("scale(0.8, 0.8)")))
      << out.toStdString();
  EXPECT_TRUE(out.contains(QStringLiteral("translate(-5, -5)")))
      << out.toStdString();

  const int path_end = out.indexOf(QStringLiteral("</path>"));
  const int arrow = out.indexOf(QStringLiteral("<g transform="));
  ASSERT_GE(path_end, 0);
  EXPECT_GT(arrow, path_end) << out.toStdString();
}

// 非自闭合图形带子内容（<title>/注释）时，内容与闭合标签必须原样保留，
// 箭头放在闭合标签之后——不能把"当前 mermaid 输出都自闭合"当成契约
TEST(MermaidSvgCleanupTest, ShapeWithChildContentKeepsItAndArrowFollows) {
  const QString svg = QStringLiteral(
      "<svg><defs>"
      "<marker id=\"a\" refX=\"0\" refY=\"0\" orient=\"auto\">"
      "<path d=\"M0,0 L5,5 z\"></path></marker>"
      "</defs><path d=\"M0,0 L100,0\" marker-end=\"url(#a)\">"
      "<title>tip</title></path></svg>");

  const QString out = InlineSvgMarkers(svg);

  EXPECT_TRUE(out.contains(QStringLiteral("<title>tip</title></path>")))
      << out.toStdString();
  const int close = out.indexOf(QStringLiteral("</path>"));
  const int arrow = out.indexOf(QStringLiteral("<g transform="));
  ASSERT_GE(close, 0) << out.toStdString();
  EXPECT_GT(arrow, close) << out.toStdString();
}

// 起点箭头用"路径前进方向"，只有 orient="auto-start-reverse" 才翻 180°：
// 翻错方向会把箭头画到路径另一侧（class 图的继承空心三角就是这么歪的）
TEST(MermaidSvgCleanupTest, StartMarkerUsesPathDirection) {
  const QString svg = QStringLiteral(
      "<svg><defs>"
      "<marker id=\"a\" refX=\"0\" refY=\"0\" orient=\"auto\">"
      "<path d=\"M0,0 L5,5 z\"></path></marker>"
      "<marker id=\"b\" refX=\"0\" refY=\"0\" orient=\"auto-start-reverse\">"
      "<path d=\"M0,0 L5,5 z\"></path></marker>"
      "</defs><path d=\"M0,0 L0,100\" marker-start=\"url(#a)\"></path>"
      "<path d=\"M0,0 L0,100\" marker-start=\"url(#b)\"></path></svg>");

  const QString out = InlineSvgMarkers(svg);

  // 直下方向的路径：auto 出 90°，auto-start-reverse 再翻 180° 出 270°
  // （270 与 -90 是同一个角，atan2 的角度范围不同而已）
  EXPECT_TRUE(out.contains(QStringLiteral("rotate(90)"))) << out.toStdString();
  EXPECT_TRUE(out.contains(QStringLiteral("rotate(270)"))) << out.toStdString();
}

// marker-mid：每个中间拐点都画一个，方向取入/出两段单位向量之和的角平分线
TEST(MarkerMidTest, EmitsAtEveryInteriorVertex) {
  const QString svg = QStringLiteral(
      "<svg><defs>"
      "<marker id=\"a\" refX=\"0\" refY=\"0\" orient=\"auto\">"
      "<path d=\"M0,0 L5,5 z\"></path></marker>"
      "</defs><polyline points=\"0,0 100,0 100,100\""
      " marker-mid=\"url(#a)\"></polyline></svg>");

  const QString out = InlineSvgMarkers(svg);

  // 内部顶点 (100,0)：入段 (100,0)、出段 (0,100)，和向量 → 45°
  EXPECT_TRUE(out.contains(QStringLiteral("rotate(45)"))) << out.toStdString();
  // 引用与定义都清干净，不留悬空 marker
  EXPECT_FALSE(out.contains(QStringLiteral("marker-mid"))) << out.toStdString();
  EXPECT_FALSE(out.contains(QStringLiteral("<marker"))) << out.toStdString();
}

// viewBox 原点非 0：refX/refY 与内容同处一个坐标系，对齐平移只有 −ref——
// vb 的平移项在对齐差里相消，它只贡献缩放。把它塞进平移会让 marker 整体
// 偏移 −scale·vb（这里会错成 translate(-45, -25)）
TEST(MarkerViewBoxOriginTest, NonZeroOriginOnlyScales) {
  const QString svg = QStringLiteral(
      "<svg><defs>"
      "<marker id=\"a\" viewBox=\"20 10 10 10\" refX=\"25\" refY=\"15\""
      " markerWidth=\"8\" markerHeight=\"8\" markerUnits=\"userSpaceOnUse\""
      " orient=\"auto\"><path d=\"M 20 10 L 30 15 L 20 20 z\"></path></marker>"
      "</defs><path d=\"M0,0 L100,0\" marker-end=\"url(#a)\"></path></svg>");

  const QString out = InlineSvgMarkers(svg);

  EXPECT_TRUE(out.contains(QStringLiteral("scale(0.8, 0.8)")))
      << out.toStdString();
  EXPECT_TRUE(out.contains(QStringLiteral("translate(-25, -15)")))
      << out.toStdString();
  EXPECT_FALSE(out.contains(QStringLiteral("translate(-45, -25)")))
      << out.toStdString();
}

// ---------- InlineSvgCss 的层叠 ----------

// 内联 style 的位次高于"规则 normal"：元素自带 style 里的 fill 不该被
// <style> 表里的普通规则盖掉（CSS：内联 normal > 规则 normal）
TEST(MermaidCssInlinerTest, InlineStyleBeatsNormalRules) {
  const QString svg = QStringLiteral(
      "<svg><style>rect.node { fill: red; }</style>"
      "<rect class=\"node\" style=\"fill: blue\"></rect></svg>");

  const QString out = InlineSvgCss(svg, QString());

  // fill 在 pres-attrs 里会单独写一份属性（QSvgRenderer 认属性不认 style）
  EXPECT_TRUE(out.contains(QStringLiteral("fill=\"blue\"")))
      << out.toStdString();
  EXPECT_FALSE(out.contains(QStringLiteral("red"))) << out.toStdString();
}

// 例外只有一条：规则的 !important 压过内联 normal（CSS：author !important
// > 内联 normal）
TEST(MermaidCssInlinerTest, RuleImportantBeatsInlineNormal) {
  const QString svg = QStringLiteral(
      "<svg><style>rect.node { fill: red !important; }</style>"
      "<rect class=\"node\" style=\"fill: blue\"></rect></svg>");

  const QString out = InlineSvgCss(svg, QString());

  EXPECT_FALSE(out.contains(QStringLiteral("blue"))) << out.toStdString();
}

// 同一条 style 里 !important 先于顺序：后面那条普通声明不能把前一条的
// important 声明顶掉（CSS 层叠先比 origin+importance，再比出现顺序）。
// 老实现无条件后写覆盖，输出 "fill: green"，与浏览器不一致
TEST(MermaidCssInlinerTest, ImportantBeatsLaterNormalInSameStyle) {
  // 必须有一条（不命中的）规则：没有 <style> 时 InlineSvgCss 原样返回，
  // 根本不解析内联 style
  const QString svg = QStringLiteral(
      "<svg><style>.dummy { fill: red; }</style>"
      "<rect style=\"fill: blue !important; fill: green\"></rect></svg>");

  const QString out = InlineSvgCss(svg, QString());

  EXPECT_TRUE(out.contains(QStringLiteral("fill=\"blue\"")))
      << out.toStdString();
  EXPECT_TRUE(out.contains(QStringLiteral("!important"))) << out.toStdString();
  EXPECT_FALSE(out.contains(QStringLiteral("green"))) << out.toStdString();
}

// 同属性按 specificity 决胜、同 specificity 才按文档顺序：tag 选择器在前、
// .class 选择器在后（文档顺序对 class 有利），class 仍应赢——老实现按文档
// 顺序，这里会错出 green
TEST(MermaidCssInlinerTest, InlineImportantIsTheCeiling) {
  const QString svg = QStringLiteral(
      "<svg><style>rect.node { fill: red !important; }</style>"
      "<rect class=\"node\" style=\"fill: blue !important\"></rect></svg>");

  const QString out = InlineSvgCss(svg, QString());

  // style 属性的 specificity 是 (1,0,0,0) 级：author 源的天花板，
  // 外部 !important 也压不动
  EXPECT_TRUE(out.contains(QStringLiteral("fill=\"blue\"")))
      << out.toStdString();
  EXPECT_FALSE(out.contains(QStringLiteral("red"))) << out.toStdString();
}

TEST(MermaidCssInlinerTest, SpecificityBeatsDocumentOrder) {
  const QString svg = QStringLiteral(
      "<svg><style>rect { fill: green; } .node { fill: red; }</style>"
      "<rect class=\"node\"></rect></svg>");

  const QString out = InlineSvgCss(svg, QString());

  EXPECT_FALSE(out.contains(QStringLiteral("green"))) << out.toStdString();
}

}  // namespace
