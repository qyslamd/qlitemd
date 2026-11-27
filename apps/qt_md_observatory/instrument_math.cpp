#include "instrument_math.h"

#include <QtGlobal>

BlockGroup GroupForBlock(MarkdownBlockKind kind) {
  switch (kind) {
    case MarkdownBlockKind::kHeading:
      return BlockGroup::kHeading;
    case MarkdownBlockKind::kParagraph:
    case MarkdownBlockKind::kBlockQuote:
    case MarkdownBlockKind::kListItem:
    case MarkdownBlockKind::kTaskItem:
      return BlockGroup::kText;
    case MarkdownBlockKind::kCodeBlock:
      return BlockGroup::kCode;
    case MarkdownBlockKind::kMathBlock:
      return BlockGroup::kMath;
    case MarkdownBlockKind::kMermaid:
    case MarkdownBlockKind::kImage:
      return BlockGroup::kDiagram;
    case MarkdownBlockKind::kTable:
      return BlockGroup::kTable;
    case MarkdownBlockKind::kAdmonition:
    case MarkdownBlockKind::kFootnoteDef:
      return BlockGroup::kNote;
    case MarkdownBlockKind::kList:
    case MarkdownBlockKind::kThematicBreak:
    case MarkdownBlockKind::kHtmlBlock:
    case MarkdownBlockKind::kOther:
      return BlockGroup::kRule;
  }
  return BlockGroup::kRule;
}

bool IsCountedBlock(MarkdownBlockKind kind) {
  // List containers would double-count their items; everything else is a
  // leaf or a container whose own type matters (quote, table, admonition).
  return kind != MarkdownBlockKind::kList;
}

QString BlockGroupLabel(BlockGroup group) {
  switch (group) {
    case BlockGroup::kHeading:
      return QStringLiteral("Headings");
    case BlockGroup::kText:
      return QStringLiteral("Text");
    case BlockGroup::kCode:
      return QStringLiteral("Code");
    case BlockGroup::kMath:
      return QStringLiteral("Math");
    case BlockGroup::kDiagram:
      return QStringLiteral("Diagrams");
    case BlockGroup::kTable:
      return QStringLiteral("Tables");
    case BlockGroup::kNote:
      return QStringLiteral("Notes");
    case BlockGroup::kRule:
      return QStringLiteral("Rules");
  }
  return QString();
}

QString BlockGroupColorHex(BlockGroup group) {
  switch (group) {
    case BlockGroup::kHeading:
      return QStringLiteral("#d97757");
    case BlockGroup::kText:
      return QStringLiteral("#708090");
    case BlockGroup::kCode:
      return QStringLiteral("#5b8a72");
    case BlockGroup::kMath:
      return QStringLiteral("#8a7bb8");
    case BlockGroup::kDiagram:
      return QStringLiteral("#4f8ba8");
    case BlockGroup::kTable:
      return QStringLiteral("#b08a4f");
    case BlockGroup::kNote:
      return QStringLiteral("#b5586a");
    case BlockGroup::kRule:
      return QStringLiteral("#6e7076");
  }
  return QStringLiteral("#6e7076");
}

int ComputeProgress(int scroll, int document_height, int viewport_height) {
  const int max_scroll = qMax(1, document_height - viewport_height);
  if (document_height <= viewport_height + 1) {
    return 100;
  }
  const qint64 value =
      static_cast<qint64>(qMax(0, scroll)) * 100 + max_scroll / 2;
  return qBound(0, static_cast<int>(value / max_scroll), 100);
}

InstrumentData BuildInstrumentData(const QVector<MarkdownBlock>& blocks,
                                   const QVector<MarkdownHeading>& outline,
                                   const QVector<int>& heading_positions,
                                   int scroll, int document_height,
                                   int viewport_height) {
  InstrumentData data;
  data.composition.fill(0, kBlockGroupCount);
  data.sections.clear();
  data.sections.reserve(outline.size());
  for (int i = 0; i < outline.size(); ++i) {
    InstrumentSection section;
    section.anchor = outline[i].anchor;
    section.title = outline[i].text;
    section.level = outline[i].level;
    section.group_counts.fill(0, kBlockGroupCount);
    if (i < heading_positions.size()) {
      section.top = heading_positions[i];
    }
    data.sections.push_back(section);
  }

  for (const MarkdownBlock& block : blocks) {
    if (!IsCountedBlock(block.kind)) {
      continue;
    }
    const int group = static_cast<int>(GroupForBlock(block.kind));
    if (group < 0 || group >= kBlockGroupCount) {
      continue;
    }
    ++data.composition[group];
    ++data.total_blocks;
    if (block.section >= 0 && block.section < data.sections.size()) {
      ++data.sections[block.section].group_counts[group];
      ++data.sections[block.section].block_count;
    }
  }

  for (int i = 0; i < data.sections.size(); ++i) {
    if (data.sections[i].top >= 0 && data.sections[i].top <= scroll + 24) {
      data.current_section = i;
    }
  }

  data.progress = ComputeProgress(scroll, document_height, viewport_height);
  return data;
}
