#include <cstdio>
#include <fstream>
#include <sstream>
#include <iterator>
#include <filesystem>
#include "hid/discovery.hpp"
#include "hid/descriptor.hpp"
#include "hid/support_table.hpp"

namespace fs = std::filesystem;

// канал сообщений по которому клавиатура отправляет события чтобы мы их переслали в вебку
// ищем по признакам "USB-интерфейс 1 и тип 0xFFFF + 1"
static const int kVenderInterface = 1;
static const uint16_t kVenderUsagePage = 0xFFFF;
static const uint16_t kVenderUsage = 1;

static std::string readFile(const fs::path &path)
{
  std::ifstream f(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

// строка вида HID_ID=0003:00003151:00005030 (шина + vid + pid в hex)
static bool parseHidId(const std::string &uevent, uint16_t &vid, uint16_t &pid)
{
  std::istringstream in(uevent);
  std::string line;

  while (std::getline(in, line))
  {
    if (line.rfind("HID_ID=", 0) != 0)
    {
      continue;
    }

    unsigned bus = 0;
    unsigned v = 0;
    unsigned p = 0;

    if (sscanf(line.c_str(), "HID_ID=%x:%x:%x", &bus, &v, &p) != 3)
    {
      return false;
    }

    vid = static_cast<uint16_t>(v);
    pid = static_cast<uint16_t>(p);
    return true;
  }

  return false;
}

// директория USB-интерфейса называется 1-10:1.2 - устройство 1-10, конфигурация 1, интерфейс 2
static bool parseInterfaceDir(const std::string &name, std::string &usbDevice, int &interfaceNumber)
{
  size_t colon = name.find(':');
  size_t dot = name.rfind('.');

  if (colon == std::string::npos || dot == std::string::npos || dot < colon)
  {
    return false;
  }

  try
  {
    interfaceNumber = std::stoi(name.substr(dot + 1));
  }

  catch (const std::exception &)
  {
    return false;
  }

  usbDevice = name.substr(0, colon);
  return true;
}

std::vector<HidrawNode> scanHidraw(const std::string &sysClassHidraw)
{
  std::vector<HidrawNode> nodes;
  std::error_code ec;

  for (const auto &entry : fs::directory_iterator(sysClassHidraw, ec))
  {
    std::string name = entry.path().filename().string();

    if (name.rfind("hidraw", 0) != 0)
    {
      continue;
    }

    fs::path hidDevice = fs::canonical(entry.path() / "device", ec);

    if (ec)
    {
      ec.clear();
      continue;
    }

    HidrawNode node;
    node.devPath = "/dev/" + name;

    if (!parseHidId(readFile(hidDevice / "uevent"), node.vid, node.pid))
    {
      continue;
    }

    if (!parseInterfaceDir(hidDevice.parent_path().filename().string(), node.usbDevice, node.interfaceNumber))
    {
      continue;
    }

    std::string descriptor = readFile(hidDevice / "report_descriptor");
    node.descriptor.assign(descriptor.begin(), descriptor.end());
    nodes.push_back(std::move(node));
  }

  return nodes;
}

static bool isFeatureChannel(const HidrawNode &node)
{
  for (const auto &e : supportTable())
  {
    if (e.vid != node.vid || e.pid != node.pid)
    {
      continue;
    }

    if (e.interfaceNumber != -1 && e.interfaceNumber != node.interfaceNumber)
    {
      continue;
    }

    if (hasCollection(node.descriptor, e.usagePage, e.usage))
    {
      return true;
    }
  }

  return false;
}

std::vector<FoundKeyboard> matchKeyboards(const std::vector<HidrawNode> &nodes)
{
  std::vector<FoundKeyboard> found;

  for (const auto &node : nodes)
  {
    if (!isFeatureChannel(node))
    {
      continue;
    }

    FoundKeyboard kb{node.devPath, "", node.vid, node.pid};

    for (const auto &other : nodes)
    {
      if (other.usbDevice == node.usbDevice && other.interfaceNumber == kVenderInterface &&
          hasCollection(other.descriptor, kVenderUsagePage, kVenderUsage))
      {
        kb.venderPath = other.devPath;
      }
    }

    found.push_back(kb);
  }

  return found;
}
