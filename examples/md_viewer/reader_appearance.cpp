#include "reader_appearance.h"

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
#include <QStringList>
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
    painter.setBrush(palette().color(QPalette::Base));
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

QString ReaderAppearanceCss(const ReaderPreferences& preferences,
                            bool prototype_layout) {
  QStringList rules;
  if (preferences.font_size > 0 || preferences.line_height_percent > 0) {
    QString body = QStringLiteral("body {");
    if (preferences.font_size > 0) {
      body += QStringLiteral(" font-size: %1px;")
                  .arg(qBound(10, preferences.font_size, 24));
    }
    if (preferences.line_height_percent > 0) {
      const qreal line_height =
          qBound(120, preferences.line_height_percent, 240) / 100.0;
      body += QStringLiteral(" line-height: %1;").arg(line_height, 0, 'f', 2);
    }
    body += QStringLiteral(" }");
    rules.append(body);
  }
  if (preferences.measure_px >= 0) {
    const QString width = preferences.measure_px == 0
                              ? QStringLiteral("none")
                              : QStringLiteral("%1px").arg(
                                    qBound(480, preferences.measure_px, 1600));
    if (prototype_layout) {
      rules.append(
          QStringLiteral("h1, h2, h3, h4, h5, h6, p, ul, ol, blockquote { "
                         "max-width: %1; }")
              .arg(width));
    } else {
      rules.append(QStringLiteral(".md-page { max-width: %1; }").arg(width));
    }
  }
  return rules.join(QLatin1Char('\n'));
}

ReaderAppearancePopup::ReaderAppearancePopup(QWidget* parent)
    : QWidget(parent) {
  setObjectName(QStringLiteral("readerAppearancePopup"));
  setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_TranslucentBackground);

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(16, 16, 16, 16);
  outer->setSpacing(0);

  auto* panel = new AppearancePanel(this);
  panel->setObjectName(QStringLiteral("readerAppearancePanel"));
  auto* shadow = new QGraphicsDropShadowEffect(panel);
  shadow->setBlurRadius(30);
  shadow->setOffset(0, 8);
  shadow->setColor(QColor(0, 0, 0, 90));
  panel->setGraphicsEffect(shadow);

  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(16, 14, 16, 12);
  layout->setSpacing(10);

  auto* title = new QLabel(QStringLiteral("阅读外观"), panel);
  title->setObjectName(QStringLiteral("readerAppearanceTitle"));
  QFont title_font = font();
  if (title_font.pointSizeF() > 0.0) {
    title_font.setPointSizeF(title_font.pointSizeF() + 2.0);
  }
  title_font.setWeight(QFont::DemiBold);
  title->setFont(title_font);
  layout->addWidget(title);

  auto* subtitle =
      new QLabel(QStringLiteral("字号、行距与栏宽实时生效"), panel);
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
      group->addButton(button, option.second);
      row->addWidget(button, 1);
      connect(button, &QToolButton::clicked, this,
              [this, option, apply] { apply(option.second); });
    }
    *group_out = group;
    layout->addLayout(row);
  };

  add_choice_row(
      QStringLiteral("字号"), QStringLiteral("appearanceFont"),
      {{QStringLiteral("小"), 13},
       {QStringLiteral("标准"), 15},
       {QStringLiteral("大"), 17},
       {QStringLiteral("特大"), 19}},
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
      QStringLiteral("行距"), QStringLiteral("appearanceLine"),
      {{QStringLiteral("紧凑"), 160},
       {QStringLiteral("标准"), 178},
       {QStringLiteral("宽松"), 200}},
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
      QStringLiteral("栏宽"), QStringLiteral("appearanceMeasure"),
      {{QStringLiteral("窄"), 680},
       {QStringLiteral("舒适"), 760},
       {QStringLiteral("宽"), 900},
       {QStringLiteral("全宽"), 0}},
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
  auto* reset = new QPushButton(QStringLiteral("恢复默认"), panel);
  reset->setObjectName(QStringLiteral("readerAppearanceReset"));
  reset->setFlat(true);
  footer->addWidget(reset);
  footer->addStretch(1);
  auto* hint = new QLabel(QStringLiteral("Esc 关闭"), panel);
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
  resize(kPopupWidth, sizeHint().height());
  const QPoint anchor_top = anchor->mapToGlobal(QPoint(0, 0));
  const QPoint anchor_bottom =
      anchor->mapToGlobal(QPoint(0, anchor->height() + 8));
  QPoint top_left(anchor_top.x() + anchor->width() / 2 - width() / 2,
                  anchor_bottom.y());
  QScreen* screen = QGuiApplication::screenAt(anchor_bottom);
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen != nullptr) {
    const QRect available = screen->availableGeometry();
    top_left.setX(qBound(available.left() + 12, top_left.x(),
                         available.right() - width() - 12));
    top_left.setY(qBound(available.top() + 12, top_left.y(),
                         available.bottom() - height() - 12));
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
      button->setChecked(group->id(button) == value);
    }
    group->setExclusive(exclusive);
  };
  check_group(font_group_, preferences_.font_size);
  check_group(line_group_, preferences_.line_height_percent);
  check_group(measure_group_, preferences_.measure_px);
}
