#include <QApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

#include <gtest/gtest.h>

#include "top_tool_bar.h"

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

TEST(DocumentBarTest, ExposesControlAnchors) {
  DocumentBar bar;
  // objectName 是 GUI 工具和测试共用的锚点；行为由后续用例覆盖
  for (const char* name : {"docBack", "docForward", "docStar", "docPathLabel",
                           "docCopyPath", "docFindEdit", "docAppearance",
                           "docZoomOut", "docZoomReset", "docZoomIn"}) {
    EXPECT_NE(bar.findChild<QWidget*>(QString::fromLatin1(name)), nullptr)
        << name;
  }
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
