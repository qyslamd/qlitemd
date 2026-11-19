#ifndef FILE_EXAMPLES_MD_VIEWER_RENDER_STATUS_PANEL_H_
#define FILE_EXAMPLES_MD_VIEWER_RENDER_STATUS_PANEL_H_

#include <array>

#include <QWidget>

class ExExpander;
class ExProgressRing;
class HitBar;
class MarkdownView;
class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;
class StatusPill;
class WorkerPips;

// 渲染状态面板：进度、阶段耗时、worker 池、缓存、引擎命中与请求历史。
// 只读展示 + 刷新/清零；文档重建时自动刷新（跟着 outlineChanged 走）
class RenderStatusPanel : public QWidget {
  Q_OBJECT

 public:
  explicit RenderStatusPanel(MarkdownView* view, QWidget* parent = nullptr);

 signals:
  void statusMessage(const QString& message);

 private:
  struct PoolWidgets {
    WorkerPips* pips = nullptr;
    QLabel* workers = nullptr;
    QLabel* meta = nullptr;
  };
  struct EngineWidgets {
    StatusPill* pill = nullptr;
    HitBar* bar = nullptr;
    QLabel* sub = nullptr;
    QLabel* detail = nullptr;
  };

  void initUi();
  void initSignals();
  void RefreshStatus();
  // 抽屉展开/收起后把内容高度同步给滚动区
  void UpdateScrollContentHeight();
  void TrackGroup(ExExpander* group);

  MarkdownView* view_ = nullptr;
  QWidget* scroll_body_ = nullptr;
  QVBoxLayout* scroll_body_layout_ = nullptr;
  QPushButton* refresh_button_ = nullptr;
  QPushButton* reset_button_ = nullptr;

  QLabel* progress_hint_ = nullptr;
  ExProgressRing* progress_ring_ = nullptr;
  QLabel* progress_percent_ = nullptr;
  QLabel* progress_meta_ = nullptr;
  QLabel* progress_chips_ = nullptr;

  QLabel* timing_hint_ = nullptr;
  std::array<QProgressBar*, 6> stage_bars_{};
  std::array<QLabel*, 6> stage_values_{};

  QLabel* pools_hint_ = nullptr;
  std::array<PoolWidgets, 3> pools_{};

  QLabel* engines_hint_ = nullptr;
  std::array<EngineWidgets, 3> engines_{};

  QLabel* cache_hint_ = nullptr;
  std::array<QProgressBar*, 4> cache_bars_{};
  std::array<QLabel*, 4> cache_values_{};
  QProgressBar* image_cap_bar_ = nullptr;
  QLabel* image_cap_value_ = nullptr;
  QLabel* detail_cache_ = nullptr;
  QLabel* detail_limits_ = nullptr;
  QLabel* detail_memory_ = nullptr;
  QLabel* detail_diagnostics_ = nullptr;

  QLabel* doc_hint_ = nullptr;
  std::array<QLabel*, 4> doc_values_{};
  std::array<QProgressBar*, 2> char_bars_{};
  std::array<QLabel*, 2> char_values_{};

  QLabel* history_hint_ = nullptr;
  std::array<QLabel*, 5> history_values_{};

  quint64 progress_request_ = 0;
  int progress_completed_ = 0;
  int progress_total_ = 0;
  int progress_in_flight_ = 0;
  quint64 finished_request_ = 0;
};

#endif  // FILE_EXAMPLES_MD_VIEWER_RENDER_STATUS_PANEL_H_
