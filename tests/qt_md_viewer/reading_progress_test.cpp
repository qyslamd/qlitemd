#include "reading_progress.h"

#include <QApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(ReadingProgressTest, ClampsToUnitRange) {
  ReadingProgressBar bar;
  bar.SetProgress(0.42);
  EXPECT_DOUBLE_EQ(bar.progress(), 0.42);
  bar.SetProgress(-1.0);
  EXPECT_DOUBLE_EQ(bar.progress(), 0.0);
  bar.SetProgress(3.0);
  EXPECT_DOUBLE_EQ(bar.progress(), 1.0);
}
