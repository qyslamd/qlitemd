#ifndef FILE_APPS_QT_MD_OBSERVATORY_DOCUMENT_RAIL_H_
#define FILE_APPS_QT_MD_OBSERVATORY_DOCUMENT_RAIL_H_

#include <QVector>
#include <QWidget>

#include "instrument_math.h"
#include "markdown_types.h"

class QLabel;
class ProgressBar;
class QVBoxLayout;
class QScrollArea;
class QToolButton;
class CompositionBar;
class SpineWidget;

// Right rail: progress, composition and the section spine. Everything here
// is derived from the block list and outline; the rail never parses HTML.
class DocumentRail : public QWidget {
  Q_OBJECT

 public:
  explicit DocumentRail(QWidget* parent = nullptr);

  void Clear();
  void SetDocument(const QString& title, const QVector<MarkdownBlock>& blocks,
                   const QVector<MarkdownHeading>& outline,
                   const QVector<int>& heading_positions);
  void SetScroll(int scroll, int document_height, int viewport_height);
  void ToggleSpine();

 signals:
  void anchorActivated(const QString& anchor);

 private:
  void RebuildLegend();
  void UpdateCurrentSection();

  QLabel* document_title_ = nullptr;
  QLabel* percent_label_ = nullptr;
  ProgressBar* progress_bar_ = nullptr;
  CompositionBar* composition_bar_ = nullptr;
  QVBoxLayout* legend_layout_ = nullptr;
  QToolButton* spine_toggle_ = nullptr;
  QScrollArea* spine_scroll_ = nullptr;
  QWidget* spine_tail_ = nullptr;
  SpineWidget* spine_ = nullptr;
  QLabel* empty_label_ = nullptr;

  InstrumentData data_;
  int scroll_ = 0;
  int document_height_ = 0;
  int viewport_height_ = 0;
  int current_section_ = -1;
};

#endif  // FILE_APPS_QT_MD_OBSERVATORY_DOCUMENT_RAIL_H_
