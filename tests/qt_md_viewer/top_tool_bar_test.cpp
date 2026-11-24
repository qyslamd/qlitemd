#include "top_tool_bar.h"

#include <QApplication>

#include <gtest/gtest.h>

// 顶部工具条是固定横带：不可拖动、不可浮动、高度恒定
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(TopToolBarTest, IsAFixedStrip) {
  TopToolBar bar;
  EXPECT_EQ(bar.objectName(), QStringLiteral("topToolBar"));
  EXPECT_FALSE(bar.isMovable());
  EXPECT_FALSE(bar.isFloatable());
  EXPECT_EQ(bar.minimumHeight(), 32);
  EXPECT_EQ(bar.maximumHeight(), 32);
  EXPECT_EQ(bar.contextMenuPolicy(), Qt::PreventContextMenu);
  EXPECT_EQ(bar.allowedAreas(), Qt::ToolBarAreas(Qt::TopToolBarArea));
}

TEST(TopToolBarTest, ResetInvariantsSurvivesAStateRestore) {
  TopToolBar bar;
  // restoreState 会把这些改回去，ResetInvariants 负责压回来
  bar.setMovable(true);
  bar.setFloatable(true);
  bar.setFixedHeight(50);
  bar.hide();

  bar.ResetInvariants();

  EXPECT_FALSE(bar.isMovable());
  EXPECT_FALSE(bar.isFloatable());
  EXPECT_EQ(bar.minimumHeight(), 32);
  EXPECT_EQ(bar.maximumHeight(), 32);
  EXPECT_FALSE(bar.isHidden());
}
