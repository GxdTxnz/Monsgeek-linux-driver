#include <cerrno>
#include <chrono>
#include <poll.h>
#include <unistd.h>
#include <libudev.h>
#include <sys/eventfd.h>
#include "devices/udev_monitor.hpp"

// при подключении клавиатуры udev присылает три события подряд (по одному на каждый hidraw)
// ждем 300 мс пока события перестанут идти и потом перепроверяем список
// каждое новое событие обнуляет счетчик
static const int kSettleMs = 300;

UdevMonitor::UdevMonitor(std::function<void()> onChange) : onChange_(std::move(onChange))
{
  udev_ = udev_new();

  if (!udev_)
  {
    return;
  }

  monitor_ = udev_monitor_new_from_netlink(udev_, "udev");

  if (!monitor_ || udev_monitor_filter_add_match_subsystem_devtype(monitor_, "hidraw", nullptr) < 0 ||
      udev_monitor_enable_receiving(monitor_) < 0)
  {
    return;
  }

  wakeFd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  thread_ = std::thread([this] { run(); });
}

UdevMonitor::~UdevMonitor()
{
  stop();

  if (monitor_)
  {
    udev_monitor_unref(monitor_);
  }

  if (udev_)
  {
    udev_unref(udev_);
  }
}

bool UdevMonitor::ok() const
{
  return thread_.joinable();
}

void UdevMonitor::stop()
{
  if (thread_.joinable())
  {
    uint64_t one = 1;
    ssize_t rc = write(wakeFd_, &one, sizeof(one));
    (void)rc;
    thread_.join();
  }

  if (wakeFd_ >= 0)
  {
    close(wakeFd_);
    wakeFd_ = -1;
  }
}

void UdevMonitor::run()
{
  pollfd fds[2] = {{udev_monitor_get_fd(monitor_), POLLIN, 0}, {wakeFd_, POLLIN, 0}};
  bool pending = false;

  while (true)
  {
    int rc = poll(fds, 2, pending ? kSettleMs : -1);

    if (rc < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }

      return;
    }

    if (fds[1].revents != 0)
    {
      return;
    }

    if (rc == 0)
    {
      pending = false;
      onChange_();
      continue;
    }

    if (fds[0].revents & POLLIN)
    {
      struct udev_device *dev = udev_monitor_receive_device(monitor_);

      if (dev)
      {
        udev_device_unref(dev);
      }

      pending = true;
    }
  }
}
