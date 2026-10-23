#ifndef FILE_EXAMPLES_MD_VIEWER_QUICK_OPEN_H_
#define FILE_EXAMPLES_MD_VIEWER_QUICK_OPEN_H_

#include <QMetaType>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;

// 快速跳转条目。标题/副标题/路径都由宿主填充，弹出层不关心来源。
enum class QuickOpenKind {
  kHeading,
  kFile,
  kRecent,
  kCommand,
};

struct QuickOpenItem {
  QuickOpenKind kind = QuickOpenKind::kFile;
  QString title;
  QString subtitle;
  // 文件/标题：绝对路径；命令：command_id 非空，path 为空
  QString path;
  QString anchor;
  QString command_id;
};

Q_DECLARE_METATYPE(QuickOpenItem)

// 大小写不敏感的子序列匹配。0 = 不匹配；分数越高越靠前。
// 连续字符、词首字符和精确子串会加分，短标题略占优。
int QuickOpenScore(const QString& text, const QString& query);

// 空查询保持调用方给的顺序，并截到 max_results；非空查询先过滤再
// 按分数稳定排序，标题命中权重高于副标题。
QVector<QuickOpenItem> FilterQuickOpenItems(const QVector<QuickOpenItem>& items,
                                            const QString& query,
                                            int max_results = 80);

// 居中的浮动跳转面板。键盘：输入筛选、↑/↓ 选择、Enter 执行、Esc 关闭。
// 列表项按行索引映射回 FilterQuickOpenItems 的结果，不依赖 QVariant 元类型。
class QuickOpenPopup : public QWidget {
  Q_OBJECT

 public:
  explicit QuickOpenPopup(QWidget* parent = nullptr);

  void Open(const QVector<QuickOpenItem>& items,
            const QString& query = QString());
  // 当前展示的条目数；测试与空态提示用
  int shown_count() const { return shown_items_.size(); }

 signals:
  void itemActivated(const QuickOpenItem& item);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void hideEvent(QHideEvent* event) override;

 private:
  void ApplyFilter();
  void ActivateCurrent();
  void MoveSelection(int delta);
  void PositionNearParent();

  QVector<QuickOpenItem> source_items_;
  QVector<QuickOpenItem> shown_items_;
  QLineEdit* query_edit_ = nullptr;
  QListWidget* list_ = nullptr;
  QLabel* empty_label_ = nullptr;
  QLabel* count_label_ = nullptr;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_QUICK_OPEN_H_
