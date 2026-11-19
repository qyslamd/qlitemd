#include "documents_panel.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <gtest/gtest.h>

#include "bookmarks.h"

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

namespace {

DocumentScanResult SampleRoot() {
  DocumentScanResult result;
  result.root = QStringLiteral("/docs");
  result.root_exists = true;
  DocumentEntry guide;
  guide.absolute_path = result.root + QStringLiteral("/guide");
  guide.relative_path = QStringLiteral("guide");
  guide.is_dir = true;
  DocumentEntry deep = guide;
  deep.absolute_path += QStringLiteral("/deep");
  deep.relative_path = QStringLiteral("guide/deep");
  DocumentEntry a;
  a.absolute_path = result.root + QStringLiteral("/a.md");
  a.relative_path = QStringLiteral("a.md");
  DocumentEntry b = a;
  b.absolute_path = result.root + QStringLiteral("/guide/b.md");
  b.relative_path = QStringLiteral("guide/b.md");
  DocumentEntry c = a;
  c.absolute_path = result.root + QStringLiteral("/guide/deep/c.md");
  c.relative_path = QStringLiteral("guide/deep/c.md");
  result.entries = {guide, deep, a, b, c};
  result.markdown_file_count = 3;
  return result;
}

}  // namespace

TEST(DocumentsPanelTest, BuildsOnlyDocDirectories) {
  DocumentsPanel panel;
  panel.SetRoot(SampleRoot());
  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("documentTree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->topLevelItemCount(), 1);
  QTreeWidgetItem* root = tree->topLevelItem(0);
  EXPECT_TRUE(root->isExpanded());
  ASSERT_EQ(root->childCount(), 2);  // guide, a.md
  EXPECT_EQ(root->child(0)->text(0), QStringLiteral("guide"));
  EXPECT_EQ(root->child(1)->text(0), QStringLiteral("a.md"));
}

TEST(DocumentsPanelTest, ShowsDirectoryCountsAndBookmarkRoles) {
  DocumentsPanel panel;
  panel.SetRoot(SampleRoot());
  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("documentTree"));
  ASSERT_NE(tree, nullptr);
  QTreeWidgetItem* root = tree->topLevelItem(0);
  QTreeWidgetItem* guide = root->child(0);
  QTreeWidgetItem* deep = guide->child(0);
  EXPECT_EQ(root->data(0, kCountRole).toInt(), 3);
  EXPECT_EQ(guide->data(0, kCountRole).toInt(), 2);
  EXPECT_EQ(deep->data(0, kCountRole).toInt(), 1);

  BookmarkStore store;
  store.Toggle(QStringLiteral("/docs/guide/b.md"), QStringLiteral("/docs"));
  panel.SetBookmarks(store.items(), QStringLiteral("/docs"));
  EXPECT_FALSE(root->child(1)->data(0, kBookmarkedRole).toBool());
  EXPECT_TRUE(guide->child(1)->data(0, kBookmarkedRole).toBool());
}

TEST(DocumentsPanelTest, StarClickRequestsBookmarkToggle) {
  DocumentsPanel panel;
  panel.SetRoot(SampleRoot());
  panel.resize(360, 400);
  panel.show();
  QApplication::processEvents();

  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("documentTree"));
  ASSERT_NE(tree, nullptr);
  QTreeWidgetItem* root = tree->topLevelItem(0);
  QTreeWidgetItem* a = root->child(1);
  tree->scrollToItem(a);
  QApplication::processEvents();
  const QRect item_rect = tree->visualItemRect(a);
  ASSERT_TRUE(item_rect.isValid());

  QString toggled;
  QString activated;
  QObject::connect(&panel, &DocumentsPanel::bookmarkToggleRequested,
                   [&toggled](const QString& path) { toggled = path; });
  QObject::connect(&panel, &DocumentsPanel::fileActivated,
                   [&activated](const QString& path) { activated = path; });
  QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                    QPoint(item_rect.right() - 10, item_rect.center().y()));
  EXPECT_EQ(toggled, QStringLiteral("/docs/a.md"));
  // 星标点击不能顺带打开文档
  EXPECT_TRUE(activated.isEmpty());
}

TEST(DocumentsPanelTest, FilterHidesNonMatchingNodes) {
  DocumentsPanel panel;
  panel.SetRoot(SampleRoot());
  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("documentTree"));
  auto* filter =
      panel.findChild<QLineEdit*>(QStringLiteral("documentFilterEdit"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(filter, nullptr);
  QTreeWidgetItem* root = tree->topLevelItem(0);
  QTreeWidgetItem* guide = root->child(0);
  QTreeWidgetItem* a = root->child(1);
  QTreeWidgetItem* deep = guide->child(0);  // 目录先于文件
  QTreeWidgetItem* b = guide->child(1);

  filter->setText(QStringLiteral("B."));
  EXPECT_FALSE(root->isHidden());
  EXPECT_FALSE(guide->isHidden());
  EXPECT_FALSE(b->isHidden());
  EXPECT_TRUE(a->isHidden());
  EXPECT_TRUE(deep->isHidden());

  filter->clear();
  EXPECT_FALSE(a->isHidden());
  EXPECT_FALSE(deep->isHidden());
}

TEST(DocumentsPanelTest, PreservesExpansionAcrossRebuild) {
  DocumentsPanel panel;
  panel.SetRoot(SampleRoot());
  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("documentTree"));
  QTreeWidgetItem* root = tree->topLevelItem(0);
  QTreeWidgetItem* guide = root->child(0);
  guide->setExpanded(true);
  panel.SetRoot(SampleRoot());
  root = tree->topLevelItem(0);
  guide = root->child(0);
  EXPECT_TRUE(guide->isExpanded());
}

TEST(DocumentsPanelTest, ShowsEmptyStateWhenRootMissing) {
  DocumentsPanel panel;
  panel.SetRootMissing(QStringLiteral("/gone"),
                       QStringLiteral("目录不存在或不可读"));
  auto* stack =
      panel.findChild<QStackedWidget*>(QStringLiteral("documentStack"));
  auto* empty = panel.findChild<QWidget*>(QStringLiteral("sidebarEmptyPage"));
  ASSERT_NE(stack, nullptr);
  ASSERT_NE(empty, nullptr);
  EXPECT_EQ(stack->currentWidget(), empty);
}

TEST(DocumentsPanelTest, MarksMissingBookmarks) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString existing = temp.path() + QStringLiteral("/a.md");
  QFile file(existing);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("x");
  file.close();

  DocumentsPanel panel;
  BookmarkStore store;
  store.Toggle(existing, temp.path());
  store.Toggle(temp.path() + QStringLiteral("/missing.md"), temp.path());
  panel.SetBookmarks(store.items(), temp.path());

  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("bookmarkTree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->topLevelItemCount(), 2);
  EXPECT_TRUE(tree->topLevelItem(0)->data(0, kMissingRole).toBool());
  EXPECT_FALSE(tree->topLevelItem(1)->data(0, kMissingRole).toBool());
  EXPECT_EQ(tree->topLevelItem(1)->text(0), QStringLiteral("a.md"));
  EXPECT_EQ(tree->topLevelItem(1)->data(0, kBookmarkDisplayPathRole).toString(),
            QStringLiteral("a.md"));
}

TEST(DocumentsPanelTest, ContextMenuSurvivesBookmarkRefresh) {
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString existing = temp.path() + QStringLiteral("/a.md");
  QFile file(existing);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("x");
  file.close();

  DocumentsPanel panel;
  BookmarkStore store;
  store.Toggle(existing, temp.path());
  panel.SetBookmarks(store.items(), temp.path());
  panel.ShowTab(DocumentsPanel::Tab::kBookmarks);
  panel.resize(420, 360);
  panel.show();
  QApplication::processEvents();

  auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("bookmarkTree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->topLevelItemCount(), 1);
  QTreeWidgetItem* item = tree->topLevelItem(0);
  tree->scrollToItem(item);
  QApplication::processEvents();
  const QRect item_rect = tree->visualItemRect(item);
  ASSERT_TRUE(item_rect.isValid());

  QString removed;
  QObject::connect(&panel, &DocumentsPanel::bookmarkRemoveRequested,
                   [&removed](const QString& path) { removed = path; });
  // 菜单 exec 的嵌套事件循环里触发一次书签刷新；不修的话 item 会被删，
  // 菜单返回后再读 item 就是 use-after-free
  QTimer::singleShot(50, [&panel, &removed, existing, temp_path = temp.path()] {
    auto* menu = panel.findChild<QMenu*>();
    if (menu == nullptr || menu->actions().isEmpty()) {
      return;
    }
    panel.SetBookmarks({}, temp_path);
    QTest::keyClick(menu, Qt::Key_Down);
    QTest::keyClick(menu, Qt::Key_Return);
    menu->close();
  });
  QMetaObject::invokeMethod(tree, "customContextMenuRequested",
                            Qt::DirectConnection,
                            Q_ARG(QPoint, item_rect.center()));
  EXPECT_EQ(removed, existing);
}

TEST(DocumentsPanelTest, GroupsSearchHitsByFile) {
  DocumentsPanel panel;
  panel.BeginSearch(QStringLiteral("render"));
  SearchHit a0{QStringLiteral("/docs/a.md"),
               QStringLiteral("a.md"),
               3,
               QStringLiteral("x render y"),
               2,
               0};
  SearchHit a1{QStringLiteral("/docs/a.md"),
               QStringLiteral("a.md"),
               9,
               QStringLiteral("z render"),
               2,
               1};
  SearchHit b0{QStringLiteral("/docs/guide/b.md"),
               QStringLiteral("guide/b.md"),
               1,
               QStringLiteral("render"),
               0,
               0};
  panel.AppendSearchHits({a0, a1, b0}, 2, 10);

  auto* tree =
      panel.findChild<QTreeWidget*>(QStringLiteral("searchResultTree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->topLevelItemCount(), 2);
  ASSERT_EQ(tree->topLevelItem(0)->childCount(), 2);
  EXPECT_EQ(tree->topLevelItem(0)
                ->child(1)
                ->data(0, kSearchHitRole)
                .value<SearchHit>()
                .file_ordinal,
            1);
  panel.EndSearch(true, false);
  auto* summary =
      panel.findChild<QLabel*>(QStringLiteral("searchSummaryLabel"));
  ASSERT_NE(summary, nullptr);
  EXPECT_TRUE(summary->text().contains(QStringLiteral("已截断")));
}
