#include "side_panel.h"

#include <QFont>
#include <QLabel>
#include <QSet>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "bookmarks_page.h"
#include "documents_page.h"
#include "search_page.h"

SidePanel::SidePanel(QWidget* parent) : QWidget(parent) {
  initUi();
  initSignals();
  // 默认没有工作区：搜索输入禁用，等 SetRoot 成功后由窗口打开
  SetSearchAvailable(false);
}

void SidePanel::initUi() {
  setObjectName(QStringLiteral("sidePanel"));
  setMinimumWidth(200);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  title_ = new QLabel(SideViewTitle(SideView::kDocuments), this);
  title_->setObjectName(QStringLiteral("sidePanelTitle"));
  // 与右列标题栏（42px）同行对齐
  title_->setFixedHeight(42);
  title_->setContentsMargins(12, 0, 10, 0);
  title_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  QFont title_font = title_->font();
  title_font.setBold(true);
  title_->setFont(title_font);
  layout->addWidget(title_);

  stack_ = new QStackedWidget(this);
  stack_->setObjectName(QStringLiteral("sideStack"));

  // 页序必须与 SideView 数值一致：ShowView 直接拿枚举当下标
  documents_ = new DocumentsPage(stack_);
  search_ = new SearchPage(stack_);
  bookmarks_ = new BookmarksPage(stack_);
  stack_->addWidget(documents_);
  stack_->addWidget(search_);
  stack_->addWidget(bookmarks_);

  render_status_host_ = new QWidget(stack_);
  auto* status_layout = new QVBoxLayout(render_status_host_);
  status_layout->setContentsMargins(8, 8, 8, 8);
  status_layout->setSpacing(6);
  stack_->addWidget(render_status_host_);

  layout->addWidget(stack_, 1);
}

void SidePanel::initSignals() {
  connect(documents_, &DocumentsPage::fileActivated, this,
          &SidePanel::fileActivated);
  connect(documents_, &DocumentsPage::bookmarkToggleRequested, this,
          &SidePanel::bookmarkToggleRequested);
  connect(documents_, &DocumentsPage::chooseRootRequested, this,
          &SidePanel::chooseRootRequested);
  connect(bookmarks_, &BookmarksPage::bookmarkActivated, this,
          &SidePanel::bookmarkActivated);
  connect(bookmarks_, &BookmarksPage::bookmarkRemoveRequested, this,
          &SidePanel::bookmarkRemoveRequested);
  connect(search_, &SearchPage::searchTextChanged, this,
          &SidePanel::searchTextChanged);
  connect(search_, &SearchPage::searchSubmitted, this,
          &SidePanel::searchSubmitted);
  connect(search_, &SearchPage::searchHitActivated, this,
          &SidePanel::searchHitActivated);
  connect(search_, &SearchPage::cancelSearchRequested, this,
          &SidePanel::cancelSearchRequested);
}

void SidePanel::SetRenderStatusWidget(QWidget* widget) {
  if (widget == nullptr) {
    return;
  }
  render_status_host_->layout()->addWidget(widget);
}

void SidePanel::SetRoot(const DocumentScanResult& result) {
  documents_->SetRoot(result);
}

void SidePanel::SetRootMissing(const QString& root, const QString& reason) {
  documents_->SetRootMissing(root, reason);
}

void SidePanel::SetFilter(const QString& text) { documents_->SetFilter(text); }

void SidePanel::SetSearchAvailable(bool available) {
  search_->SetSearchAvailable(available);
}

QLineEdit* SidePanel::search_edit() const { return search_->search_edit(); }

void SidePanel::SetCurrentFile(const QString& absolute_path) {
  documents_->SetCurrentFile(absolute_path);
}

void SidePanel::SetBookmarks(const QVector<Bookmark>& bookmarks,
                             const QString& current_root) {
  QSet<QString> paths;
  for (const Bookmark& bookmark : bookmarks) {
    paths.insert(bookmark.path);
  }
  documents_->SetBookmarkedPaths(paths);
  bookmarks_->SetBookmarks(bookmarks, current_root);
}

void SidePanel::BeginSearch(const QString& query) {
  search_->BeginSearch(query);
}

void SidePanel::AppendSearchHits(const QVector<SearchHit>& hits,
                                 int scanned_files, int total_files) {
  search_->AppendSearchHits(hits, scanned_files, total_files);
}

void SidePanel::EndSearch(bool truncated, bool canceled) {
  search_->EndSearch(truncated, canceled);
}

void SidePanel::SetSearchError(const QString& message) {
  search_->SetSearchError(message);
}

void SidePanel::ShowView(SideView view) {
  const int index = static_cast<int>(view);
  if (!IsValidSideView(index)) {
    return;
  }
  stack_->setCurrentIndex(index);
  title_->setText(SideViewTitle(view));
}

SideView SidePanel::currentView() const {
  const int index = stack_->currentIndex();
  return IsValidSideView(index) ? static_cast<SideView>(index)
                                : SideView::kDocuments;
}

void SidePanel::SetThemeDark(bool dark) {
  documents_->SetThemeDark(dark);
  bookmarks_->SetThemeDark(dark);
}
