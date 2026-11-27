// драйвер для обеспечения подключения клавиатур monsgeek с веб приложением web.monsgeek.com под Linux
// в оригинале вебка общается с клавиатурой с помощью iot_driver.exe на 127.0.0.1:3814 но мы будем пересылать команды с
// вебки в hidraw
//
// monsgeek-bridge - запустить
// monsgeek-bridge --probe - выполнить поиск клавиатуры и вывести номера их моделей

#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "config.hpp"
#include "db/store.hpp"
#include "hid/discovery.hpp"
#include "grpc_web/server.hpp"
#include "hid/keyboard_io.hpp"
#include "service/service.hpp"
#include "devices/udev_monitor.hpp"
#include "devices/device_manager.hpp"

static const char kSysClassHidraw[] = "/sys/class/hidraw";

// директория из переменной XDG_...
// если переменной нет то берем директорию из домашней директории пользователя
static std::string xdgDir(const char *xdgVar, const char *fallback)
{
  const char *xdg = getenv(xdgVar);

  if (xdg && *xdg)
  {
    return xdg;
  }

  const char *home = getenv("HOME");
  return std::string(home ? home : ".") + "/" + fallback;
}

static std::vector<FoundKeyboard> findKeyboards()
{
  return matchKeyboards(scanHidraw(kSysClassHidraw));
}

static std::unique_ptr<FeatureChannel> openKeyboard(const std::string &path)
{
  std::string error;
  auto channel = HidrawChannel::open(path, error);

  if (!channel)
  {
    std::cerr << error << std::endl;
  }

  return channel;
}

static int probe()
{
  auto keyboards = findKeyboards();

  if (keyboards.empty())
  {
    std::cout << "no monsgeek keyboards were found" << std::endl;
    return 1;
  }

  int rc = 0;

  for (const auto &kb : keyboards)
  {
    char ids[16];
    snprintf(ids, sizeof(ids), "%04x:%04x", kb.vid, kb.pid);
    std::string error;
    auto channel = HidrawChannel::open(kb.featurePath, error);

    if (!channel)
    {
      std::cout << kb.featurePath << " " << ids << " " << error << std::endl;
      rc = 1;
      continue;
    }

    KeyboardIo io(std::move(channel));
    auto id = io.queryModelId();
    std::cout << kb.featurePath << " " << ids << " id " << (id ? std::to_string(*id) : "?") << " (events on "
              << (kb.venderPath.empty() ? "no" : kb.venderPath) << ")" << std::endl;
  }

  return rc;
}

static int run()
{
  Config config;
  Store store(xdgDir("XDG_DATA_HOME", ".local/share") + "/monsgeek-bridge");

  // SIGINT и SIGTERM вылавливаем в главном потоке
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &signals, nullptr);

  DeviceManager devices(findKeyboards, openKeyboard, true);
  devices.rescan();

  for (const auto &kb : devices.list())
  {
    std::cout << "keyboard: " << kb.path << " id: " << kb.id << std::endl;
  }

  UdevMonitor monitor([&devices] { devices.rescan(); });

  if (!monitor.ok())
  {
    std::cerr << "udev is not available, so the driver will not recognize a keyboard connected after startup"
              << std::endl;
  }

  Service service(devices, store, config.version);
  GrpcWebServer server(service, config);

  if (!server.start())
  {
    std::cerr << "port " << config.port << " is in use - is the driver already running?" << std::endl;
    return 1;
  }

  std::cout << "listening on http://" << config.host << ":" << server.port() << std::endl;

  int sig = 0;
  sigwait(&signals, &sig);
  std::cout << "stopping" << std::endl;
  server.stop();
  monitor.stop();
  return 0;
}

int main(int argc, char **argv)
{
  for (int i = 1; i < argc; ++i)
  {
    if (strcmp(argv[i], "--probe") == 0)
    {
      return probe();
    }

    std::cerr << "usage: monsgeek-bridge [--probe]" << std::endl;
    return 2;
  }

  return run();
}
