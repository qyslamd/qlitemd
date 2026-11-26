#include "search_page.h"

#include <QAbstractItemView>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include "side_panel_delegates.h"
#include "side_panel_roles.h"

SearchPage::SearchPage(QWidget* parent) : QWidget(parent) {
  initUi();
  initSignals();
}

void SearchPage::initUi() {
  setObjectName(QStringLiteral("searchPage"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 8, 4, 8);
  layout->setSpacing(6);
  search_edit_ = new QLineEdit(this);
  search_edit_->setObjectName(QStringLiteral("workspaceSearchEdit"));
  search_edit_->setPlaceholderText(tr("Search workspace files and content"));
  search_edit_->setClearButtonEnabled(true);
  search_edit_->setEnabled(false);
  layout->addWidget(search_edit_);

  // 搜索状态与取消放在同一行：进度/命中数属于这次查询，不该占面板底部
  auto* header = new QWidget(this);
  header->setObjectName(QStringLiteral("searchHeader"));
  auto* header_layout = new QHBoxLayout(header);
  header_layout->setContentsMargins(0, 0, 0, 0);
  header_layout->setSpacing(6);
  search_summary_ = new QLabel(header);
  search_summary_->setObjectName(QStringLiteral("searchSummaryLabel"));
  search_summary_->setWordWrap(true);
  search_summary_->setText(tr("Type keywords to start searching"));
  header_layout->addWidget(search_summary_, 1);
  cancel_button_ = new QPushButton(tr("Cancel"), header);
  cancel_button_->setObjectName(QStringLiteral("sidebarCancelButton"));
  cancel_button_->hide();
  header_layout->addWidget(cancel_button_);
  layout->addWidget(header);

  search_tree_ = new QTreeWidget(this);
  search_tree_->setObjectName(QStringLiteral("searchResultTree"));
  search_tree_->setColumnCount(1);
  search_tree_->setHeaderHidden(true);
  search_tree_->setRootIsDecorated(true);
  search_tree_->setIndentation(12);
  search_tree_->setAlternatingRowColors(false);
  search_tree_->setUniformRowHeights(true);
  search_tree_->setMouseTracking(true);
  search_tree_->viewport()->setMouseTracking(true);
  search_tree_->setItemDelegate(new SearchResultDelegate(search_tree_));
  layout->addWidget(search_tree_, 1);

  search_empty_ = new QLabel(this);
  search_empty_->setObjectName(QStringLiteral("searchEmptyLabel"));
  search_empty_->setWordWrap(true);
  search_empty_->setAlignment(Qt::AlignCenter);
  search_empty_->hide();
  layout->addWidget(search_empty_);
}

void SearchPage::initSignals() {
  connect(search_edit_, &QLineEdit::textChanged, this,
          &SearchPage::searchTextChanged);
  connect(search_edit_, &QLineEdit::returnPressed, this,
          &SearchPage::searchSubmitted);
  connect(cancel_button_, &QPushButton::clicked, this,
          &SearchPage::cancelSearchRequested);
  connect(search_tree_, &QTreeWidget::itemClicked, this,
          [this](QTreeWidgetItem* item, int) {
            if (item->parent() == nullptr) {
              item->setExpanded(!item->isExpanded());
              return;
            }
            emit searchHitActivated(
                item->data(0, kSearchHitRole).value<SearchHit>());
          });
}

void SearchPage::SetSearchAvailable(bool available) {
  search_edit_->setEnabled(available);
  search_edit_->setPlaceholderText(
      available ? tr("Search workspace files and content")
                : tr("Open a folder first"));
  if (!available) {
    search_edit_->clear();
  }
}

void SearchPage::BeginSearch(const QString& query) {
  current_query_ = query;
  search_groups_.clear();
  search_group_counts_.clear();
  search_tree_->clear();
  search_total_hits_ = 0;
  search_total_files_ = 0;
  search_scanned_files_ = 0;
  search_empty_->hide();
  if (query.isEmpty()) {
    search_summary_->setText(tr("Type keywords to start searching"));
    cancel_button_->hide();
  } else {
    search_summary_->setText(tr("Searching…"));
    cancel_button_->show();
  }
}

void SearchPage::AppendSearchHits(const QVector<SearchHit>& hits,
                                  int scanned_files, int total_files) {
  search_scanned_files_ = scanned_files;
  search_total_files_ = total_files;
  for (const SearchHit& hit : hits) {
    QTreeWidgetItem* group = search_groups_.value(hit.file_path);
    if (group == nullptr) {
      group = new QTreeWidgetItem(search_tree_);
      group->setData(0, kPathRole, hit.file_path);
      group->setExpanded(true);
      QFont font = group->font(0);
      font.setBold(true);
      group->setFont(0, font);
      search_groups_.insert(hit.file_path, group);
    }
    const int count = ++search_group_counts_[hit.file_path];
    group->setText(0,
                   QStringLiteral("%1 (%2)").arg(hit.relative_path).arg(count));
    auto* child = new QTreeWidgetItem(group);
    child->setData(0, kSearchHitRole, QVariant::fromValue(hit));
    child->setData(0, kQueryLengthRole, current_query_.size());
    ++search_total_hits_;
  }
  search_summary_->setText(tr("Searching… · scanned %1/%2 · %3 hits")
                               .arg(search_scanned_files_)
                               .arg(search_total_files_)
                               .arg(search_total_hits_));
}

void SearchPage::EndSearch(bool truncated, bool canceled) {
  cancel_button_->hide();
  QString notice;
  if (canceled) {
    notice = tr(" · canceled (partial results)");
  } else if (truncated) {
    notice = tr(" · truncated");
  }
  search_summary_->setText(tr("Searched %1 documents for “%2” · %3 hits%4")
                               .arg(search_total_files_)
                               .arg(current_query_)
                               .arg(search_total_hits_)
                               .arg(notice));
  if (search_total_hits_ == 0) {
    search_empty_->setText(canceled ? tr("Search canceled")
                                    : tr("No matches found"));
    search_empty_->show();
  }
}

void SearchPage::SetSearchError(const QString& message) {
  cancel_button_->hide();
  search_summary_->setText(message);
  search_empty_->setText(message);
  search_empty_->show();
}
