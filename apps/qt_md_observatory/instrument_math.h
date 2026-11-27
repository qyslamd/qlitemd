#ifndef FILE_APPS_QT_MD_OBSERVATORY_INSTRUMENT_MATH_H_
#define FILE_APPS_QT_MD_OBSERVATORY_INSTRUMENT_MATH_H_

#include <QString>
#include <QVector>

#include "markdown_types.h"

// Coarse categories for the composition bar. Containers and their contents
// may both count; the bar is proportional, not a word count.
enum class BlockGroup {
  kHeading = 0,
  kText,
  kCode,
  kMath,
  kDiagram,
  kTable,
  kNote,
  kRule,
};

constexpr int kBlockGroupCount = 8;

BlockGroup GroupForBlock(MarkdownBlockKind kind);
bool IsCountedBlock(MarkdownBlockKind kind);
QString BlockGroupLabel(BlockGroup group);
// Hex string so this module stays QtCore-only and testable headless.
QString BlockGroupColorHex(BlockGroup group);

struct InstrumentSection {
  QString anchor;
  QString title;
  int level = 0;
  // Document y of the heading; -1 when the layout has not produced one.
  int top = -1;
  int block_count = 0;
  QVector<int> group_counts;
};

struct InstrumentData {
  QVector<int> composition;
  int total_blocks = 0;
  QVector<InstrumentSection> sections;
  int current_section = -1;
  int progress = 0;
};

// heading_positions is parallel to outline; -1 means unknown.
InstrumentData BuildInstrumentData(const QVector<MarkdownBlock>& blocks,
                                   const QVector<MarkdownHeading>& outline,
                                   const QVector<int>& heading_positions,
                                   int scroll, int document_height,
                                   int viewport_height);

int ComputeProgress(int scroll, int document_height, int viewport_height);

#endif  // FILE_APPS_QT_MD_OBSERVATORY_INSTRUMENT_MATH_H_
