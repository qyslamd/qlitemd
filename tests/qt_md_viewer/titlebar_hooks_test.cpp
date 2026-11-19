#include <fluenttitlebar.h>

#include <QApplication>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMainWindow>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(TitleBarHooksTest, InsertsLeadingAndTrailingWidgets) {
  QMainWindow window;
  FluentTitleBar bar(&window);
  auto* title = bar.findChild<QLabel*>(QStringLiteral("fluent-title-label"));
  ASSERT_NE(title, nullptr);

  auto* leading = new QWidget(&bar);
  leading->setObjectName(QStringLiteral("leadingProbe"));
  bar.setLeadingWidget(leading);
  EXPECT_EQ(bar.leadingWidget(), leading);

  auto* trailing = new QWidget(&bar);
  trailing->setObjectName(QStringLiteral("trailingProbe"));
  bar.setTrailingWidget(trailing);
  EXPECT_EQ(bar.trailingWidget(), trailing);

  QLayout* layout = bar.layout();
  ASSERT_NE(layout, nullptr);
  EXPECT_LT(layout->indexOf(leading), layout->indexOf(title));
  EXPECT_LT(layout->indexOf(title), layout->indexOf(trailing));
  EXPECT_LT(layout->indexOf(trailing), layout->indexOf(bar.searchLineEdit()));
}

TEST(TitleBarHooksTest, TitleCanBeHidden) {
  QMainWindow window;
  FluentTitleBar bar(&window);
  auto* title = bar.findChild<QLabel*>(QStringLiteral("fluent-title-label"));
  ASSERT_NE(title, nullptr);
  EXPECT_FALSE(title->isHidden());
  bar.setTitleVisible(false);
  EXPECT_TRUE(title->isHidden());
  bar.setTitleVisible(true);
  EXPECT_FALSE(title->isHidden());
}
