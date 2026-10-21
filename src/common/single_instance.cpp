// clang-format off
// windows.h 需先于其它头文件；<string> 用于构造宽字符互斥体名
#ifdef _WIN32
#include <windows.h>
#include <string>
#pragma comment(lib, "user32.lib")
#endif
// clang-format on
#include "single_instance.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include "logging/log_core.h"

SingleInstance::SingleInstance(const QString& appKey, const QString& mutexName,
                               QObject* parent)
    : QObject(parent), app_key_(appKey), shared_memory_(appKey) {
  server_name_ =
      QString("%1-%2").arg(appKey).arg(QCoreApplication::applicationPid());

#ifdef _WIN32
  // 命名互斥体，进程存活期间持有。相比共享内存，进程异常退出时系统会
  // 回收句柄，不残留。
  if (!mutexName.isEmpty()) {
    const std::wstring mutex_name_w = mutexName.toStdWString();
    mutex_handle_ = CreateMutexW(nullptr, FALSE, mutex_name_w.c_str());
    if (mutex_handle_ != nullptr) {
      // GetLastError 须紧接 CreateMutexW 读取，中间不能插入其它 API 调用
      mutex_already_existed_ = (GetLastError() == ERROR_ALREADY_EXISTS);
    } else {
      LOG_WARN("SingleInstance", "创建命名互斥体失败，错误码: {}",
               static_cast<unsigned long>(GetLastError()));
    }
  }
#endif
}

SingleInstance::~SingleInstance() {
  if (local_server_ && local_server_->isListening()) {
    local_server_->close();
  }
  delete local_server_;

  if (shared_memory_.isAttached()) {
    shared_memory_.detach();
  }

#ifdef _WIN32
  if (mutex_handle_ != nullptr) {
    CloseHandle(reinterpret_cast<HANDLE>(mutex_handle_));
    mutex_handle_ = nullptr;
  }
#endif
}

bool SingleInstance::isAppAlreadyRunning() {
  // 互斥体优先：判据最可靠，且不会因共享内存残留而误报
  if (mutex_already_existed_) {
    LOG_INFO("SingleInstance", "检测到已有实例运行 (Mutex)");
    return true;
  }

  QLocalSocket socket;
  socket.connectToServer(app_key_, QIODevice::ReadWrite);
  if (socket.waitForConnected(CONNECTION_TIMEOUT)) {
    LOG_INFO("SingleInstance", "检测到已有实例运行 (LocalSocket)");
    socket.disconnectFromServer();
    return true;
  }

  if (shared_memory_.attach()) {
    LOG_INFO("SingleInstance", "检测到已有实例运行 (SharedMemory)");
    shared_memory_.detach();
    return true;
  }
  LOG_DEBUG("SingleInstance", "未检测到其他实例");
  return false;
}

bool SingleInstance::connectToExistingInstance(const QStringList& arguments) {
  QLocalSocket socket;
  QElapsedTimer deadline;
  deadline.start();
  // 首实例可能刚建好互斥体、还没开始监听；双击多个文件时第二个进程正好
  // 撞进这个窗口。短间隔重试到原有超时，避免参数静默丢失
  while (true) {
    socket.abort();
    socket.connectToServer(app_key_, QIODevice::WriteOnly);
    if (socket.waitForConnected(100)) {
      break;
    }
    if (deadline.elapsed() >= CONNECTION_TIMEOUT) {
      LOG_WARN("SingleInstance", "连接到已有实例失败");
      return false;
    }
    QThread::msleep(20);
  }

  LOG_INFO("SingleInstance", "转发参数到已有实例, 参数数: {}",
           arguments.size());

  QByteArray message;
  QDataStream stream(&message, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_5_12);

  stream << (quint32)1;
  stream << (quint32)arguments.size();
  for (const auto& arg : arguments) {
    stream << arg;
  }
  qint64 bytesWritten = socket.write(message);
  socket.flush();
  socket.waitForBytesWritten(CONNECTION_TIMEOUT);
  socket.disconnectFromServer();
  LOG_INFO("SingleInstance", "参数转发完成, 发送字节数: {}", bytesWritten);
  return bytesWritten > 0;
}

bool SingleInstance::startListening() {
  QLocalServer::removeServer(app_key_);

  local_server_ = new QLocalServer(this);
  connect(local_server_, &QLocalServer::newConnection, this,
          &SingleInstance::handleNewConnection);

  if (!local_server_->listen(app_key_)) {
    LOG_WARN("SingleInstance", "无法启动本地服务: {}",
             local_server_->errorString().toStdString());
    if (local_server_->serverError() == QAbstractSocket::AddressInUseError) {
      QLocalServer::removeServer(app_key_);
      if (!local_server_->listen(app_key_)) {
        LOG_ERROR("SingleInstance", "重试监听失败: {}",
                  local_server_->errorString().toStdString());
        return false;
      }
    } else {
      return false;
    }
  }

  if (!shared_memory_.create(1)) {
    LOG_WARN("SingleInstance", "无法创建共享内存，但本地服务已启动: {}",
             shared_memory_.errorString().toStdString());
  }

  LOG_INFO("SingleInstance", "单实例服务器启动成功");
  return true;
}

void SingleInstance::setActivationWindow(void* window) {
  activate_window_ = window;
}

void* SingleInstance::activationWindow() const { return activate_window_; }

void SingleInstance::handleNewConnection() {
  auto* socket = local_server_->nextPendingConnection();
  if (socket) {
    LOG_INFO("SingleInstance", "收到新实例连接请求");
    connect(socket, &QLocalSocket::readyRead, this,
            &SingleInstance::handleSocketReadyRead);
    connect(socket, &QLocalSocket::disconnected, socket,
            &QLocalSocket::deleteLater);
    connect(socket,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            &QLocalSocket::errorOccurred,
#else
            QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error),
#endif
            this, &SingleInstance::handleSocketError);
  }
}

void SingleInstance::handleSocketReadyRead() {
  auto* socket = qobject_cast<QLocalSocket*>(QObject::sender());
  if (!socket) {
    return;
  }
  auto data = socket->readAll();
  QStringList arguments = parseMessage(data);
  if (!arguments.isEmpty()) {
    LOG_INFO("SingleInstance", "接收到参数, 参数数: {}, 首参数: {}",
             arguments.size(),
             arguments.isEmpty() ? "" : arguments.first().toStdString());
    emit newInstanceLaunched(arguments);
    activateWindow();
  }
}

void SingleInstance::handleSocketError(QLocalSocket::LocalSocketError errCode) {
  auto* socket = qobject_cast<QLocalSocket*>(QObject::sender());
  if (socket) {
    LOG_WARN("SingleInstance", "Socket错误: {}, 错误码: {}",
             socket->errorString().toStdString(), (int)errCode);
  }
}

QStringList SingleInstance::parseMessage(const QByteArray& message) {
  QStringList arguments;
  QDataStream stream(message);
  stream.setVersion(QDataStream::Qt_5_12);

  quint32 protocolVersion = 0;
  quint32 argCount = 0;
  stream >> protocolVersion;
  if (protocolVersion != 1) {
    LOG_WARN("SingleInstance", "不支持的协议版本: {}", protocolVersion);
    return arguments;
  }
  stream >> argCount;
  for (quint32 i = 0; i < argCount; i++) {
    QString arg;
    stream >> arg;
    arguments.append(arg);
  }
  return arguments;
}

void SingleInstance::activateWindow() {
  if (!activate_window_) {
    LOG_WARN("SingleInstance", "激活窗口为空，发出 showApplication 信号");
    emit showApplication();
    return;
  }
  LOG_INFO("SingleInstance", "激活现有窗口");
#ifdef _WIN32
  HWND hwnd = reinterpret_cast<HWND>(activate_window_);
  if (IsIconic(hwnd)) {
    LOG_INFO("SingleInstance", "窗口已最小化，执行恢复");
    ShowWindow(hwnd, SW_RESTORE);
  }
  SetForegroundWindow(hwnd);
  BringWindowToTop(hwnd);
  SetActiveWindow(hwnd);
#endif

  // 各平台统一发一次信号，由使用方连接完成 Qt 层的显示与激活
  emit showApplication();
}
