#include "settings_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "markdown_render_context.h"
#include "markdown_types.h"
#include "markdown_view.h"

namespace {

// Fluent Gallery 的卡片写法：isCard + WA_StyledBackground，样式由 style 画
QWidget* NewCard(QWidget* parent, const QString& title, QVBoxLayout** content) {
  auto* card = new QWidget(parent);
  card->setProperty("isCard", true);
  card->setAttribute(Qt::WA_StyledBackground, true);
  auto* layout = new QVBoxLayout(card);
  layout->setContentsMargins(16, 12, 16, 12);
  layout->setSpacing(0);

  auto* label = new QLabel(title, card);
  QFont font = label->font();
  font.setBold(true);
  label->setFont(font);
  layout->addWidget(label);
  layout->addSpacing(4);

  *content = layout;
  return card;
}

QWidget* NewPage(QWidget* parent, const QString& object_name) {
  auto* page = new QWidget(parent);
  page->setObjectName(object_name);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(20, 16, 20, 16);
  layout->setSpacing(12);
  return page;
}

void AddPageHeader(QVBoxLayout* layout, QWidget* parent, const QString& title,
                   const QString& subtitle) {
  auto* title_label = new QLabel(title, parent);
  QFont font = title_label->font();
  if (font.pointSizeF() > 0.0) {
    font.setPointSizeF(font.pointSizeF() + 3.0);
  }
  font.setBold(true);
  title_label->setFont(font);
  layout->addWidget(title_label);

  auto* sub_label = new QLabel(subtitle, parent);
  sub_label->setForegroundRole(QPalette::PlaceholderText);
  sub_label->setWordWrap(true);
  layout->addWidget(sub_label);
  layout->addSpacing(4);
}

// 一行设置：左侧标题 + 说明，右侧控件；返回右侧布局供调用方加控件
QHBoxLayout* AddSettingRow(QVBoxLayout* card_layout, QWidget* parent,
                           const QString& title, const QString& description) {
  auto* row = new QWidget(parent);
  auto* row_layout = new QHBoxLayout(row);
  row_layout->setContentsMargins(0, 8, 0, 8);
  row_layout->setSpacing(12);

  auto* left = new QVBoxLayout();
  left->setContentsMargins(0, 0, 0, 0);
  left->setSpacing(2);
  auto* title_label = new QLabel(title, row);
  left->addWidget(title_label);
  if (!description.isEmpty()) {
    auto* desc_label = new QLabel(description, row);
    desc_label->setForegroundRole(QPalette::PlaceholderText);
    desc_label->setWordWrap(true);
    left->addWidget(desc_label);
  }
  row_layout->addLayout(left, 1);

  auto* right = new QHBoxLayout();
  right->setContentsMargins(0, 0, 0, 0);
  right->setSpacing(6);
  right->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  row_layout->addLayout(right, 0);

  card_layout->addWidget(row);
  return right;
}

void AddPage(QListWidget* list, QStackedWidget* stack, const QString& name,
             QWidget* page) {
  auto* scroll = new QScrollArea(stack);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(page);
  stack->addWidget(scroll);
  list->addItem(name);
}

}  // namespace

SettingsPanel::SettingsPanel(MarkdownView* view, QWidget* parent)
    : QWidget(parent), view_(view) {
  // 窗口用欢迎标签的隐藏视图兜底，构造时必须有一个可读视图
  Q_ASSERT(view_ != nullptr);
  initUi();
  initSignals();
}

QCheckBox* SettingsPanel::CreateSwitch(bool checked) {
  auto* box = new QCheckBox(
      checked ? QStringLiteral("开") : QStringLiteral("关"), this);
  box->setProperty("isSwitchButton", true);
  box->setChecked(checked);
  connect(box, &QCheckBox::toggled, box, [box](bool on) {
    box->setText(on ? QStringLiteral("开") : QStringLiteral("关"));
  });
  return box;
}

void SettingsPanel::initUi() {
  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  category_list_ = new QListWidget(this);
  category_list_->setObjectName(QStringLiteral("settingsCategoryList"));
  category_list_->setFixedWidth(140);
  page_stack_ = new QStackedWidget(this);
  page_stack_->setObjectName(QStringLiteral("settingsPageStack"));
  root->addWidget(category_list_);
  root->addWidget(page_stack_, 1);

  // 外观
  QWidget* appearance_page =
      NewPage(page_stack_, QStringLiteral("settingsPageAppearance"));
  auto* appearance_layout =
      qobject_cast<QVBoxLayout*>(appearance_page->layout());
  AddPageHeader(appearance_layout, appearance_page, QStringLiteral("外观"),
                QStringLiteral("主题与窗口材质，修改后立即生效"));
  QVBoxLayout* appearance_card_layout = nullptr;
  QWidget* appearance_card =
      NewCard(appearance_page, QStringLiteral("显示"), &appearance_card_layout);
  {
    QHBoxLayout* right = AddSettingRow(
        appearance_card_layout, appearance_card, QStringLiteral("主题"),
        QStringLiteral("浅色 / 暗色，应用到所有标签"));
    theme_ = new QComboBox(appearance_card);
    theme_->setObjectName(QStringLiteral("themeCombo"));
    theme_->setFixedWidth(200);
    theme_->addItem(QStringLiteral("浅色"),
                    static_cast<int>(MarkdownTheme::Light));
    theme_->addItem(QStringLiteral("暗色"),
                    static_cast<int>(MarkdownTheme::Dark));
    const int theme_index = theme_->findData(static_cast<int>(view_->theme()));
    theme_->setCurrentIndex(theme_index >= 0 ? theme_index : 0);
    right->addWidget(theme_);
  }
#ifdef Q_OS_WIN
  {
    const QString description = QStringLiteral(
        "自动：Windows 11 用 Mica，旧系统回退 DWM 模糊；关闭后背景不透明");
    QHBoxLayout* right = AddSettingRow(appearance_card_layout, appearance_card,
                                       QStringLiteral("窗口材质"), description);
    window_material_ = new QComboBox(appearance_card);
    window_material_->setObjectName(QStringLiteral("windowMaterialCombo"));
    window_material_->setFixedWidth(200);
    window_material_->setToolTip(description);
    window_material_->addItem(QStringLiteral("自动（Mica）"),
                              static_cast<int>(WindowMaterial::kAuto));
    window_material_->addItem(QStringLiteral("亚克力"),
                              static_cast<int>(WindowMaterial::kAcrylic));
    window_material_->addItem(QStringLiteral("关闭"),
                              static_cast<int>(WindowMaterial::kNone));
    right->addWidget(window_material_);
  }
#endif
  appearance_layout->addWidget(appearance_card);
  appearance_layout->addStretch(1);
  AddPage(category_list_, page_stack_, QStringLiteral("外观"), appearance_page);

  // 阅读
  QWidget* reading_page =
      NewPage(page_stack_, QStringLiteral("settingsPageReading"));
  auto* reading_layout = qobject_cast<QVBoxLayout*>(reading_page->layout());
  AddPageHeader(reading_layout, reading_page, QStringLiteral("阅读"),
                QStringLiteral("阅读区样式与透明页面，修改后立即生效"));
  QVBoxLayout* reading_card_layout = nullptr;
  QWidget* reading_card =
      NewCard(reading_page, QStringLiteral("阅读区"), &reading_card_layout);
  {
    QHBoxLayout* right = AddSettingRow(
        reading_card_layout, reading_card, QStringLiteral("样式"),
        QStringLiteral("原型：左对齐 + 卡片；库默认：保持库样式"));
    reading_style_ = new QComboBox(reading_card);
    reading_style_->setObjectName(QStringLiteral("readingStyleCombo"));
    reading_style_->setFixedWidth(220);
    reading_style_->addItem(QStringLiteral("库默认"),
                            static_cast<int>(ReadingStyle::kLibraryDefault));
    reading_style_->addItem(QStringLiteral("原型（左对齐 + 卡片）"),
                            static_cast<int>(ReadingStyle::kPrototype));
    right->addWidget(reading_style_);
  }
  {
    QHBoxLayout* right = AddSettingRow(
        reading_card_layout, reading_card, QStringLiteral("透明阅读区"),
        QStringLiteral("仅原型样式生效；透出 Mica/窗口底色，"
                       "关闭后使用主题页面色"));
    transparent_reading_ = CreateSwitch(true);
    transparent_reading_->setObjectName(
        QStringLiteral("transparentReadingCheck"));
    right->addWidget(transparent_reading_);
  }
  reading_layout->addWidget(reading_card);
  reading_layout->addStretch(1);
  AddPage(category_list_, page_stack_, QStringLiteral("阅读"), reading_page);

  // 内容
  QWidget* content_page =
      NewPage(page_stack_, QStringLiteral("settingsPageContent"));
  auto* content_layout = qobject_cast<QVBoxLayout*>(content_page->layout());
  AddPageHeader(content_layout, content_page, QStringLiteral("内容"),
                QStringLiteral("渲染开关与引擎选择，修改后重新渲染当前文档"));

  QVBoxLayout* switches_layout = nullptr;
  QWidget* switches_card =
      NewCard(content_page, QStringLiteral("渲染开关"), &switches_layout);
  {
    QHBoxLayout* right = AddSettingRow(
        switches_layout, switches_card, QStringLiteral("代码块行号"),
        QStringLiteral("切换后重渲染并保留滚动位置"));
    code_line_numbers_ = CreateSwitch(view_->code_line_numbers());
    code_line_numbers_->setObjectName(QStringLiteral("codeLineNumbersCheck"));
    right->addWidget(code_line_numbers_);
  }
  {
    QHBoxLayout* right = AddSettingRow(
        switches_layout, switches_card, QStringLiteral("公式渲染"),
        QStringLiteral("关闭后 $…$ 原样显示，不加载 MathJax"));
    formula_rendering_ = CreateSwitch(view_->formula_rendering_enabled());
    formula_rendering_->setObjectName(QStringLiteral("formulaRenderingCheck"));
    right->addWidget(formula_rendering_);
  }
  {
    QHBoxLayout* right = AddSettingRow(
        switches_layout, switches_card, QStringLiteral("Mermaid 渲染"),
        QStringLiteral("关闭后 mermaid 围栏退回代码块"));
    mermaid_rendering_ = CreateSwitch(view_->mermaid_rendering_enabled());
    mermaid_rendering_->setObjectName(QStringLiteral("mermaidRenderingCheck"));
    right->addWidget(mermaid_rendering_);
  }
  content_layout->addWidget(switches_card);

  QVBoxLayout* engines_layout = nullptr;
  QWidget* engines_card =
      NewCard(content_page, QStringLiteral("引擎与语法"), &engines_layout);
  {
    const QString description = QStringLiteral(
        "自动：RaTeX 优先，单条公式失败自动用 MathJax 重试；"
        "RaTeX 快，MathJax 兼容性更强但需要加载 QuickJS");
    QHBoxLayout* right = AddSettingRow(engines_layout, engines_card,
                                       QStringLiteral("公式引擎"), description);
    formula_backend_ = new QComboBox(engines_card);
    formula_backend_->setObjectName(QStringLiteral("formulaBackendCombo"));
    formula_backend_->setFixedWidth(220);
    formula_backend_->setToolTip(description);
    formula_backend_->addItem(QStringLiteral("自动（RaTeX 优先）"),
                              static_cast<int>(MarkdownFormulaBackend::kAuto));
    formula_backend_->addItem(
        QStringLiteral("RaTeX（原生）"),
        static_cast<int>(MarkdownFormulaBackend::kNative));
    formula_backend_->addItem(
        QStringLiteral("QuickJS（MathJax）"),
        static_cast<int>(MarkdownFormulaBackend::kQuickjs));
    const int index =
        formula_backend_->findData(static_cast<int>(view_->formula_backend()));
    formula_backend_->setCurrentIndex(index >= 0 ? index : 0);
    right->addWidget(formula_backend_);
  }
  {
    const QString description = QStringLiteral(
        "自动：merman 优先，单图失败自动用 mermaid.js 重试；"
        "merman 快但语法子集小，QuickJS 慢但兼容性最好");
    QHBoxLayout* right =
        AddSettingRow(engines_layout, engines_card,
                      QStringLiteral("Mermaid 引擎"), description);
    mermaid_backend_ = new QComboBox(engines_card);
    mermaid_backend_->setObjectName(QStringLiteral("mermaidBackendCombo"));
    mermaid_backend_->setFixedWidth(220);
    mermaid_backend_->setToolTip(description);
    mermaid_backend_->addItem(QStringLiteral("自动（merman 优先）"),
                              static_cast<int>(MarkdownMermaidBackend::kAuto));
    mermaid_backend_->addItem(
        QStringLiteral("merman（原生）"),
        static_cast<int>(MarkdownMermaidBackend::kNative));
    mermaid_backend_->addItem(
        QStringLiteral("QuickJS（mermaid.js）"),
        static_cast<int>(MarkdownMermaidBackend::kQuickjs));
    const int index =
        mermaid_backend_->findData(static_cast<int>(view_->mermaid_backend()));
    mermaid_backend_->setCurrentIndex(index >= 0 ? index : 0);
    right->addWidget(mermaid_backend_);
  }
  {
    const QString description = QStringLiteral(
        "MPE：标记后同一行文字作为标题，支持 info 等更多类型；"
        "GitHub：标记须单独成行，仅支持五种类型");
    QHBoxLayout* right =
        AddSettingRow(engines_layout, engines_card,
                      QStringLiteral("Admonition 样式"), description);
    admonition_mode_ = new QComboBox(engines_card);
    admonition_mode_->setObjectName(QStringLiteral("admonitionModeCombo"));
    admonition_mode_->setFixedWidth(220);
    admonition_mode_->setToolTip(description);
    admonition_mode_->addItem(QStringLiteral("MPE Callout（默认）"),
                              static_cast<int>(MarkdownAdmonitionMode::kMpe));
    admonition_mode_->addItem(
        QStringLiteral("GitHub Admonition"),
        static_cast<int>(MarkdownAdmonitionMode::kGithub));
    const int index =
        admonition_mode_->findData(static_cast<int>(view_->admonition_mode()));
    admonition_mode_->setCurrentIndex(index >= 0 ? index : 0);
    right->addWidget(admonition_mode_);
  }
  content_layout->addWidget(engines_card);
  content_layout->addStretch(1);
  AddPage(category_list_, page_stack_, QStringLiteral("内容"), content_page);

  // 缓存
  QWidget* cache_page =
      NewPage(page_stack_, QStringLiteral("settingsPageCache"));
  auto* cache_layout = qobject_cast<QVBoxLayout*>(cache_page->layout());
  AddPageHeader(cache_layout, cache_page, QStringLiteral("缓存"),
                QStringLiteral("持久缓存目录与清理操作"));
  QVBoxLayout* cache_card_layout = nullptr;
  QWidget* cache_card =
      NewCard(cache_page, QStringLiteral("持久缓存"), &cache_card_layout);
  {
    QHBoxLayout* right = AddSettingRow(
        cache_card_layout, cache_card, QStringLiteral("启用持久缓存"),
        QStringLiteral("关闭只跳过磁盘缓存读写，内存缓存照常"));
    persistent_cache_ = CreateSwitch(view_->persistent_render_cache_enabled());
    persistent_cache_->setObjectName(QStringLiteral("persistentCacheCheck"));
    right->addWidget(persistent_cache_);
  }
  {
    QHBoxLayout* right =
        AddSettingRow(cache_card_layout, cache_card, QStringLiteral("缓存目录"),
                      QStringLiteral("留空使用默认位置；所有标签共用同一目录"));
    cache_directory_ = new QLineEdit(cache_card);
    cache_directory_->setText(
        view_->render_context().options().cache_directory);
    cache_directory_->setPlaceholderText(QStringLiteral("默认位置"));
    cache_directory_->setReadOnly(true);
    cache_directory_->setMinimumWidth(220);
    right->addWidget(cache_directory_, 1);
    choose_cache_ = new QPushButton(QStringLiteral("选择…"), cache_card);
    reset_cache_ = new QPushButton(QStringLiteral("默认"), cache_card);
    right->addWidget(choose_cache_);
    right->addWidget(reset_cache_);
  }
  {
    QHBoxLayout* right =
        AddSettingRow(cache_card_layout, cache_card, QStringLiteral("缓存操作"),
                      QStringLiteral("清空只影响缓存，不改文档"));
    clear_memory_cache_ =
        new QPushButton(QStringLiteral("清空内存缓存"), cache_card);
    clear_cache_ = new QPushButton(QStringLiteral("清除持久缓存"), cache_card);
    right->addWidget(clear_memory_cache_);
    right->addWidget(clear_cache_);
  }
  cache_layout->addWidget(cache_card);
  cache_layout->addStretch(1);
  AddPage(category_list_, page_stack_, QStringLiteral("缓存"), cache_page);

  // 诊断
  QWidget* diagnostics_page =
      NewPage(page_stack_, QStringLiteral("settingsPageDiagnostics"));
  auto* diagnostics_layout =
      qobject_cast<QVBoxLayout*>(diagnostics_page->layout());
  AddPageHeader(diagnostics_layout, diagnostics_page, QStringLiteral("诊断"),
                QStringLiteral("渲染日志与中间产物，仅供排查问题"));
  QVBoxLayout* diagnostics_card_layout = nullptr;
  QWidget* diagnostics_card = NewCard(
      diagnostics_page, QStringLiteral("日志与产物"), &diagnostics_card_layout);
  {
    QHBoxLayout* right = AddSettingRow(
        diagnostics_card_layout, diagnostics_card, QStringLiteral("trace 日志"),
        QStringLiteral("输出 qt.markdown.render，日志量可能较大"));
    trace_ = CreateSwitch(view_->diagnostics().trace);
    trace_->setObjectName(QStringLiteral("traceCheck"));
    right->addWidget(trace_);
  }
  {
    QHBoxLayout* right = AddSettingRow(
        diagnostics_card_layout, diagnostics_card, QStringLiteral("dump 目录"),
        QStringLiteral("dump 文件可能包含文档正文，仅排查问题时开启"));
    dump_directory_ = new QLineEdit(diagnostics_card);
    dump_directory_->setText(view_->diagnostics().dump_directory);
    dump_directory_->setPlaceholderText(QStringLiteral("未开启"));
    dump_directory_->setReadOnly(true);
    dump_directory_->setMinimumWidth(220);
    right->addWidget(dump_directory_, 1);
    choose_dump_ = new QPushButton(QStringLiteral("选择…"), diagnostics_card);
    reset_dump_ = new QPushButton(QStringLiteral("关闭"), diagnostics_card);
    right->addWidget(choose_dump_);
    right->addWidget(reset_dump_);
  }
  diagnostics_layout->addWidget(diagnostics_card);
  diagnostics_layout->addStretch(1);
  AddPage(category_list_, page_stack_, QStringLiteral("诊断"),
          diagnostics_page);

  category_list_->setCurrentRow(0);
  UpdateReadingControls();
}

void SettingsPanel::SetView(MarkdownView* view) {
  if (view != view_) {
    view_ = view;
  }
}

void SettingsPanel::SetTheme(MarkdownTheme theme) {
  const int index = theme_->findData(static_cast<int>(theme));
  if (index < 0) {
    return;
  }
  const QSignalBlocker blocker(theme_);
  theme_->setCurrentIndex(index);
}

void SettingsPanel::SetWindowMaterial(WindowMaterial material) {
#ifdef Q_OS_WIN
  if (window_material_ == nullptr) {
    return;
  }
  const int index = window_material_->findData(static_cast<int>(material));
  if (index < 0) {
    return;
  }
  const QSignalBlocker blocker(window_material_);
  window_material_->setCurrentIndex(index);
#else
  Q_UNUSED(material);
#endif
}

void SettingsPanel::SetReadingStyle(ReadingStyle style) {
  if (reading_style_ == nullptr) {
    return;
  }
  const int index = reading_style_->findData(static_cast<int>(style));
  if (index < 0) {
    return;
  }
  const QSignalBlocker blocker(reading_style_);
  reading_style_->setCurrentIndex(index);
  UpdateReadingControls();
}

void SettingsPanel::SetTransparentReading(bool enabled) {
  if (transparent_reading_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(transparent_reading_);
  transparent_reading_->setChecked(enabled);
  transparent_reading_->setText(enabled ? QStringLiteral("开")
                                        : QStringLiteral("关"));
}

void SettingsPanel::UpdateReadingControls() {
  if (reading_style_ == nullptr || transparent_reading_ == nullptr) {
    return;
  }
  transparent_reading_->setEnabled(reading_style_->currentData().toInt() ==
                                   static_cast<int>(ReadingStyle::kPrototype));
}

void SettingsPanel::initSignals() {
  connect(category_list_, &QListWidget::currentRowChanged, page_stack_,
          &QStackedWidget::setCurrentIndex);
#ifdef Q_OS_WIN
  connect(window_material_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            if (index < 0) {
              return;
            }
            emit windowMaterialChanged(static_cast<WindowMaterial>(
                window_material_->itemData(index).toInt()));
          });
#endif
  connect(theme_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int index) {
            emit themeChanged(
                static_cast<MarkdownTheme>(theme_->itemData(index).toInt()));
          });
  connect(reading_style_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            UpdateReadingControls();
            emit readingStyleChanged(static_cast<ReadingStyle>(
                reading_style_->itemData(index).toInt()));
          });
  connect(transparent_reading_, &QCheckBox::toggled, this,
          &SettingsPanel::transparentReadingChanged);
  connect(admonition_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            if (index < 0 || view_ == nullptr) {
              return;
            }
            view_->SetAdmonitionMode(static_cast<MarkdownAdmonitionMode>(
                admonition_mode_->itemData(index).toInt()));
            emit settingsChanged();
          });
  connect(code_line_numbers_, &QCheckBox::toggled, this, [this](bool on) {
    if (view_ == nullptr) {
      return;
    }
    view_->SetCodeLineNumbers(on);
    emit settingsChanged();
  });
  connect(formula_rendering_, &QCheckBox::toggled, this, [this](bool on) {
    if (view_ == nullptr) {
      return;
    }
    view_->SetFormulaRenderingEnabled(on);
    emit settingsChanged();
  });
  connect(formula_backend_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            if (index < 0 || view_ == nullptr) {
              return;
            }
            view_->SetFormulaBackend(static_cast<MarkdownFormulaBackend>(
                formula_backend_->itemData(index).toInt()));
            emit settingsChanged();
          });
  connect(mermaid_rendering_, &QCheckBox::toggled, this, [this](bool on) {
    if (view_ == nullptr) {
      return;
    }
    view_->SetMermaidRenderingEnabled(on);
    emit settingsChanged();
  });
  connect(mermaid_backend_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            if (index < 0 || view_ == nullptr) {
              return;
            }
            view_->SetMermaidBackend(static_cast<MarkdownMermaidBackend>(
                mermaid_backend_->itemData(index).toInt()));
            emit settingsChanged();
          });
  connect(persistent_cache_, &QCheckBox::toggled, this, [this](bool on) {
    if (view_ == nullptr) {
      return;
    }
    view_->SetPersistentRenderCacheEnabled(on);
    emit settingsChanged();
  });
  connect(trace_, &QCheckBox::toggled, this, [this] { ApplyDiagnostics(); });
  connect(choose_cache_, &QPushButton::clicked, this,
          &SettingsPanel::BrowseCacheDirectory);
  connect(reset_cache_, &QPushButton::clicked, this, [this] {
    if (view_ == nullptr) {
      return;
    }
    cache_directory_->clear();
    view_->SetRenderCacheDirectory(QString());
    emit settingsChanged();
    emit statusMessage(QStringLiteral("持久缓存目录恢复默认"));
  });
  connect(clear_memory_cache_, &QPushButton::clicked, this, [this] {
    if (view_ == nullptr) {
      return;
    }
    view_->ClearRenderCaches();
    emit statusMessage(QStringLiteral("已清空内存缓存"));
  });
  connect(clear_cache_, &QPushButton::clicked, this, [this] {
    if (view_ == nullptr) {
      return;
    }
    view_->ClearPersistentRenderCache();
    emit statusMessage(QStringLiteral("已清除持久渲染缓存"));
  });
  connect(choose_dump_, &QPushButton::clicked, this,
          &SettingsPanel::BrowseDumpDirectory);
  connect(reset_dump_, &QPushButton::clicked, this, [this] {
    dump_directory_->clear();
    ApplyDiagnostics();
  });
}

void SettingsPanel::ApplyDiagnostics() {
  if (view_ == nullptr) {
    return;
  }
  MarkdownDiagnostics diagnostics;
  diagnostics.trace = trace_->isChecked();
  diagnostics.dump_directory = dump_directory_->text().trimmed();
  view_->SetDiagnostics(diagnostics);
  emit settingsChanged();
}

void SettingsPanel::BrowseCacheDirectory() {
  if (view_ == nullptr) {
    return;
  }
  const QString dir = QFileDialog::getExistingDirectory(
      this, QStringLiteral("选择持久缓存目录"),
      cache_directory_->text().isEmpty() ? QDir::homePath()
                                         : cache_directory_->text());
  if (dir.isEmpty()) {
    return;
  }
  cache_directory_->setText(dir);
  view_->SetRenderCacheDirectory(dir);
  emit settingsChanged();
}

void SettingsPanel::BrowseDumpDirectory() {
  if (view_ == nullptr) {
    return;
  }
  const QString dir = QFileDialog::getExistingDirectory(
      this, QStringLiteral("选择 dump 目录"),
      dump_directory_->text().isEmpty() ? QDir::homePath()
                                        : dump_directory_->text());
  if (dir.isEmpty()) {
    return;
  }
  dump_directory_->setText(dir);
  ApplyDiagnostics();
}
