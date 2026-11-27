#include <utility>
#include "hid/descriptor.hpp"

// HID-описание интерфейса это поток коротких записей типа [префикс][0, 1, 2 или 4 байта данных]
// в префиксе лежат: 2 младших бита - размер данных, следующие 2 - тип (main/global/local), 4 старших бита - что это
// ищем три записи: Usage Page (global), Usage (local) и Collection (main)

static const uint8_t kTypeMain = 0;
static const uint8_t kTypeGlobal = 1;
static const uint8_t kTypeLocal = 2;
static const uint8_t kTagUsagePage = 0x0;
static const uint8_t kTagUsage = 0x0;
static const uint8_t kTagCollection = 0xA;
static const uint8_t kLongItem = 0xFE;

bool hasCollection(const std::vector<uint8_t> &descriptor, uint16_t usagePage, uint16_t usage)
{
  uint32_t currentPage = 0;
  // usage и признак "записан 4 байтами" = в есть нем своя страница
  std::vector<std::pair<uint32_t, bool>> usages;
  size_t i = 0;

  while (i < descriptor.size())
  {
    uint8_t prefix = descriptor[i];

    if (prefix == kLongItem)
    {
      if (i + 1 >= descriptor.size())
      {
        return false;
      }

      i += 3 + descriptor[i + 1];
      continue;
    }

    size_t size = prefix & 0x3;

    if (size == 3)
    {
      size = 4;
    }

    if (i + 1 + size > descriptor.size())
    {
      return false;
    }

    uint32_t value = 0;

    for (size_t k = 0; k < size; ++k)
    {
      value |= static_cast<uint32_t>(descriptor[i + 1 + k]) << (8 * k);
    }

    uint8_t type = (prefix >> 2) & 0x3;
    uint8_t tag = prefix >> 4;

    if (type == kTypeGlobal && tag == kTagUsagePage)
    {
      currentPage = value;
    }

    else if (type == kTypeLocal && tag == kTagUsage)
    {
      usages.push_back({value, size == 4});
    }

    else if (type == kTypeMain)
    {
      if (tag == kTagCollection && !usages.empty())
      {
        auto [u, extended] = usages.front();
        // 4-байтовый usage несет страницу в 2х старших байтах
        uint32_t page = extended ? (u >> 16) : currentPage;

        if (page == usagePage && (u & 0xFFFF) == usage)
        {
          return true;
        }
      }

      usages.clear();
    }

    i += 1 + size;
  }

  return false;
}
