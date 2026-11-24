#ifndef FILE_APPS_QT_MD_VIEWER_READING_POSITION_CONTROLLER_H_
#define FILE_APPS_QT_MD_VIEWER_READING_POSITION_CONTROLLER_H_

#include <QString>

#include "reading_positions.h"

struct MdViewerSettings;

// 阅读位置缓存与持久化；每个路径一条，LRU 淘汰。
class ReadingPositionController {
 public:
  explicit ReadingPositionController(MdViewerSettings* settings);

  // 从 settings_->positions_json 读入
  void Load();
  // 不存在返回 -1
  int ScrollFor(const QString& path) const;
  // 记录后立即落盘，调用点是关闭/切篇/退出
  void Remember(const QString& path, int scroll, qint64 now_seconds);

  void Save() const;

 private:
  MdViewerSettings* settings_ = nullptr;
  ReadingPositionStore store_;
};

#endif  // FILE_APPS_QT_MD_VIEWER_READING_POSITION_CONTROLLER_H_
