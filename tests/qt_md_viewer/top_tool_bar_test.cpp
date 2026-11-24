#include "top_tool_bar.h"

#include <QApplication>
#include <QImage>
#include <QStackedWidget>
#include <QWidget>

#include <gtest/gtest.h>

// 顶部工具条是固定横带：不可拖动、不可浮动、高度恒定，且能容纳页栈
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

TEST(TopToolBarTest, PaintsTheOpaqueWindowColor) {
  TopToolBar bar;
  bar.resize(120, 32);
  bar.show();
  QApplication::processEvents();
  // DwmBlur 下窗口色带 alpha，条本身必须填不透明色，否则 Mica 会把
  // 它拉亮，和中心区对不上
  QColor expected = QApplication::palette().color(QPalette::Window);
  expected.setAlpha(255);
  EXPECT_EQ(bar.grab().toImage().pixelColor(60, 16), expected);
}

TEST(TopToolBarTest, HostsStackedBars) {
  TopToolBar bar;
  auto* stack = new QStackedWidget(&bar);
  stack->setObjectName(QStringLiteral("barStack"));
  auto* document_bar = new QWidget(stack);
  auto* welcome_bar = new QWidget(stack);
  stack->addWidget(document_bar);
  stack->addWidget(welcome_bar);
  bar.addWidget(stack);
  bar.resize(800, 32);
  bar.show();
  QApplication::processEvents();

  EXPECT_EQ(stack->parentWidget(), &bar);
  // 工具条内容贴满 32px，切换页不会改变高度
  EXPECT_EQ(stack->height(), 32);
  stack->setCurrentIndex(1);
  EXPECT_EQ(stack->currentWidget(), welcome_bar);
  EXPECT_EQ(bar.height(), 32);
}
