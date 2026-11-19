#include "quick_open.h"

#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

QuickOpenItem MakeItem(QuickOpenKind kind, const QString& title,
                       const QString& subtitle = QString(),
                       const QString& command_id = QString()) {
  QuickOpenItem item;
  item.kind = kind;
  item.title = title;
  item.subtitle = subtitle;
  item.command_id = command_id;
  return item;
}

}  // namespace

TEST(QuickOpenTest, EmptyQueryKeepsOrderAndLimit) {
  QVector<QuickOpenItem> items;
  for (const QString& title :
       {QStringLiteral("alpha"), QStringLiteral("bravo"),
        QStringLiteral("charlie"), QStringLiteral("delta")}) {
    items.append(MakeItem(QuickOpenKind::kFile, title));
  }
  const QVector<QuickOpenItem> result =
      FilterQuickOpenItems(items, QString(), 3);
  ASSERT_EQ(result.size(), 3);
  EXPECT_EQ(result.at(0).title, QStringLiteral("alpha"));
  EXPECT_EQ(result.at(1).title, QStringLiteral("bravo"));
  EXPECT_EQ(result.at(2).title, QStringLiteral("charlie"));
}

TEST(QuickOpenTest, ExactAndPrefixTitlesRankFirst) {
  QVector<QuickOpenItem> items{
      MakeItem(QuickOpenKind::kFile, QStringLiteral("Markdown Guide")),
      MakeItem(QuickOpenKind::kHeading, QStringLiteral("Guide")),
      MakeItem(QuickOpenKind::kFile, QStringLiteral("Guided Tour")),
  };
  const QVector<QuickOpenItem> result =
      FilterQuickOpenItems(items, QStringLiteral("guide"), 10);
  ASSERT_EQ(result.size(), 3);
  EXPECT_EQ(result.at(0).title, QStringLiteral("Guide"));
  EXPECT_EQ(result.at(1).title, QStringLiteral("Guided Tour"));
  EXPECT_EQ(result.at(2).title, QStringLiteral("Markdown Guide"));
}

TEST(QuickOpenTest, SubtitlePathMatches) {
  QVector<QuickOpenItem> items{
      MakeItem(QuickOpenKind::kFile, QStringLiteral("Chapter 1"),
               QStringLiteral("guide/install.md")),
      MakeItem(QuickOpenKind::kFile, QStringLiteral("Chapter 2"),
               QStringLiteral("guide/usage.md")),
  };
  const QVector<QuickOpenItem> result =
      FilterQuickOpenItems(items, QStringLiteral("install"), 10);
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result.at(0).title, QStringLiteral("Chapter 1"));
}

TEST(QuickOpenTest, MatchIsCaseInsensitive) {
  const int score = QuickOpenScore(QStringLiteral("Getting Started"),
                                   QStringLiteral("gEtTiNg"));
  EXPECT_GT(score, 0);
  EXPECT_EQ(
      QuickOpenScore(QStringLiteral("Getting Started"), QStringLiteral("zzz")),
      0);
}

TEST(QuickOpenTest, PopupFiltersAndActivatesWithEnter) {
  qRegisterMetaType<QuickOpenItem>();
  QuickOpenPopup popup;
  QVector<QuickOpenItem> items{
      MakeItem(QuickOpenKind::kCommand, QStringLiteral("Zoom In"),
               QStringLiteral("放大"), QStringLiteral("zoom-in")),
      MakeItem(QuickOpenKind::kCommand, QStringLiteral("Focus Mode"),
               QStringLiteral("专注模式"), QStringLiteral("focus-mode")),
  };
  popup.Open(items);
  auto* edit = popup.findChild<QLineEdit*>(QStringLiteral("quickOpenQuery"));
  auto* list = popup.findChild<QListWidget*>(QStringLiteral("quickOpenList"));
  ASSERT_NE(edit, nullptr);
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->count(), 2);

  edit->setText(QStringLiteral("focus"));
  QCoreApplication::processEvents();
  ASSERT_EQ(list->count(), 1);
  EXPECT_EQ(popup.shown_count(), 1);

  QSignalSpy spy(&popup, &QuickOpenPopup::itemActivated);
  QTest::keyClick(edit, Qt::Key_Return);
  ASSERT_EQ(spy.count(), 1);
  const QuickOpenItem activated = spy.takeFirst().at(0).value<QuickOpenItem>();
  EXPECT_EQ(activated.command_id, QStringLiteral("focus-mode"));
}
