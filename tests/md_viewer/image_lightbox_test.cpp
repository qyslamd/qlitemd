#include "image_lightbox.h"

#include <QApplication>
#include <QSignalSpy>
#include <QToolButton>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

QPixmap SamplePixmap() {
  QPixmap pixmap(64, 48);
  pixmap.fill(Qt::red);
  return pixmap;
}

}  // namespace

TEST(ImageLightboxTest, SourceButtonFollowsCapability) {
  ImageLightbox with_source(SamplePixmap(), QStringLiteral("公式预览"), true);
  auto* source = with_source.findChild<QToolButton*>(
      QStringLiteral("imageLightboxSource"));
  ASSERT_NE(source, nullptr);
  EXPECT_FALSE(source->isHidden());

  ImageLightbox without_source(SamplePixmap(), QStringLiteral("图片预览"),
                               false);
  auto* hidden = without_source.findChild<QToolButton*>(
      QStringLiteral("imageLightboxSource"));
  ASSERT_NE(hidden, nullptr);
  EXPECT_TRUE(hidden->isHidden());
}

TEST(ImageLightboxTest, SaveAndSourceEmit) {
  ImageLightbox lightbox(SamplePixmap(), QStringLiteral("公式预览"), true);
  auto* save =
      lightbox.findChild<QToolButton*>(QStringLiteral("imageLightboxSave"));
  auto* source =
      lightbox.findChild<QToolButton*>(QStringLiteral("imageLightboxSource"));
  ASSERT_NE(save, nullptr);
  ASSERT_NE(source, nullptr);

  QSignalSpy save_spy(&lightbox, &ImageLightbox::saveRequested);
  QSignalSpy source_spy(&lightbox, &ImageLightbox::sourceRequested);
  save->click();
  source->click();
  EXPECT_EQ(save_spy.count(), 1);
  EXPECT_EQ(source_spy.count(), 1);
}

TEST(ImageLightboxTest, FitAndActualDoNotCrash) {
  ImageLightbox lightbox(SamplePixmap(), QStringLiteral("图片预览"), false);
  lightbox.resize(640, 480);
  lightbox.FitToWindow();
  lightbox.ActualSize();
  lightbox.FitToWindow();
}
