#ifndef FILE_APPS_QT_MD_OBSERVATORY_OBSERVATORY_WINDOW_H_
#define FILE_APPS_QT_MD_OBSERVATORY_OBSERVATORY_WINDOW_H_

#include <QWidget>

class QLabel;
class QPushButton;
class QSplitter;
class MarkdownView;
class WorkspaceModel;
class WorkspacePanel;
class DocumentRail;
class CommandPalette;
class TitleBar;

namespace QWK {
class WidgetWindowAgent;
}

// Frameless workspace reader. Layout: title bar, workspace rail, document,
// document rail. The window only consumes the block model exposed by
// MarkdownView; it never parses HTML.
class ObservatoryWindow : public QWidget {
  Q_OBJECT

 public:
  explicit ObservatoryWindow(QWidget* parent = nullptr);

  bool OpenWorkspace(const QString& root);
  bool OpenDocument(int index);

  WorkspaceModel* workspace_model() const { return model_; }

 protected:
  void changeEvent(QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;

 private:
  void BuildUi();
  void ApplyTheme(bool dark);
  void OpenRelative(int delta);
  void OpenPalette();
  void ToggleWorkspace();
  void ToggleRail();
  void SetFocusMode(bool enabled);
  void JumpSection(int delta);
  void ScrollBy(int delta);
  bool IsTyping() const;
  void HandleEscape();
  void SaveCurrentPosition(bool persist);
  void UpdateProgressFromScroll(int y);
  void OnRenderFinished();
  void ShowResumeBar(bool visible, int percent);

  QWK::WidgetWindowAgent* window_agent_ = nullptr;
  TitleBar* title_bar_ = nullptr;
  WorkspacePanel* workspace_panel_ = nullptr;
  DocumentRail* document_rail_ = nullptr;
  CommandPalette* palette_ = nullptr;
  QSplitter* splitter_ = nullptr;
  MarkdownView* view_ = nullptr;
  QLabel* breadcrumb_ = nullptr;
  QWidget* resume_bar_ = nullptr;
  QLabel* resume_text_ = nullptr;
  QPushButton* resume_top_ = nullptr;
  QPushButton* resume_close_ = nullptr;

  WorkspaceModel* model_ = nullptr;
  int current_index_ = -1;
  int current_percent_ = 0;
  bool dark_ = true;
  bool focus_mode_ = false;
  bool workspace_before_focus_ = true;
  bool rail_before_focus_ = true;
  QList<int> sizes_before_focus_;
  // The resume bar belongs to the first render after OpenDocument, not to
  // later theme/zoom re-renders of the same document.
  bool resume_on_render_ = false;
};

#endif  // FILE_APPS_QT_MD_OBSERVATORY_OBSERVATORY_WINDOW_H_
