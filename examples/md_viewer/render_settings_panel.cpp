#include "render_settings_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
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
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(10);

  auto* label = new QLabel(title, card);
  QFont font = label->font();
  font.setBold(true);
  label->setFont(font);
  layout->addWidget(label);

  *content = layout;
  return card;
}

void AddSwitchRow(QVBoxLayout* layout, QWidget* parent, const QString& text,
                  QCheckBox* box, const QString& tooltip) {
  auto* label = new QLabel(text, parent);
  label->setToolTip(tooltip);
  box->setToolTip(tooltip);
  layout->addWidget(label);
  layout->addWidget(box);
}

// 只读路径 + 选择/默认两个按钮
QWidget* NewPathRow(QWidget* parent, QLineEdit* edit, QPushButton** choose,
                    QPushButton** reset, const QString& choose_text,
                    const QString& reset_text) {
  auto* row = new QWidget(parent);
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);
  edit->setReadOnly(true);
  layout->addWidget(edit, 1);
  *choose = new QPushButton(choose_text, row);
  *reset = new QPushButton(reset_text, row);
  layout->addWidget(*choose);
  layout->addWidget(*reset);
  return row;
}

}  // namespace

RenderSettingsPanel::RenderSettingsPanel(MarkdownView* view, QWidget* parent)
    : QWidget(parent), view_(view) {
  initUi();
  initSignals();
}

QCheckBox* RenderSettingsPanel::CreateSwitch(bool checked) {
  auto* box = new QCheckBox(
      checked ? QStringLiteral("开") : QStringLiteral("关"), this);
  box->setProperty("isSwitchButton", true);
  box->setChecked(checked);
  connect(box, &QCheckBox::toggled, box, [box](bool on) {
    box->setText(on ? QStringLiteral("开") : QStringLiteral("关"));
  });
  return box;
}

void RenderSettingsPanel::initUi() {
  // 面板放进滚动区：dock 高度可以随便压，内容不会被挤扁
  auto* panel_layout = new QVBoxLayout(this);
  panel_layout->setContentsMargins(0, 0, 0, 0);
  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* body = new QWidget;
  auto* outer = new QVBoxLayout(body);
  outer->setContentsMargins(12, 12, 12, 12);
  outer->setSpacing(12);

  // 外观
  QVBoxLayout* appearance = nullptr;
  QWidget* appearance_card = NewCard(body, QStringLiteral("外观"), &appearance);
  theme_ = new QComboBox(appearance_card);
  theme_->setObjectName(QStringLiteral("themeCombo"));
  theme_->addItem(QStringLiteral("浅色"),
                  static_cast<int>(MarkdownTheme::Light));
  theme_->addItem(QStringLiteral("暗色"),
                  static_cast<int>(MarkdownTheme::Dark));
  const int theme_index = theme_->findData(static_cast<int>(view_->theme()));
  theme_->setCurrentIndex(theme_index >= 0 ? theme_index : 0);
  appearance->addWidget(new QLabel(QStringLiteral("主题"), appearance_card));
  appearance->addWidget(theme_);
#ifdef Q_OS_WIN
  // 窗口材质是 Windows 的 DWM 能力；其它平台不创建这个下拉框
  auto* material_label =
      new QLabel(QStringLiteral("窗口材质"), appearance_card);
  material_label->setToolTip(QStringLiteral(
      "自动：Windows 11 用 Mica，旧系统回退 DWM 模糊；关闭后背景不透明"));
  appearance->addWidget(material_label);
  window_material_ = new QComboBox(appearance_card);
  window_material_->setObjectName(QStringLiteral("windowMaterialCombo"));
  window_material_->setToolTip(material_label->toolTip());
  window_material_->addItem(QStringLiteral("自动（Mica）"),
                            static_cast<int>(WindowMaterial::kAuto));
  window_material_->addItem(QStringLiteral("亚克力"),
                            static_cast<int>(WindowMaterial::kAcrylic));
  window_material_->addItem(QStringLiteral("关闭"),
                            static_cast<int>(WindowMaterial::kNone));
  appearance->addWidget(window_material_);
#endif
  outer->addWidget(appearance_card);

  // 阅读区：原型样式 opt-in；透明页面只对原型样式生效
  QVBoxLayout* reading = nullptr;
  QWidget* reading_card = NewCard(body, QStringLiteral("阅读区"), &reading);
  reading->addWidget(new QLabel(QStringLiteral("样式"), reading_card));
  reading_style_ = new QComboBox(reading_card);
  reading_style_->setObjectName(QStringLiteral("readingStyleCombo"));
  reading_style_->addItem(QStringLiteral("库默认"),
                          static_cast<int>(ReadingStyle::kLibraryDefault));
  reading_style_->addItem(QStringLiteral("原型（左对齐 + 卡片）"),
                          static_cast<int>(ReadingStyle::kPrototype));
  reading_style_->setToolTip(
      QStringLiteral("原型阅读样式为 opt-in；库默认样式保持不变"));
  reading->addWidget(reading_style_);
  transparent_reading_ = CreateSwitch(true);
  transparent_reading_->setObjectName(
      QStringLiteral("transparentReadingCheck"));
  AddSwitchRow(reading, reading_card, QStringLiteral("透明阅读区"),
               transparent_reading_,
               QStringLiteral("仅原型样式生效；透出 Mica/窗口底色，"
                              "关闭后使用主题页面色"));
  outer->addWidget(reading_card);

  // 内容
  QVBoxLayout* content = nullptr;
  QWidget* content_card = NewCard(body, QStringLiteral("内容"), &content);
  code_line_numbers_ = CreateSwitch(view_->code_line_numbers());
  code_line_numbers_->setObjectName(QStringLiteral("codeLineNumbersCheck"));
  AddSwitchRow(content, content_card, QStringLiteral("代码块行号"),
               code_line_numbers_,
               QStringLiteral("切换后重渲染并保留滚动位置"));
  formula_rendering_ = CreateSwitch(view_->formula_rendering_enabled());
  formula_rendering_->setObjectName(QStringLiteral("formulaRenderingCheck"));
  AddSwitchRow(content, content_card, QStringLiteral("公式渲染"),
               formula_rendering_,
               QStringLiteral("关闭后 $…$ 原样显示，不加载 MathJax"));
  mermaid_rendering_ = CreateSwitch(view_->mermaid_rendering_enabled());
  mermaid_rendering_->setObjectName(QStringLiteral("mermaidRenderingCheck"));
  AddSwitchRow(content, content_card, QStringLiteral("Mermaid 渲染"),
               mermaid_rendering_,
               QStringLiteral("关闭后 mermaid 围栏退回代码块"));
  content->addWidget(
      new QLabel(QStringLiteral("Admonition 样式"), content_card));
  admonition_mode_ = new QComboBox(content_card);
  admonition_mode_->setObjectName(QStringLiteral("admonitionModeCombo"));
  admonition_mode_->setToolTip(
      QStringLiteral("MPE：标记后同一行文字作为标题，支持 info 等更多类型；"
                     "GitHub：标记须单独成行，仅支持五种类型"));
  admonition_mode_->addItem(QStringLiteral("MPE Callout（默认）"),
                            static_cast<int>(MarkdownAdmonitionMode::kMpe));
  admonition_mode_->addItem(QStringLiteral("GitHub Admonition"),
                            static_cast<int>(MarkdownAdmonitionMode::kGithub));
  const int admonition_index =
      admonition_mode_->findData(static_cast<int>(view_->admonition_mode()));
  admonition_mode_->setCurrentIndex(admonition_index >= 0 ? admonition_index
                                                          : 0);
  content->addWidget(admonition_mode_);
  content->addWidget(new QLabel(QStringLiteral("Mermaid 引擎"), content_card));
  mermaid_backend_ = new QComboBox(content_card);
  mermaid_backend_->setObjectName(QStringLiteral("mermaidBackendCombo"));
  mermaid_backend_->setToolTip(
      QStringLiteral("自动：merman 优先，单图失败自动用 mermaid.js 重试；"
                     "merman 快但语法子集小，QuickJS 慢但兼容性最好"));
  mermaid_backend_->addItem(QStringLiteral("自动（merman 优先）"),
                            static_cast<int>(MarkdownMermaidBackend::kAuto));
  mermaid_backend_->addItem(QStringLiteral("merman（原生）"),
                            static_cast<int>(MarkdownMermaidBackend::kNative));
  mermaid_backend_->addItem(QStringLiteral("QuickJS（mermaid.js）"),
                            static_cast<int>(MarkdownMermaidBackend::kQuickjs));
  const int mermaid_index =
      mermaid_backend_->findData(static_cast<int>(view_->mermaid_backend()));
  mermaid_backend_->setCurrentIndex(mermaid_index >= 0 ? mermaid_index : 0);
  content->addWidget(mermaid_backend_);
  outer->addWidget(content_card);

  // 持久缓存
  QVBoxLayout* cache = nullptr;
  QWidget* cache_card = NewCard(body, QStringLiteral("持久缓存"), &cache);
  persistent_cache_ = CreateSwitch(view_->persistent_render_cache_enabled());
  persistent_cache_->setObjectName(QStringLiteral("persistentCacheCheck"));
  AddSwitchRow(cache, cache_card, QStringLiteral("启用持久缓存"),
               persistent_cache_,
               QStringLiteral("关闭只跳过磁盘缓存读写，内存缓存照常"));
  cache_directory_ = new QLineEdit(cache_card);
  cache_directory_->setText(view_->render_context().options().cache_directory);
  cache_directory_->setPlaceholderText(QStringLiteral("默认位置"));
  cache->addWidget(NewPathRow(cache_card, cache_directory_, &choose_cache_,
                              &reset_cache_, QStringLiteral("选择…"),
                              QStringLiteral("默认")));
  auto* cache_actions = new QWidget(cache_card);
  auto* cache_actions_layout = new QHBoxLayout(cache_actions);
  cache_actions_layout->setContentsMargins(0, 0, 0, 0);
  cache_actions_layout->setSpacing(6);
  clear_memory_cache_ =
      new QPushButton(QStringLiteral("清空内存缓存"), cache_actions);
  clear_cache_ = new QPushButton(QStringLiteral("清除持久缓存"), cache_actions);
  cache_actions_layout->addWidget(clear_memory_cache_);
  cache_actions_layout->addWidget(clear_cache_);
  cache->addWidget(cache_actions);
  outer->addWidget(cache_card);

  // 诊断
  QVBoxLayout* diagnostics = nullptr;
  QWidget* diagnostics_card =
      NewCard(body, QStringLiteral("诊断"), &diagnostics);
  trace_ = CreateSwitch(view_->diagnostics().trace);
  trace_->setObjectName(QStringLiteral("traceCheck"));
  AddSwitchRow(diagnostics, diagnostics_card, QStringLiteral("trace 日志"),
               trace_,
               QStringLiteral("输出 qt.markdown.render，日志量可能较大"));
  dump_directory_ = new QLineEdit(diagnostics_card);
  dump_directory_->setText(view_->diagnostics().dump_directory);
  dump_directory_->setPlaceholderText(QStringLiteral("未开启"));
  dump_directory_->setToolTip(
      QStringLiteral("dump 文件可能包含文档正文，仅排查问题时开启"));
  diagnostics->addWidget(
      NewPathRow(diagnostics_card, dump_directory_, &choose_dump_, &reset_dump_,
                 QStringLiteral("选择…"), QStringLiteral("关闭")));
  outer->addWidget(diagnostics_card);

  outer->addStretch(1);
  scroll->setWidget(body);
  panel_layout->addWidget(scroll);
  UpdateReadingControls();
}

void RenderSettingsPanel::SetTheme(MarkdownTheme theme) {
  const int index = theme_->findData(static_cast<int>(theme));
  if (index < 0) {
    return;
  }
  const QSignalBlocker blocker(theme_);
  theme_->setCurrentIndex(index);
}

void RenderSettingsPanel::SetWindowMaterial(WindowMaterial material) {
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

void RenderSettingsPanel::SetReadingStyle(ReadingStyle style) {
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

void RenderSettingsPanel::SetTransparentReading(bool enabled) {
  if (transparent_reading_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(transparent_reading_);
  transparent_reading_->setChecked(enabled);
  transparent_reading_->setText(enabled ? QStringLiteral("开")
                                        : QStringLiteral("关"));
}

void RenderSettingsPanel::UpdateReadingControls() {
  if (reading_style_ == nullptr || transparent_reading_ == nullptr) {
    return;
  }
  transparent_reading_->setEnabled(reading_style_->currentData().toInt() ==
                                   static_cast<int>(ReadingStyle::kPrototype));
}

void RenderSettingsPanel::initSignals() {
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
          &RenderSettingsPanel::transparentReadingChanged);
  connect(admonition_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            if (index < 0) {
              return;
            }
            view_->SetAdmonitionMode(static_cast<MarkdownAdmonitionMode>(
                admonition_mode_->itemData(index).toInt()));
          });
  connect(code_line_numbers_, &QCheckBox::toggled, view_,
          &MarkdownView::SetCodeLineNumbers);
  connect(formula_rendering_, &QCheckBox::toggled, view_,
          &MarkdownView::SetFormulaRenderingEnabled);
  connect(mermaid_rendering_, &QCheckBox::toggled, view_,
          &MarkdownView::SetMermaidRenderingEnabled);
  connect(mermaid_backend_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            if (index < 0) {
              return;
            }
            view_->SetMermaidBackend(static_cast<MarkdownMermaidBackend>(
                mermaid_backend_->itemData(index).toInt()));
          });
  connect(persistent_cache_, &QCheckBox::toggled, view_,
          &MarkdownView::SetPersistentRenderCacheEnabled);
  connect(trace_, &QCheckBox::toggled, this, [this] { ApplyDiagnostics(); });
  connect(choose_cache_, &QPushButton::clicked, this,
          &RenderSettingsPanel::BrowseCacheDirectory);
  connect(reset_cache_, &QPushButton::clicked, this, [this] {
    cache_directory_->clear();
    view_->SetRenderCacheDirectory(QString());
    emit statusMessage(QStringLiteral("持久缓存目录恢复默认"));
  });
  connect(clear_memory_cache_, &QPushButton::clicked, this, [this] {
    view_->ClearRenderCaches();
    emit statusMessage(QStringLiteral("已清空内存缓存"));
  });
  connect(clear_cache_, &QPushButton::clicked, this, [this] {
    view_->ClearPersistentRenderCache();
    emit statusMessage(QStringLiteral("已清除持久渲染缓存"));
  });
  connect(choose_dump_, &QPushButton::clicked, this,
          &RenderSettingsPanel::BrowseDumpDirectory);
  connect(reset_dump_, &QPushButton::clicked, this, [this] {
    dump_directory_->clear();
    ApplyDiagnostics();
  });
}

void RenderSettingsPanel::ApplyDiagnostics() {
  MarkdownDiagnostics diagnostics;
  diagnostics.trace = trace_->isChecked();
  diagnostics.dump_directory = dump_directory_->text().trimmed();
  view_->SetDiagnostics(diagnostics);
}

void RenderSettingsPanel::BrowseCacheDirectory() {
  const QString dir = QFileDialog::getExistingDirectory(
      this, QStringLiteral("选择持久缓存目录"),
      cache_directory_->text().isEmpty() ? QDir::homePath()
                                         : cache_directory_->text());
  if (dir.isEmpty()) {
    return;
  }
  cache_directory_->setText(dir);
  view_->SetRenderCacheDirectory(dir);
}

void RenderSettingsPanel::BrowseDumpDirectory() {
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
