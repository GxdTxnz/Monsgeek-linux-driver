#pragma once

// ищем клавиатуру monsgeek в /sys/class/hidraw

#include <string>
#include <vector>
#include <cstdint>

// один /dev/hidrawN и вся инфа о нем из sysfs
struct HidrawNode
{
  std::string devPath;
  // имя USB-устройства без номера интерфейса (у всех каналов одной клавиатуры одинаковое имя)
  std::string usbDevice;
  uint16_t vid;
  uint16_t pid;
  int interfaceNumber;
  std::vector<uint8_t> descriptor;
};

// sysClassHidraw обычно /sys/class/hidraw
std::vector<HidrawNode> scanHidraw(const std::string &sysClassHidraw);

struct FoundKeyboard
{
  // канал для запросов (команд) и ответов
  std::string featurePath;
  // канал по которому клавиатура шлет сообщения
  std::string venderPath;
  uint16_t vid;
  uint16_t pid;
};

std::vector<FoundKeyboard> matchKeyboards(const std::vector<HidrawNode> &nodes);
