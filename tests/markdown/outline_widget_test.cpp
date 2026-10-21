#include <gtest/gtest.h>

#include <QApplication>
#include <QTest>
#include <QTreeWidgetItem>
#include <QVector>

#include "markdown_outline_widget.h"

// 大纲控件的判据：层级结构、点击回跳、当前章节高亮。控件是纯数据驱动，
// 不依赖 MarkdownView，所以直接喂 MarkdownHeading 列表
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

QVector<MarkdownHeading> SampleHeadings() {
  return {
      {1, QStringLiteral("A"), QStringLiteral("a")},
      {2, QStringLiteral("A1"), QStringLiteral("a1")},
      {3, QStringLiteral("A1x"), QStringLiteral("a1x")},
      {2, QStringLiteral("A2"), QStringLiteral("a2")},
      {1, QStringLiteral("B"), QStringLiteral("b")},
  };
}

TEST(MarkdownOutlineWidgetTest, BuildsHierarchyFromLevels) {
  MarkdownOutlineWidget widget;
  widget.SetHeadings(SampleHeadings());

  ASSERT_EQ(widget.topLevelItemCount(), 2);
  QTreeWidgetItem* a = widget.topLevelItem(0);
  QTreeWidgetItem* b = widget.topLevelItem(1);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(a->text(0), QStringLiteral("A"));
  EXPECT_EQ(b->text(0), QStringLiteral("B"));
  ASSERT_EQ(a->childCount(), 2);
  EXPECT_EQ(a->child(0)->text(0), QStringLiteral("A1"));
  EXPECT_EQ(a->child(1)->text(0), QStringLiteral("A2"));
  ASSERT_EQ(a->child(0)->childCount(), 1);
  EXPECT_EQ(a->child(0)->child(0)->text(0), QStringLiteral("A1x"));
}

TEST(MarkdownOutlineWidgetTest, ClickEmitsAnchor) {
  MarkdownOutlineWidget widget;
  widget.SetHeadings(SampleHeadings());
  widget.resize(320, 480);
  widget.show();

  QString activated;
  QObject::connect(&widget, &MarkdownOutlineWidget::headingActivated,
                   [&activated](const QString& anchor) { activated = anchor; });
  QApplication::processEvents();

  QTreeWidgetItem* b = widget.topLevelItem(1);
  ASSERT_NE(b, nullptr);
  const QRect rect = widget.visualItemRect(b);
  ASSERT_TRUE(rect.isValid());
  QTest::mouseClick(widget.viewport(), Qt::LeftButton, Qt::NoModifier,
                    rect.center());
  EXPECT_EQ(activated, QStringLiteral("b"));
}

TEST(MarkdownOutlineWidgetTest, CurrentAnchorFollowsLookup) {
  MarkdownOutlineWidget widget;
  widget.SetHeadings(SampleHeadings());

  widget.SetCurrentAnchor(QStringLiteral("a1"));
  ASSERT_NE(widget.currentItem(), nullptr);
  EXPECT_EQ(widget.currentItem()->text(0), QStringLiteral("A1"));

  widget.SetCurrentAnchor(QStringLiteral("missing"));
  EXPECT_EQ(widget.currentItem(), nullptr);
}

TEST(MarkdownOutlineWidgetTest, EmptyHeadingsClearTheTree) {
  MarkdownOutlineWidget widget;
  widget.SetHeadings(SampleHeadings());
  ASSERT_EQ(widget.topLevelItemCount(), 2);
  widget.SetHeadings({});
  EXPECT_EQ(widget.topLevelItemCount(), 0);
  EXPECT_EQ(widget.currentItem(), nullptr);
}

}  // namespace
