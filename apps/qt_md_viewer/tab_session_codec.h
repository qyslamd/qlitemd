#ifndef FILE_APPS_QT_MD_VIEWER_TAB_SESSION_CODEC_H_
#define FILE_APPS_QT_MD_VIEWER_TAB_SESSION_CODEC_H_

#include <QByteArray>
#include <QString>
#include <QStringList>

// 标签顺序与激活下标的纯数据模型。第 0 项固定是欢迎标签（路径为空），
// 不可关闭、不可拖动；其余项是已打开的文档路径，路径唯一。
class TabSessionCodec {
 public:
  TabSessionCodec();

  // 空路径返回欢迎标签；已存在返回现有下标；否则追加并返回新下标。
  int Add(const QString& path);
  // 欢迎标签与越界下标返回 false。
  bool Remove(int index);
  bool SetActive(int index);
  // 只重排文档标签；涉及欢迎标签、越界时返回 false。
  bool Move(int from, int to);

  int active() const { return active_; }
  int count() const { return paths_.size(); }
  QString path_at(int index) const;
  bool IsWelcome(int index) const { return index == 0; }

  QByteArray Serialize() const;
  bool Restore(const QByteArray& data);

 private:
  QStringList paths_;  // 第 0 项恒为空
  int active_ = 0;
};

#endif  // FILE_APPS_QT_MD_VIEWER_TAB_SESSION_CODEC_H_
