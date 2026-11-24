#include "document_bar.h"

#include <QApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

DocumentBar* MakeShownBar(int width) {
  auto* bar = new DocumentBar;
  bar->resize(width, 32);
  bar->show();
  QApplication::processEvents();
  return bar;
}

}  // namespace

TEST(DocumentBarTest, ExposesAllControls) {
  DocumentBar bar;
  EXPECT_NE(bar.back_button(), nullptr);
  EXPECT_NE(bar.forward_button(), nullptr);
  EXPECT_NE(bar.star_button(), nullptr);
  EXPECT_NE(bar.appearance_button(), nullptr);
  EXPECT_NE(bar.zoom_out_button(), nullptr);
  EXPECT_NE(bar.zoom_reset_button(), nullptr);
  EXPECT_NE(bar.zoom_in_button(), nullptr);
  EXPECT_NE(bar.copy_button(), nullptr);
  EXPECT_NE(bar.find_edit(), nullptr);
  EXPECT_EQ(bar.find_edit()->objectName(), QStringLiteral("docFindEdit"));
  EXPECT_NE(bar.path_label(), nullptr);
  EXPECT_TRUE(bar.star_button()->isCheckable());
}

TEST(DocumentBarTest, ElidesPathButKeepsFullTooltip) {
  DocumentBar* bar = MakeShownBar(700);
  // 不同平台的字体度量差很多，路径要撑到必然放不下才谈得上省略
  const QString segment = QStringLiteral(
      "docs/guide/二层/三层/四层/一个非常非常长的文档标题用来撑满路径栏");
  QString full = segment;
  const QFontMetrics metrics(bar->path_label()->font());
  while (metrics.horizontalAdvance(full) <= bar->path_label()->width()) {
    full += QLatin1Char('/') + segment;
  }
  bar->SetPath(full,
               QStringLiteral("D:/trae_workspace/qlitemd/docs/guide/x.md"));
  QApplication::processEvents();
  EXPECT_NE(bar->path_label()->text(), full);
  EXPECT_TRUE(bar->path_label()->text().contains(QChar(0x2026)));
  EXPECT_EQ(bar->path_label()->toolTip(),
            QStringLiteral("D:/trae_workspace/qlitemd/docs/guide/x.md"));
  delete bar;
}

TEST(DocumentBarTest, ShrinksFindBoxInsteadOfHidingIt) {
  DocumentBar* wide = MakeShownBar(700);
  EXPECT_EQ(wide->find_edit()->width(), 240);
  delete wide;

  DocumentBar* narrow = MakeShownBar(600);
  EXPECT_TRUE(narrow->find_edit()->isVisible());
  EXPECT_EQ(narrow->find_edit()->width(), 160);
  delete narrow;
}

TEST(DocumentBarTest, HidesGroupsAsWidthShrinks) {
  struct Case {
    int width;
    bool zoom;
    bool search;
    bool appearance;
    bool copy;
  };
  // 阈值按整窗宽度定：缩放 860、外观 720、复制 620；搜索始终可见
  const Case cases[] = {
      {1000, true, true, true, true},   {860, true, true, true, true},
      {800, false, true, true, true},   {720, false, true, true, true},
      {700, false, true, false, true},  {620, false, true, false, true},
      {600, false, true, false, false}, {400, false, true, false, false},
  };
  for (const Case& item : cases) {
    DocumentBar* bar = MakeShownBar(item.width);
    EXPECT_EQ(bar->zoom_out_button()->isVisible(), item.zoom)
        << "width=" << item.width;
    EXPECT_EQ(bar->find_edit()->isVisible(), item.search)
        << "width=" << item.width;
    EXPECT_EQ(bar->appearance_button()->isVisible(), item.appearance)
        << "width=" << item.width;
    EXPECT_EQ(bar->copy_button()->isVisible(), item.copy)
        << "width=" << item.width;
    EXPECT_TRUE(bar->path_label()->isVisible()) << "width=" << item.width;
    delete bar;
  }
}
