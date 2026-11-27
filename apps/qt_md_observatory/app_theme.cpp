#include "app_theme.h"

#include <QColor>

namespace {

struct Colors {
  const char* background;
  const char* surface;
  const char* surface_alt;
  const char* border;
  const char* text;
  const char* muted;
  const char* accent;
  const char* accent_soft;
  const char* selection;
};

Colors ColorsFor(bool dark) {
  if (dark) {
    return {
        "#161719",
        "#1d1e21",
        "#232428",
        "#2b2c31",
        "#e7e6e2",
        "#97968f",
        "#d97757",
        "rgba(217, 119, 87, 34)",
        "rgba(217, 119, 87, 46)",
    };
  }
  return {
      "#f5f4f0",
      "#fbfaf7",
      "#efeee9",
      "#dddcd5",
      "#1f1f1d",
      "#6e6d66",
      "#b85c3e",
      "rgba(184, 92, 62, 30)",
      "rgba(184, 92, 62, 40)",
  };
}

}  // namespace

QPalette AppPalette(bool dark) {
  const Colors colors = ColorsFor(dark);
  QPalette palette;
  palette.setColor(QPalette::Window, QColor(colors.background));
  palette.setColor(QPalette::Base, QColor(colors.surface));
  palette.setColor(QPalette::AlternateBase, QColor(colors.surface_alt));
  palette.setColor(QPalette::Text, QColor(colors.text));
  palette.setColor(QPalette::WindowText, QColor(colors.text));
  palette.setColor(QPalette::ButtonText, QColor(colors.text));
  palette.setColor(QPalette::Button, QColor(colors.surface_alt));
  palette.setColor(QPalette::Highlight, QColor(colors.accent));
  palette.setColor(QPalette::HighlightedText, QColor(colors.text));
  palette.setColor(QPalette::ToolTipBase, QColor(colors.surface_alt));
  palette.setColor(QPalette::ToolTipText, QColor(colors.text));
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor(colors.muted));
  palette.setColor(QPalette::Disabled, QPalette::WindowText,
                   QColor(colors.muted));
  return palette;
}

QString AppStyleSheet(bool dark) {
  const Colors colors = ColorsFor(dark);
  QString sheet = QStringLiteral(R"(
QWidget#observatoryRoot {
  background: @background;
  color: @text;
}
QWidget#titleBar {
  background: @surface;
  border-bottom: 1px solid @border;
}
QLabel#appTitle {
  color: @text;
  font-weight: 600;
  font-size: 12px;
  letter-spacing: 1px;
}
QLabel#rootLabel, QLabel#muted, QLabel#sectionHint {
  color: @muted;
}
QLabel#breadcrumb {
  color: @muted;
  font-size: 12px;
}
QLabel#docIndex {
  color: @muted;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 1px;
}
QWidget#workspacePanel, QWidget#railPanel {
  background: @surface;
  border: none;
}
QLabel#panelTitle {
  color: @muted;
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 2px;
}
QLabel#workspaceOverall {
  color: @text;
  font-size: 22px;
  font-weight: 300;
}
QLabel#railPercent {
  color: @text;
  font-size: 26px;
  font-weight: 300;
}
QScrollArea {
  background: transparent;
  border: none;
}
QScrollArea > QWidget > QWidget {
  background: transparent;
}
QTreeWidget#workspaceTree {
  background: transparent;
  border: none;
  outline: 0;
  color: @text;
  font-size: 12px;
}
QTreeWidget#workspaceTree::item {
  height: 28px;
  border-radius: 4px;
}
QTreeWidget#workspaceTree::item:hover {
  background: @surface_alt;
}
QTreeWidget#workspaceTree::item:selected {
  background: @selection;
  color: @text;
}
QLineEdit {
  background: @surface_alt;
  border: 1px solid @border;
  border-radius: 5px;
  padding: 4px 8px;
  color: @text;
  selection-background-color: @accent;
}
QPushButton {
  background: @surface_alt;
  border: 1px solid @border;
  border-radius: 5px;
  padding: 5px 12px;
  color: @text;
}
QPushButton#paletteButton {
  padding: 2px 10px;
  font-size: 11px;
  color: @muted;
}
QToolButton#panelTitleButton {
  color: @muted;
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 2px;
  border: none;
  background: transparent;
  padding: 0;
}
QWidget#paletteCard {
  background: @surface_alt;
  border: 1px solid @border;
  border-radius: 10px;
}
QLineEdit#paletteInput {
  background: transparent;
  border: none;
  border-bottom: 1px solid @border;
  border-radius: 0;
  padding: 10px 12px;
  font-size: 14px;
}
QListWidget#paletteList {
  background: transparent;
  border: none;
  outline: 0;
  color: @text;
}
QListWidget#paletteList::item {
  height: 30px;
  padding-left: 10px;
  border-radius: 4px;
}
QListWidget#paletteList::item:selected {
  background: @selection;
}
QPushButton:hover {
  border-color: @accent;
}
QPushButton:pressed {
  background: @selection;
}
QWidget#resumeBar {
  background: @accent_soft;
  border-bottom: 1px solid @border;
}
QLabel#resumeText {
  color: @text;
  font-size: 12px;
}
QSplitter::handle {
  background: @border;
}
QSplitter::handle:horizontal {
  width: 1px;
}
QScrollBar:vertical {
  background: transparent;
  width: 10px;
  margin: 0;
}
QScrollBar::handle:vertical {
  background: @border;
  min-height: 28px;
  border-radius: 5px;
}
QScrollBar::handle:vertical:hover {
  background: @muted;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
  height: 0;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
  background: transparent;
}
QProgressBar {
  background: @surface_alt;
  border: none;
  border-radius: 2px;
  height: 4px;
  text-align: center;
}
QProgressBar::chunk {
  background: @accent;
  border-radius: 2px;
}
)");
  struct Token {
    const char* name;
    const char* value;
  };
  const Token tokens[] = {
      {"@background", colors.background},
      {"@surface", colors.surface},
      {"@surface_alt", colors.surface_alt},
      {"@border", colors.border},
      {"@text", colors.text},
      {"@muted", colors.muted},
      {"@accent", colors.accent},
      {"@accent_soft", colors.accent_soft},
      {"@selection", colors.selection},
  };
  for (const Token& token : tokens) {
    sheet.replace(QLatin1String(token.name), QLatin1String(token.value));
  }
  return sheet;
}
