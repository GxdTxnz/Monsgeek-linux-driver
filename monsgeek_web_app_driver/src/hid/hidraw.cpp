#include <cerrno>
#include <poll.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <linux/hidraw.h>
#include "hid/hidraw.hpp"

static const uint8_t kVenderReportId = 5;

static std::string errnoText(const std::string &what, const std::string &path)
{
  if (errno == EACCES)
  {
    return "no access to " + path + " - install the udev rule (make install)";
  }

  return what + " " + path + ": " + strerror(errno);
}

std::unique_ptr<HidrawChannel> HidrawChannel::open(const std::string &path, std::string &error)
{
  int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);

  if (fd < 0)
  {
    error = errnoText("could not open", path);
    return nullptr;
  }

  return std::unique_ptr<HidrawChannel>(new HidrawChannel(fd));
}

HidrawChannel::HidrawChannel(int fd) : fd_(fd) {}

HidrawChannel::~HidrawChannel()
{
  close(fd_);
}

bool HidrawChannel::sendFeature(const std::array<uint8_t, 65> &data, std::string &error)
{
  std::array<uint8_t, 65> buf = data;

  if (ioctl(fd_, HIDIOCSFEATURE(buf.size()), buf.data()) < 0)
  {
    error = std::string("send feature: ") + strerror(errno);
    return false;
  }

  return true;
}

bool HidrawChannel::getFeature(std::array<uint8_t, 65> &data, std::string &error)
{
  data.fill(0);

  // первый байт = номер нужного отчета
  // у служебного интерфейса номеров нет поэтому 0
  if (ioctl(fd_, HIDIOCGFEATURE(data.size()), data.data()) < 0)
  {
    error = std::string("get feature: ") + strerror(errno);
    return false;
  }

  return true;
}

VenderReader::VenderReader(const std::string &path, std::function<void(std::vector<uint8_t>)> onMessage)
    : onMessage_(std::move(onMessage))
{
  fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);

  if (fd_ < 0)
  {
    std::cerr << errnoText("unable to open event channel", path) << std::endl;
    return;
  }

  wakeFd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  thread_ = std::thread([this] { run(); });
}

VenderReader::~VenderReader()
{
  stop();
}

void VenderReader::stop()
{
  if (thread_.joinable())
  {
    uint64_t one = 1;
    ssize_t rc = write(wakeFd_, &one, sizeof(one));
    (void)rc;
    thread_.join();
  }

  if (fd_ >= 0)
  {
    close(fd_);
    fd_ = -1;
  }

  if (wakeFd_ >= 0)
  {
    close(wakeFd_);
    wakeFd_ = -1;
  }
}

void VenderReader::run()
{
  pollfd fds[2] = {{fd_, POLLIN, 0}, {wakeFd_, POLLIN, 0}};
  uint8_t buf[256];

  while (true)
  {
    if (poll(fds, 2, -1) < 0)
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

    // отключили клавиатуру -> читать неченго
    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
    {
      return;
    }

    // hidraw за одно чтение отдает один отчет
    ssize_t n = read(fd_, buf, sizeof(buf));

    if (n <= 0)
    {
      if (n < 0 && (errno == EAGAIN || errno == EINTR))
      {
        continue;
      }

      return;
    }

    if (buf[0] == kVenderReportId)
    {
      onMessage_(std::vector<uint8_t>(buf, buf + n));
    }
  }
}
