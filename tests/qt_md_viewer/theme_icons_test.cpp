#include "theme_icons.h"

#include <QApplication>
#include <QIcon>

#include <gtest/gtest.h>

// 两套主题图标靠 qrc 里的 resources/svg/light|dark 路径生效；漏登记一个
// 条目，QIcon 就是 null，按钮上静默空白。这里把名单钉住。
namespace {

constexpr const char* kThemeIcons[] = {
    "appearance", "back",         "file",    "folder",   "forward",    "menu",
    "search",     "star-outline", "zoom-in", "zoom-out", "zoom-reset",
};

}  // namespace

TEST(ThemeIconsTest, LightAndDarkVariantsExist) {
  for (const char* name : kThemeIcons) {
    EXPECT_FALSE(ThemeIcon(QString::fromLatin1(name), false).isNull()) << name;
    EXPECT_FALSE(ThemeIcon(QString::fromLatin1(name), true).isNull()) << name;
  }
}

TEST(ThemeIconsTest, SemanticIconsStayAtRoot) {
  // 实心星（金色）、状态点（绿色）、应用图标：不随主题换色，从 svg/ 根加载
  for (const char* name : {"star-filled", "status-dot", "icon"}) {
    const QIcon icon(QStringLiteral(":/resources/svg/") +
                     QString::fromLatin1(name) + QStringLiteral(".svg"));
    EXPECT_FALSE(icon.isNull()) << name;
  }
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
