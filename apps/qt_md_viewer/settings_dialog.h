#ifndef FILE_APPS_QT_MD_VIEWER_SETTINGS_DIALOG_H_
#define FILE_APPS_QT_MD_VIEWER_SETTINGS_DIALOG_H_

#include <QDialog>

// 设置对话框：托管渲染设置面板。模型窗口，改动即时生效，没有
// 应用/取消按钮；关闭只是隐藏，下次打开保留滚动位置。
class SettingsDialog : public QDialog {
  Q_OBJECT

 public:
  explicit SettingsDialog(QWidget* content, QWidget* parent = nullptr);
};

#endif  // FILE_APPS_QT_MD_VIEWER_SETTINGS_DIALOG_H_
