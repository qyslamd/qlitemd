#include "command_palette.h"

#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QTest>
#include <QWidget>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

QVector<PaletteItem> Items(int* fired) {
  QVector<PaletteItem> items;
  PaletteItem first;
  first.category = QStringLiteral("DOCUMENT");
  first.title = QStringLiteral("alpha");
  first.detail = QStringLiteral("docs/alpha.md");
  first.action = [fired] { *fired = 1; };
  items.push_back(first);
  PaletteItem second;
  second.category = QStringLiteral("SECTION");
  second.title = QStringLiteral("beta");
  second.detail = QStringLiteral("H2");
  second.action = [fired] { *fired = 2; };
  items.push_back(second);
  PaletteItem third;
  third.category = QStringLiteral("COMMAND");
  third.title = QStringLiteral("gamma");
  third.detail = QStringLiteral("W");
  third.action = [fired] { *fired = 3; };
  items.push_back(third);
  return items;
}

QLineEdit* Input(CommandPalette& palette) {
  return palette.findChild<QLineEdit*>(QStringLiteral("paletteInput"));
}

QListWidget* List(CommandPalette& palette) {
  return palette.findChild<QListWidget*>(QStringLiteral("paletteList"));
}

}  // namespace

TEST(CommandPaletteTest, FiltersAndRunsCurrentItem) {
  QWidget host;
  host.resize(800, 600);
  host.show();
  CommandPalette palette(&host);
  int fired = 0;
  palette.SetItems(Items(&fired));
  palette.Open();
  QLineEdit* input = Input(palette);
  QListWidget* list = List(palette);
  ASSERT_NE(input, nullptr);
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->count(), 3);

  QTest::keyClicks(input, QStringLiteral("beta"));
  EXPECT_EQ(list->count(), 1);
  QTest::keyClick(input, Qt::Key_Return);
  EXPECT_EQ(fired, 2);
  EXPECT_FALSE(palette.is_open());
}

TEST(CommandPaletteTest, DownAndEnterUseSecondItem) {
  QWidget host;
  host.resize(800, 600);
  host.show();
  CommandPalette palette(&host);
  int fired = 0;
  palette.SetItems(Items(&fired));
  palette.Open();
  QLineEdit* input = Input(palette);
  QTest::keyClick(input, Qt::Key_Down);
  QTest::keyClick(input, Qt::Key_Return);
  EXPECT_EQ(fired, 2);
}

TEST(CommandPaletteTest, MatchesCategoryAndClosesOnEscape) {
  QWidget host;
  host.resize(800, 600);
  host.show();
  CommandPalette palette(&host);
  int fired = 0;
  palette.SetItems(Items(&fired));
  palette.Open();
  QLineEdit* input = Input(palette);
  QListWidget* list = List(palette);
  QTest::keyClicks(input, QStringLiteral("comm"));
  EXPECT_EQ(list->count(), 1);
  QTest::keyClick(input, Qt::Key_Escape);
  EXPECT_FALSE(palette.is_open());
  EXPECT_EQ(fired, 0);
}
