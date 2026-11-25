#ifndef FILE_APPS_QT_MD_VIEWER_READING_POSITIONS_H_
#define FILE_APPS_QT_MD_VIEWER_READING_POSITIONS_H_

#include <QString>
#include <QVector>

struct ReadingPosition {
  QString path;
  int scroll = 0;
  qint64 touched = 0;
};

// 按绝对路径记住阅读位置，最多 capacity 条，LRU 淘汰。
class ReadingPositionStore {
 public:
  static ReadingPositionStore FromJson(const QString& json, int capacity = 500);
  QString ToJson() const;
  // 不存在返回 -1
  int ScrollFor(const QString& path) const;
  void Update(const QString& path, int scroll, qint64 now_seconds);
  int size() const;

 private:
  QVector<ReadingPosition> items_;
  int capacity_ = 500;
};

struct MdViewerSettings;

// 阅读位置缓存 + positions_json 的读写。
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

#endif  // FILE_APPS_QT_MD_VIEWER_READING_POSITIONS_H_
