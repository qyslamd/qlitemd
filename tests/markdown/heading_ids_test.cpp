#include "heading_ids.h"

#include <gtest/gtest.h>
#include <vector>

#include <QHash>
#include <QString>
#include <QVector>

namespace {

// 收集回调吐出来的 (slug, id) 对，验"宿主拿到的表"与 HTML 里挂的属性一致
struct HeadingProbe {
  QVector<QString> slugs;
  QVector<QString> ids;

  HeadingSink Sink() {
    return [this](const QString& slug, const QString& id) {
      slugs << slug;
      ids << id;
    };
  }
};

TEST(HeadingIdsTest, GivesCjkSlugAnAsciiId) {
  // id 走纯 ASCII 是硬约束：litehtml 解析时改写 id 的存储形式，非 ASCII
  // 存不住，scrollToAnchor 会静默找不到元素。slug 原样留在 data-md-slug
  EXPECT_EQ(
      AddHeadingIds(QStringLiteral("<h2>二级标题</h2>")),
      QStringLiteral("<h2 id=\"h-0\" data-md-slug=\"二级标题\">二级标题</h2>"));
}

// 标点丢弃、空白转 -、大写转小写——与 GitHub 的 slug 规则对齐
TEST(HeadingIdsTest, SlugFollowsGithubFlavor) {
  EXPECT_EQ(AddHeadingIds(QStringLiteral("<h2>Hello, World! (v2)</h2>")),
            QStringLiteral("<h2 id=\"h-0\" data-md-slug=\"hello-world-v2\">"
                           "Hello, World! (v2)</h2>"));
}

// slug 用纯文本算：内联标签参与显示但不进锚点
TEST(HeadingIdsTest, InlineTagsStayOutOfTheSlug) {
  EXPECT_EQ(AddHeadingIds(QStringLiteral("<h2>Hello <code>world</code></h2>")),
            QStringLiteral("<h2 id=\"h-0\" data-md-slug=\"hello-world\">"
                           "Hello <code>world</code></h2>"));
}

// 重名标题追加 -1 / -2（GitHub 同款）；id 因为带全局序号，天然不会撞
TEST(HeadingIdsTest, DuplicateSlugsGetSuffixes) {
  EXPECT_EQ(
      AddHeadingIds(QStringLiteral("<h2>Title</h2><h2>Title</h2>"
                                   "<h2>Title</h2>")),
      QStringLiteral("<h2 id=\"h-0\" data-md-slug=\"title\">Title</h2>"
                     "<h2 id=\"h-1\" data-md-slug=\"title-1\">Title</h2>"
                     "<h2 id=\"h-2\" data-md-slug=\"title-2\">Title</h2>"));
}

// 后缀要和"已占用集合"比较，不能只看原始 slug 出现次数：
// Foo / Foo-1 / Foo 的第三个标题必须绕开已存在的 foo-1
TEST(HeadingIdsTest, SuffixDoesNotCollideWithExplicitSlug) {
  EXPECT_EQ(AddHeadingIds(QStringLiteral("<h2>Foo</h2><h2>Foo-1</h2>"
                                         "<h2>Foo</h2>")),
            QStringLiteral("<h2 id=\"h-0\" data-md-slug=\"foo\">Foo</h2>"
                           "<h2 id=\"h-1\" data-md-slug=\"foo-1\">Foo-1</h2>"
                           "<h2 id=\"h-2\" data-md-slug=\"foo-2\">Foo</h2>"));
}

// 实体先解码再算 slug：'&' 是标点被丢，两侧空格各变一个 '-'
TEST(HeadingIdsTest, EntitiesDecodedBeforeSlugging) {
  EXPECT_EQ(
      AddHeadingIds(QStringLiteral("<h2>A &amp; B</h2>")),
      QStringLiteral("<h2 id=\"h-0\" data-md-slug=\"a--b\">A &amp; B</h2>"));
}

TEST(HeadingIdsTest, CoversH1ToH6) {
  EXPECT_EQ(AddHeadingIds(QStringLiteral("<h1>Top</h1><h3>Sub</h3>")),
            QStringLiteral("<h1 id=\"h-0\" data-md-slug=\"top\">Top</h1>"
                           "<h3 id=\"h-1\" data-md-slug=\"sub\">Sub</h3>"));
}

// 没有标题：一个字符都不动（别顺手规范化别的标签）
TEST(HeadingIdsTest, LeavesHtmlWithoutHeadingsAlone) {
  EXPECT_EQ(AddHeadingIds(QStringLiteral("<p>hello</p><ul><li>x</li></ul>")),
            QStringLiteral("<p>hello</p><ul><li>x</li></ul>"));
}

// 回调吐出的表就是宿主反查锚点的依据：键是 slug（与用户写的锚点一致），
// 值是元素 id。序号跨标题全局递增，重名也拿得到彼此不同的 id
TEST(HeadingIdsTest, SinkReportsSlugToIdInOrder) {
  HeadingProbe probe;
  AddHeadingIds(QStringLiteral("<h2>跳转测试</h2><h2>Title</h2>"
                               "<h2>Title</h2>"),
                probe.Sink());

  ASSERT_EQ(probe.slugs.size(), 3);
  EXPECT_EQ(probe.slugs.at(0), QStringLiteral("跳转测试"));
  EXPECT_EQ(probe.slugs.at(1), QStringLiteral("title"));
  EXPECT_EQ(probe.slugs.at(2), QStringLiteral("title-1"));
  EXPECT_EQ(probe.ids.at(0), QStringLiteral("h-0"));
  EXPECT_EQ(probe.ids.at(1), QStringLiteral("h-1"));
  EXPECT_EQ(probe.ids.at(2), QStringLiteral("h-2"));
}

// 大纲回调：层级、纯文本、最终 slug（含重名后缀）都要能直接给侧栏用
TEST(HeadingIdsTest, OutlineReportsLevelsTextAndAnchors) {
  std::vector<MarkdownHeading> headings;
  AddHeadingIds(QStringLiteral("<h1>Title</h1><h3>Sub <code>code</code></h3>"
                               "<h3>Sub</h3>"),
                {}, [&headings](const MarkdownHeading& heading) {
                  headings.push_back(heading);
                });

  ASSERT_EQ(headings.size(), 3);
  EXPECT_EQ(headings.at(0).level, 1);
  EXPECT_EQ(headings.at(0).text, QStringLiteral("Title"));
  EXPECT_EQ(headings.at(0).anchor, QStringLiteral("title"));
  EXPECT_EQ(headings.at(1).level, 3);
  EXPECT_EQ(headings.at(1).text, QStringLiteral("Sub code"));
  EXPECT_EQ(headings.at(1).anchor, QStringLiteral("sub-code"));
  EXPECT_EQ(headings.at(2).anchor, QStringLiteral("sub"));
}

// 不给回调也照样补 id：页内锚点表是宿主的事，正文的结构不该因此变样
TEST(HeadingIdsTest, WorksWithoutSink) {
  EXPECT_TRUE(AddHeadingIds(QStringLiteral("<h2>x</h2>"))
                  .contains(QStringLiteral("id=")));
}

}  // namespace
