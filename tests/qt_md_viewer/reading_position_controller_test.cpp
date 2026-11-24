#include "reading_position_controller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include "md_viewer_settings.h"

int main(int argc, char** argv) {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("md-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("position-controller-%1")
          .arg(QDateTime::currentMSecsSinceEpoch()));
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(ReadingPositionControllerTest, RememberSavesAndReloads) {
  MdViewerSettings settings;
  ReadingPositionController controller(&settings);
  controller.Load();

  const QString path = QStringLiteral("C:/docs/a.md");
  EXPECT_EQ(controller.ScrollFor(path), -1);
  controller.Remember(path, 120, 1000);
  EXPECT_EQ(controller.ScrollFor(path), 120);
  EXPECT_FALSE(settings.positions_json.isEmpty());

  MdViewerSettings reloaded = settings;
  ReadingPositionController second(&reloaded);
  second.Load();
  EXPECT_EQ(second.ScrollFor(path), 120);
}

TEST(ReadingPositionControllerTest, LaterPositionsWin) {
  MdViewerSettings settings;
  ReadingPositionController controller(&settings);
  controller.Load();

  const QString path = QStringLiteral("C:/docs/a.md");
  controller.Remember(path, 10, 1);
  controller.Remember(path, 40, 2);
  EXPECT_EQ(controller.ScrollFor(path), 40);
  EXPECT_EQ(controller.ScrollFor(QStringLiteral("C:/docs/b.md")), -1);
}
