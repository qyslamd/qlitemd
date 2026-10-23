#include "reader_appearance.h"

#include <QAbstractButton>
#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(ReaderAppearanceCssTest, DefaultProducesNoOverrides) {
  EXPECT_TRUE(ReaderAppearanceCss(ReaderPreferences(), true).isEmpty());
  EXPECT_TRUE(ReaderAppearanceCss(ReaderPreferences(), false).isEmpty());
}

TEST(ReaderAppearanceCssTest, TypographyIsClamped) {
  ReaderPreferences preferences;
  preferences.font_size = 99;
  preferences.line_height_percent = 300;
  const QString css = ReaderAppearanceCss(preferences, true);
  EXPECT_TRUE(css.contains(QStringLiteral("font-size: 24px")));
  EXPECT_TRUE(css.contains(QStringLiteral("line-height: 2.40")));
}

TEST(ReaderAppearanceCssTest, PrototypeMeasureLandsOnTextBlocks) {
  ReaderPreferences preferences;
  preferences.measure_px = 900;
  const QString css = ReaderAppearanceCss(preferences, true);
  EXPECT_TRUE(css.contains(QStringLiteral("blockquote")));
  EXPECT_TRUE(css.contains(QStringLiteral("max-width: 900px")));
  EXPECT_FALSE(css.contains(QStringLiteral(".md-page")));
}

TEST(ReaderAppearanceCssTest, LibraryMeasureLandsOnPage) {
  ReaderPreferences preferences;
  preferences.measure_px = 680;
  const QString css = ReaderAppearanceCss(preferences, false);
  EXPECT_TRUE(css.contains(QStringLiteral(".md-page { max-width: 680px; }")));
  EXPECT_FALSE(css.contains(QStringLiteral("blockquote")));
}

TEST(ReaderAppearanceCssTest, FullWidthIsNone) {
  ReaderPreferences preferences;
  preferences.measure_px = 0;
  EXPECT_TRUE(ReaderAppearanceCss(preferences, true)
                  .contains(QStringLiteral("max-width: none")));
  EXPECT_TRUE(ReaderAppearanceCss(preferences, false)
                  .contains(QStringLiteral(".md-page { max-width: none; }")));
}

TEST(ReaderAppearancePopupTest, ChoiceAndResetEmitPreferences) {
  qRegisterMetaType<ReaderPreferences>();
  ReaderAppearancePopup popup;
  auto* font_large =
      popup.findChild<QAbstractButton*>(QStringLiteral("appearanceFont17"));
  auto* reset =
      popup.findChild<QPushButton*>(QStringLiteral("readerAppearanceReset"));
  ASSERT_NE(font_large, nullptr);
  ASSERT_NE(reset, nullptr);

  QSignalSpy spy(&popup, &ReaderAppearancePopup::preferencesChanged);
  font_large->click();
  ASSERT_EQ(spy.count(), 1);
  ReaderPreferences preferences =
      spy.takeFirst().at(0).value<ReaderPreferences>();
  EXPECT_EQ(preferences.font_size, 17);
  EXPECT_TRUE(font_large->isChecked());

  reset->click();
  ASSERT_EQ(spy.count(), 1);
  preferences = spy.takeFirst().at(0).value<ReaderPreferences>();
  EXPECT_TRUE(preferences.IsDefault());
  EXPECT_FALSE(font_large->isChecked());
}
