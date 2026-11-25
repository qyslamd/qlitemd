#include "welcome_view.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "document_index.h"
#include "theme_icons.h"

namespace {

constexpr int kRecentItemHeight = 46;

}  // namespace

// 不进匿名命名空间：WelcomeView 存它的指针，主题切换时调 set_dark
class RecentItemDelegate final : public QStyledItemDelegate {
 public:
  explicit RecentItemDelegate(QObject* parent = nullptr)
      : QStyledItemDelegate(parent) {}

  void set_dark(bool dark) { dark_ = dark; }

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override {
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(0, kRecentItemHeight);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const bool selected = option.state & QStyle::State_Selected;
    const QRect row = option.rect.adjusted(2, 1, -2, -1);
    if (selected) {
      QColor background = option.palette.color(QPalette::Highlight);
      background.setAlpha(30);
      painter->setPen(Qt::NoPen);
      painter->setBrush(background);
      painter->drawRoundedRect(row, 8, 8);
    }

    const QPixmap icon =
        ThemeIcon(QStringLiteral("file"), dark_).pixmap(16, 16);
    if (!icon.isNull()) {
      painter->drawPixmap(row.left() + 10, row.center().y() - icon.height() / 2,
                          icon);
    }

    const int text_left = row.left() + 38;
    const int text_width = qMax(20, row.right() - text_left - 10);
    QFont title_font = option.font;
    title_font.setWeight(QFont::DemiBold);
    painter->setFont(title_font);
    painter->setPen(option.palette.color(QPalette::Text));
    const QFontMetrics title_metrics(title_font);
    painter->drawText(
        QRect(text_left, row.top() + 5, text_width, 20),
        Qt::AlignLeft | Qt::AlignVCenter,
        title_metrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                 Qt::ElideMiddle, text_width));

    QFont subtitle_font = option.font;
    if (subtitle_font.pointSizeF() > 0.0) {
      subtitle_font.setPointSizeF(qMax(8.0, subtitle_font.pointSizeF() - 1.5));
    }
    painter->setFont(subtitle_font);
    painter->setPen(option.palette.color(QPalette::PlaceholderText));
    const QFontMetrics subtitle_metrics(subtitle_font);
    painter->drawText(
        QRect(text_left, row.top() + 24, text_width, 17),
        Qt::AlignLeft | Qt::AlignVCenter,
        subtitle_metrics.elidedText(index.data(Qt::UserRole + 1).toString(),
                                    Qt::ElideMiddle, text_width));
    painter->restore();
  }

 private:
  bool dark_ = false;
};

WelcomeView::WelcomeView(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("welcomeView"));

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(48, 40, 48, 40);
  outer->addStretch(1);

  auto* content = new QWidget(this);
  content->setObjectName(QStringLiteral("welcomeContent"));
  // 窄窗下欢迎页要能跟着收，最大宽度仍然限制在大屏上的可读行宽
  content->setMinimumWidth(320);
  content->setMaximumWidth(560);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* icon = new QLabel(content);
  icon->setObjectName(QStringLiteral("welcomeIcon"));
  const QIcon app_icon(QStringLiteral(":/resources/svg/icon.svg"));
  if (!app_icon.isNull()) {
    icon->setPixmap(app_icon.pixmap(56, 56));
  }
  icon->setFixedSize(56, 56);
  layout->addWidget(icon);
  layout->addSpacing(18);

  auto* title = new QLabel(tr("Markdown Reader"), content);
  title->setObjectName(QStringLiteral("welcomeTitle"));
  QFont title_font = font();
  if (title_font.pointSizeF() > 0.0) {
    title_font.setPointSizeF(title_font.pointSizeF() + 10.0);
  }
  title_font.setWeight(QFont::DemiBold);
  title->setFont(title_font);
  layout->addWidget(title);
  layout->addSpacing(6);

  auto* subtitle = new QLabel(
      tr("Focused reading with native rendering, no browser engine required."),
      content);
  subtitle->setObjectName(QStringLiteral("welcomeSubtitle"));
  subtitle->setForegroundRole(QPalette::PlaceholderText);
  QFont subtitle_font = font();
  if (subtitle_font.pointSizeF() > 0.0) {
    subtitle_font.setPointSizeF(subtitle_font.pointSizeF() + 1.5);
  }
  subtitle->setFont(subtitle_font);
  layout->addWidget(subtitle);
  layout->addSpacing(24);

  auto* actions = new QHBoxLayout;
  actions->setContentsMargins(0, 0, 0, 0);
  actions->setSpacing(10);
  auto* open_dir = new QPushButton(tr("Open Folder"), content);
  open_dir->setObjectName(QStringLiteral("welcomeOpenDirectory"));
  open_dir->setDefault(true);
  open_dir->setMinimumHeight(34);
  open_dir->setMinimumWidth(116);
  auto* open_file = new QPushButton(tr("Open File…"), content);
  open_file->setObjectName(QStringLiteral("welcomeOpenFile"));
  open_file->setMinimumHeight(34);
  open_file->setMinimumWidth(116);
  auto* quick_open = new QPushButton(tr("Quick Open…"), content);
  quick_open->setObjectName(QStringLiteral("welcomeQuickOpen"));
  quick_open->setFlat(true);
  quick_open->setMinimumHeight(34);
  actions->addWidget(open_dir);
  actions->addWidget(open_file);
  actions->addWidget(quick_open);
  actions->addStretch(1);
  layout->addLayout(actions);

  layout->addSpacing(30);
  recent_title_ = new QLabel(tr("Recent Files"), content);
  recent_title_->setObjectName(QStringLiteral("welcomeRecentTitle"));
  QFont section_font = font();
  section_font.setWeight(QFont::DemiBold);
  recent_title_->setFont(section_font);
  layout->addWidget(recent_title_);
  layout->addSpacing(6);

  recent_list_ = new QListWidget(content);
  recent_list_->setObjectName(QStringLiteral("welcomeRecentList"));
  recent_list_->setFrameShape(QFrame::NoFrame);
  recent_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  recent_list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  recent_list_->setUniformItemSizes(true);
  recent_delegate_ = new RecentItemDelegate(recent_list_);
  recent_list_->setItemDelegate(recent_delegate_);
  recent_list_->setMaximumHeight(5 * kRecentItemHeight + 10);
  recent_list_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  recent_list_->setAttribute(Qt::WA_TranslucentBackground);
  recent_list_->viewport()->setAutoFillBackground(false);
  layout->addWidget(recent_list_);

  empty_hint_ = new QLabel(
      tr("No recent documents yet; open a folder to start reading."), content);
  empty_hint_->setObjectName(QStringLiteral("welcomeEmptyHint"));
  empty_hint_->setForegroundRole(QPalette::PlaceholderText);
  empty_hint_->setFont(subtitle_font);
  layout->addWidget(empty_hint_);
  empty_hint_->hide();

  layout->addSpacing(26);
  auto* shortcuts = new QLabel(
      tr("Ctrl+Shift+F workspace search · Ctrl+P quick open"), content);
  shortcuts->setObjectName(QStringLiteral("welcomeShortcuts"));
  shortcuts->setForegroundRole(QPalette::PlaceholderText);
  QFont hint_font = font();
  if (hint_font.pointSizeF() > 0.0) {
    hint_font.setPointSizeF(qMax(8.0, hint_font.pointSizeF() - 1.0));
  }
  shortcuts->setFont(hint_font);
  layout->addWidget(shortcuts);

  outer->addWidget(content, 0, Qt::AlignHCenter);
  outer->addStretch(2);

  connect(open_dir, &QPushButton::clicked, this,
          &WelcomeView::openDirectoryRequested);
  connect(open_file, &QPushButton::clicked, this,
          &WelcomeView::openFileRequested);
  connect(quick_open, &QPushButton::clicked, this,
          &WelcomeView::quickOpenRequested);
  connect(recent_list_, &QListWidget::itemClicked, this,
          [this](QListWidgetItem* item) {
            if (item != nullptr) {
              emit recentActivated(item->data(Qt::UserRole).toString());
            }
          });
  connect(recent_list_, &QListWidget::itemActivated, this,
          [this](QListWidgetItem* item) {
            if (item != nullptr) {
              emit recentActivated(item->data(Qt::UserRole).toString());
            }
          });
}

void WelcomeView::SetRecentFiles(const QStringList& paths,
                                 const QString& root_dir) {
  root_dir_ = root_dir;
  recent_paths_.clear();
  for (const QString& path : paths) {
    if (!QFileInfo(path).isFile()) {
      continue;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (!recent_paths_.contains(absolute)) {
      recent_paths_.append(absolute);
    }
    if (recent_paths_.size() >= 5) {
      break;
    }
  }
  RebuildRecentItems();
}

void WelcomeView::SetThemeDark(bool dark) {
  if (theme_dark_ == dark) {
    return;
  }
  theme_dark_ = dark;
  if (recent_delegate_ != nullptr) {
    recent_delegate_->set_dark(dark);
  }
  if (recent_list_ != nullptr) {
    recent_list_->viewport()->update();
  }
}

void WelcomeView::RebuildRecentItems() {
  recent_list_->clear();
  for (const QString& path : recent_paths_) {
    const QFileInfo info(path);
    QString subtitle;
    if (!root_dir_.isEmpty() && IsPathUnderRoot(path, root_dir_)) {
      subtitle = QDir(root_dir_).relativeFilePath(path);
    } else {
      subtitle = info.absolutePath();
    }
    auto* item = new QListWidgetItem(info.fileName(), recent_list_);
    item->setData(Qt::UserRole, path);
    item->setData(Qt::UserRole + 1, QDir::toNativeSeparators(subtitle));
    item->setToolTip(QDir::toNativeSeparators(path));
  }
  const bool has_recent = !recent_paths_.isEmpty();
  if (has_recent) {
    const int list_height = recent_paths_.size() * kRecentItemHeight + 6;
    recent_list_->setMinimumHeight(list_height);
    recent_list_->setMaximumHeight(list_height);
  }
  recent_title_->setVisible(has_recent);
  recent_list_->setVisible(has_recent);
  empty_hint_->setVisible(!has_recent);
}

void WelcomeView::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), palette().color(QPalette::Base));
}
