#include "reader_appearance_popup.h"

#include <functional>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPair>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>

namespace {

constexpr int kPanelRadius = 12;
constexpr int kPopupWidth = 388;

class AppearancePanel final : public QFrame {
 public:
  explicit AppearancePanel(QWidget* parent = nullptr) : QFrame(parent) {}

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor border = palette().color(QPalette::Mid);
    border.setAlpha(110);
    painter.setPen(QPen(border, 1.0));
    // DwmBlur 模式下调色板的 Base 带透明度，弹层必须自己填成不透明
    QColor background = palette().color(QPalette::Base);
    background.setAlpha(255);
    painter.setBrush(background);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), kPanelRadius,
                            kPanelRadius);
  }
};

// 自定义绘制选项：Fluent 的 QToolButton 选中态不适合做分段控件，
// 这里只画圆角底色与文字，避免引入 QSS
class ChoiceButton final : public QToolButton {
 public:
  ChoiceButton(const QString& text, QWidget* parent) : QToolButton(parent) {
    setText(text);
    setCheckable(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(28);
    setCursor(Qt::PointingHandCursor);
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRect area = rect().adjusted(1, 1, -1, -1);
    const QColor accent = palette().color(QPalette::Highlight);
    QColor background =
        isChecked() ? accent : palette().color(QPalette::Button);
    background.setAlpha(isChecked() ? 38 : 130);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(area, 6, 6);
    painter.setPen(isChecked() ? accent
                               : palette().color(QPalette::ButtonText));
    QFont text_font = font();
    if (isChecked()) {
      text_font.setWeight(QFont::DemiBold);
    }
    painter.setFont(text_font);
    painter.drawText(area, Qt::AlignCenter, text());
  }
};

}  // namespace

ReaderAppearancePopup::ReaderAppearancePopup(QWidget* parent)
    : QWidget(parent) {
  setObjectName(QStringLiteral("readerAppearancePopup"));
  setWindowFlags(Qt::Popup | Qt::FramelessWindowHint |
                 Qt::NoDropShadowWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);

  auto* outer = new QVBoxLayout(this);
  // 阴影半径必须放得进外边距，否则会被窗口边缘裁出一条硬边灰带
  outer->setContentsMargins(24, 24, 24, 24);
  outer->setSpacing(0);

  auto* panel = new AppearancePanel(this);
  panel->setObjectName(QStringLiteral("readerAppearancePanel"));
  auto* shadow = new QGraphicsDropShadowEffect(panel);
  shadow->setBlurRadius(20);
  shadow->setOffset(0, 0);
  shadow->setColor(QColor(105, 105, 105, 100));
  panel->setGraphicsEffect(shadow);

  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(16, 14, 16, 12);
  layout->setSpacing(10);

  auto* title = new QLabel(tr("Reading Appearance"), panel);
  title->setObjectName(QStringLiteral("readerAppearanceTitle"));
  QFont title_font = font();
  if (title_font.pointSizeF() > 0.0) {
    title_font.setPointSizeF(title_font.pointSizeF() + 2.0);
  }
  title_font.setWeight(QFont::DemiBold);
  title->setFont(title_font);
  layout->addWidget(title);

  auto* subtitle = new QLabel(
      tr("Font size, line height and measure apply instantly"), panel);
  subtitle->setForegroundRole(QPalette::PlaceholderText);
  QFont subtitle_font = font();
  if (subtitle_font.pointSizeF() > 0.0) {
    subtitle_font.setPointSizeF(qMax(8.0, subtitle_font.pointSizeF() - 1.0));
  }
  subtitle->setFont(subtitle_font);
  layout->addWidget(subtitle);

  auto add_choice_row = [this, panel, layout](
                            const QString& label, const QString& object_prefix,
                            const QVector<QPair<QString, int>>& options,
                            const std::function<void(int)>& apply,
                            QButtonGroup** group_out) {
    auto* row = new QHBoxLayout;
    row->setSpacing(5);
    auto* caption = new QLabel(label, panel);
    caption->setFixedWidth(36);
    row->addWidget(caption);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (const QPair<QString, int>& option : options) {
      auto* button = new ChoiceButton(option.first, panel);
      button->setObjectName(object_prefix + QString::number(option.second));
      // 组 id 不能直接用 -1（QButtonGroup 会当成自动分配），
      // 选中态统一读这个属性
      button->setProperty("preferenceValue", option.second);
      group->addButton(button);
      row->addWidget(button, 1);
      connect(button, &QToolButton::clicked, this,
              [this, option, apply] { apply(option.second); });
    }
    *group_out = group;
    layout->addLayout(row);
  };

  add_choice_row(
      tr("Font Size"), QStringLiteral("appearanceFont"),
      {{tr("Small"), 13},
       {tr("Standard"), 15},
       {tr("Large"), 17},
       {tr("Extra Large"), 19}},
      [this](int value) {
        if (preferences_.font_size == value) {
          return;
        }
        preferences_.font_size = value;
        UpdateChecked();
        emit preferencesChanged(preferences_);
      },
      &font_group_);

  add_choice_row(
      tr("Line Height"), QStringLiteral("appearanceLine"),
      {{tr("Compact"), 160}, {tr("Standard"), 178}, {tr("Relaxed"), 200}},
      [this](int value) {
        if (preferences_.line_height_percent == value) {
          return;
        }
        preferences_.line_height_percent = value;
        UpdateChecked();
        emit preferencesChanged(preferences_);
      },
      &line_group_);

  add_choice_row(
      tr("Measure"), QStringLiteral("appearanceMeasure"),
      {{tr("Default"), -1},
       {tr("Narrow"), 680},
       {tr("Comfortable"), 760},
       {tr("Wide"), 900},
       {tr("Full Width"), 0}},
      [this](int value) {
        if (preferences_.measure_px == value) {
          return;
        }
        preferences_.measure_px = value;
        UpdateChecked();
        emit preferencesChanged(preferences_);
      },
      &measure_group_);

  auto* footer = new QHBoxLayout;
  footer->setContentsMargins(0, 2, 0, 0);
  auto* reset = new QPushButton(tr("Reset to Defaults"), panel);
  reset->setObjectName(QStringLiteral("readerAppearanceReset"));
  reset->setFlat(true);
  footer->addWidget(reset);
  footer->addStretch(1);
  auto* hint = new QLabel(tr("Esc closes"), panel);
  hint->setForegroundRole(QPalette::PlaceholderText);
  hint->setFont(subtitle_font);
  footer->addWidget(hint);
  layout->addLayout(footer);

  connect(reset, &QPushButton::clicked, this, [this] {
    if (preferences_.IsDefault()) {
      return;
    }
    preferences_ = ReaderPreferences();
    UpdateChecked();
    emit preferencesChanged(preferences_);
  });

  outer->addWidget(panel);
  resize(kPopupWidth, sizeHint().height());
}

void ReaderAppearancePopup::SetPreferences(
    const ReaderPreferences& preferences) {
  preferences_ = preferences;
  UpdateChecked();
}

void ReaderAppearancePopup::OpenNear(QWidget* anchor) {
  if (anchor == nullptr) {
    return;
  }
  adjustSize();
  const QPoint anchor_top = anchor->mapToGlobal(QPoint(0, 0));
  const QPoint anchor_bottom =
      anchor->mapToGlobal(QPoint(0, anchor->height() + 8));
  QScreen* screen = QGuiApplication::screenAt(anchor_bottom);
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  const QRect available =
      screen != nullptr ? screen->availableGeometry() : QRect();
  int width = kPopupWidth;
  int height = sizeHint().height();
  if (available.isValid()) {
    width = qMin(width, qMax(220, available.width() - 24));
    height = qMin(height, qMax(160, available.height() - 24));
  }
  resize(width, height);
  QPoint top_left(anchor_top.x() + anchor->width() / 2 - width / 2,
                  anchor_bottom.y());
  if (available.isValid()) {
    const int min_x = available.left() + 12;
    const int min_y = available.top() + 12;
    const int max_x = qMax(min_x, available.right() - width - 12);
    const int max_y = qMax(min_y, available.bottom() - height - 12);
    top_left.setX(qBound(min_x, top_left.x(), max_x));
    top_left.setY(qBound(min_y, top_left.y(), max_y));
  }
  move(top_left);
  show();
  raise();
  activateWindow();
}

void ReaderAppearancePopup::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    hide();
    return;
  }
  QWidget::keyPressEvent(event);
}

void ReaderAppearancePopup::UpdateChecked() {
  auto check_group = [](QButtonGroup* group, int value) {
    if (group == nullptr) {
      return;
    }
    // 独占组会把已选中按钮的 setChecked(false) 吃掉；先临时关掉独占
    const bool exclusive = group->exclusive();
    group->setExclusive(false);
    for (QAbstractButton* button : group->buttons()) {
      QSignalBlocker blocker(button);
      button->setChecked(button->property("preferenceValue").toInt() == value);
    }
    group->setExclusive(exclusive);
  };
  check_group(font_group_, preferences_.font_size);
  check_group(line_group_, preferences_.line_height_percent);
  check_group(measure_group_, preferences_.measure_px);
}
