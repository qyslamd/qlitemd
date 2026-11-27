#include <QApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
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
  for (const char* name :
       {"docBack", "docForward", "docStar", "docPathLabel", "docCopyPath",
        "docFindEdit", "docFindCount", "docFindPrev", "docFindNext",
        "docOutlineToggle", "docAppearance", "docZoomOut", "docZoomReset",
        "docZoomIn", "docExport", "docExportMenu"}) {
    EXPECT_NE(bar.findChild<QWidget*>(QString::fromLatin1(name)), nullptr)
        << name;
  }
}

TEST(DocumentBarTest, KeepsToolbarRhythm) {
  DocumentBar* bar = MakeShownBar(1000);
  // 40px 的行高给图标和输入框留出呼吸空间；复制后、查找后和导出前各有
  // 一条分隔线
  EXPECT_EQ(bar->height(), 40);
  EXPECT_NE(bar->findChild<QWidget*>(QStringLiteral("docZoomGroup")), nullptr);
  EXPECT_EQ(
      bar->findChildren<QWidget*>(QStringLiteral("toolbarSeparator")).size(),
      3);
  delete bar;
}

TEST(DocumentBarTest, PathIsPlainTextAfterDocumentActions) {
  DocumentBar* bar = MakeShownBar(1000);
  bar->SetPath(QStringLiteral("guide/intro.md"),
               QStringLiteral("D:/docs/guide/intro.md"));
  QApplication::processEvents();

  // 路径不再套地址栏式的圆角层
  EXPECT_EQ(bar->findChild<QWidget*>(QStringLiteral("docPathLayer")), nullptr);
  EXPECT_EQ(bar->star_button()->parentWidget(), bar);
  EXPECT_EQ(bar->copy_button()->parentWidget(), bar);
  EXPECT_EQ(bar->path_label()->parentWidget(), bar);
  EXPECT_LT(bar->star_button()->x(), bar->copy_button()->x());
  EXPECT_LT(bar->copy_button()->x(), bar->path_label()->x());
  EXPECT_TRUE(bar->path_label()->text().startsWith(QStringLiteral("guide/")));
  delete bar;
}

TEST(DocumentBarTest, ReelidesPathAfterLayout) {
  // 路径标签在 layer 里，SetPath 时宽度还是 0；布局落定后要重新省略一次，
  // 否则宽窗下也会停留在 200px 的省略结果
  DocumentBar bar;
  const QString path = QStringLiteral(
      "docs/guide/一个宽度超过两百像素但远小于整窗的文档标题.md");
  bar.SetPath(path, path);
  bar.resize(1200, 40);
  bar.show();
  QApplication::processEvents();
  EXPECT_EQ(bar.path_label()->text(), path);
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

TEST(DocumentBarTest, FindControlsUseFlatToolbarLayout) {
  DocumentBar* bar = MakeShownBar(700);
  // 查找不再套 ToolbarLayer，否则会把工具栏撑高、输入框显示不全
  EXPECT_EQ(bar->findChild<QWidget*>(QStringLiteral("docFindLayer")), nullptr);
  EXPECT_EQ(bar->find_edit()->parentWidget(), bar);
  EXPECT_EQ(bar->find_count_label()->parentWidget(), bar);
  EXPECT_EQ(bar->find_prev_button()->parentWidget(), bar);
  EXPECT_EQ(bar->find_next_button()->parentWidget(), bar);
  EXPECT_EQ(bar->find_prev_button()->focusPolicy(), Qt::NoFocus);
  EXPECT_EQ(bar->find_next_button()->focusPolicy(), Qt::NoFocus);
  EXPECT_TRUE(
      bar->find_prev_button()->toolTip().contains(QStringLiteral("Previous")));
  EXPECT_TRUE(
      bar->find_next_button()->toolTip().contains(QStringLiteral("Next")));
  delete bar;
}

TEST(DocumentBarTest, FindNavigationFollowsQueryAndDocumentState) {
  DocumentBar* bar = MakeShownBar(700);
  ASSERT_TRUE(bar->find_prev_button()->isEnabled() ||
              !bar->find_prev_button()->isEnabled());
  EXPECT_FALSE(bar->find_prev_button()->isEnabled());
  EXPECT_FALSE(bar->find_next_button()->isEnabled());

  bar->find_edit()->setText(QStringLiteral("alpha"));
  EXPECT_FALSE(bar->find_prev_button()->isEnabled());
  bar->SetFindNavigationEnabled(true);
  EXPECT_TRUE(bar->find_prev_button()->isEnabled());
  EXPECT_TRUE(bar->find_next_button()->isEnabled());

  QSignalSpy previous(bar, &DocumentBar::FindPreviousRequested);
  QSignalSpy next(bar, &DocumentBar::FindNextRequested);
  bar->find_prev_button()->click();
  bar->find_next_button()->click();
  EXPECT_EQ(previous.count(), 1);
  EXPECT_EQ(next.count(), 1);

  bar->find_edit()->clear();
  EXPECT_FALSE(bar->find_prev_button()->isEnabled());
  EXPECT_FALSE(bar->find_next_button()->isEnabled());

  bar->find_edit()->setText(QStringLiteral("alpha"));
  bar->SetFindNavigationEnabled(false);
  EXPECT_FALSE(bar->find_prev_button()->isEnabled());
  EXPECT_FALSE(bar->find_next_button()->isEnabled());
  delete bar;
}

TEST(DocumentBarTest, FindControlsStayUsableWhenFindBoxShrinks) {
  // 主窗最小宽度是 600，这里覆盖应用能到达的最窄档
  DocumentBar* bar = MakeShownBar(600);
  EXPECT_TRUE(bar->find_edit()->isVisible());
  EXPECT_EQ(bar->find_edit()->width(), 160);
  EXPECT_TRUE(bar->find_prev_button()->isVisible());
  EXPECT_TRUE(bar->find_next_button()->isVisible());

  bar->SetFindCount(2, 7);
  EXPECT_TRUE(bar->find_count_label()->isVisible());
  EXPECT_EQ(bar->find_count_label()->text(), QStringLiteral("3 / 7"));
  delete bar;
}

TEST(DocumentBarTest, FindsCountLabelShowsAndClears) {
  DocumentBar* bar = MakeShownBar(700);
  QLabel* label = bar->find_count_label();
  ASSERT_NE(label, nullptr);
  EXPECT_FALSE(label->isVisible());

  bar->SetFindCount(2, 7);
  EXPECT_EQ(label->text(), QStringLiteral("3 / 7"));
  EXPECT_TRUE(label->isVisible());

  bar->SetFindCount(-1, 0);
  EXPECT_EQ(label->text(), QStringLiteral("0 / 0"));

  bar->ClearFindCount();
  EXPECT_TRUE(label->text().isEmpty());
  EXPECT_FALSE(label->isVisible());
  delete bar;
}

TEST(DocumentBarTest, HidesGroupsAsWidthShrinks) {
  struct Case {
    int width;
    bool zoom;
    bool search;
    bool appearance;
    bool copy;
    bool export_menu;
  };
  // 阈值按整窗宽度定：导出 900、缩放 860、外观 720、复制 620；搜索始终可见
  const Case cases[] = {
      {1000, true, true, true, true, true},
      {900, true, true, true, true, true},
      {860, true, true, true, true, false},
      {800, false, true, true, true, false},
      {720, false, true, true, true, false},
      {700, false, true, false, true, false},
      {620, false, true, false, true, false},
      {600, false, true, false, false, false},
      {400, false, true, false, false, false},
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
    EXPECT_EQ(bar->export_button()->isVisible(), item.export_menu)
        << "width=" << item.width;
    EXPECT_TRUE(bar->path_label()->isVisible()) << "width=" << item.width;
    // 大纲开关是主控入口，和查找框一样任何宽度都保留
    EXPECT_TRUE(bar->outline_button()->isVisible()) << "width=" << item.width;
    delete bar;
  }
}
