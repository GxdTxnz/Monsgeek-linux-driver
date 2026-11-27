#pragma once

// список клавиатур которые поддерживает драйвер + через какой hidraw слать команды
#include <vector>
#include <cstdint>

struct SupportEntry
{
  uint16_t vid;
  uint16_t pid;
  uint16_t usagePage;
  uint16_t usage;
  // -1 = любой интерфейс
  int interfaceNumber;
};

const std::vector<SupportEntry> &supportTable();
