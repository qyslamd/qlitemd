#ifndef FILE_CORE_COMMON_SINGLE_INSTANCE_H_
#define FILE_CORE_COMMON_SINGLE_INSTANCE_H_

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QSharedMemory>
#include <QStringList>

// 单实例检测。三重判据，按可靠性排序：
//   1. 命名互斥体（Windows）：进程异常退出时系统自动回收句柄，不会残留
//   2. 本地套接字：跨平台，且承载第二实例向首实例转发的命令行参数
//   3. 共享内存：兜底
class SingleInstance : public QObject {
  Q_OBJECT

 public:
  // appKey 用于本地套接字名与共享内存键。注意它同时是**进程外可见**的端点：
  // Windows 落在 \\.\pipe\<appKey>（命名管道），Linux 落在 <临时目录>/<appKey>
  // （unix socket，Qt 用 QDir::tempPath()，故受 TMPDIR 影响——探测方与被探测
  // 方必须在同一个 TMPDIR 下）。
  //
  // mutexName 为 Windows 命名互斥体名，由调用方传入而非内部按约定拼接：
  // 同一应用的所有入口必须使用同一个名字，两边各自推导会静默失配。
  // 传空串表示不创建互斥体，仅保留本地套接字与共享内存两条判据。
  explicit SingleInstance(const QString& appKey, const QString& mutexName,
                          QObject* parent = nullptr);
  ~SingleInstance() override;

  bool isAppAlreadyRunning();
  bool connectToExistingInstance(const QStringList& arguments = {});
  bool startListening();

  void setActivationWindow(void* window);
  void* activationWindow() const;

  void activateWindow();

 signals:
  void newInstanceLaunched(const QStringList& arguments);
  void showApplication();

 private slots:
  void handleNewConnection();
  void handleSocketReadyRead();
  void handleSocketError(QLocalSocket::LocalSocketError err);

 private:
  void sendMessageToExistingInstance(const QByteArray& message);
  QStringList parseMessage(const QByteArray& message);

  QString app_key_;
  void* activate_window_ = nullptr;
  QLocalServer* local_server_ = nullptr;
  QSharedMemory shared_memory_;
  QString server_name_;
  // 命名互斥体句柄（Windows HANDLE，其它平台恒为空）。用 void* 保存，
  // 避免在头文件中引入平台相关头文件
  void* mutex_handle_ = nullptr;
  // 构造时该互斥体已存在，说明本进程之前已有实例在运行
  bool mutex_already_existed_ = false;
  static const int CONNECTION_TIMEOUT = 1000;
};

#endif  // FILE_CORE_COMMON_SINGLE_INSTANCE_H_
