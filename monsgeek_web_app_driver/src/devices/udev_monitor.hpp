#pragma once

// мониторим подключения/отключения hidraw-устройств и вызываем onChange

#include <thread>
#include <functional>

class UdevMonitor
{
public:
  explicit UdevMonitor(std::function<void()> onChange);

  ~UdevMonitor();

  // false если udev недоступен
  // в таком случае клавиатуру увидим только при старте драйвера
  bool ok() const;
  void stop();

private:
  void run();

  std::function<void()> onChange_;
  struct udev *udev_ = nullptr;
  struct udev_monitor *monitor_ = nullptr;
  int wakeFd_ = -1;
  std::thread thread_;
};
