/*
тесты поиска клавиатуры
разбор описания HID
таблица из iot_driver.exe
обход /sys/class/hidraw
tests/fixtures/sys - копия настоящих hidraw0/7/8/9
*/
#include <fstream>
#include <iterator>
#include "check.hpp"
#include "hid/discovery.hpp"
#include "hid/descriptor.hpp"
#include "hid/support_table.hpp"

static std::string fixtureDir(const std::string &name)
{
  return std::string(TESTS_DIR) + "/fixtures/" + name;
}

static std::vector<uint8_t> hexToBytes(const std::string &hex)
{
  std::vector<uint8_t> out;

  for (size_t i = 0; i + 1 < hex.size(); i += 2)
  {
    out.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
  }

  return out;
}

static std::vector<uint8_t> readFixture(const std::string &node)
{
  std::ifstream f(fixtureDir("sys/class/hidraw/") + node + "/device/report_descriptor", std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static void testDescriptorCollections()
{
  auto d9 = hexToBytes("06ffff0902a10109021580257f95407508b102c0");
  CHECK(hasCollection(d9, 0xFFFF, 2));
  CHECK(!hasCollection(d9, 0xFFFF, 1));
  auto d8 = readFixture("hidraw8");
  CHECK(!d8.empty());
  CHECK(hasCollection(d8, 0xFFFF, 1));
  CHECK(hasCollection(d8, 0x000C, 1));
  CHECK(!hasCollection(d8, 0xFFFF, 2));
}

// кривое описание роняет разбор
static void testDescriptorGarbage()
{
  CHECK(!hasCollection({0x06, 0xff}, 0xFFFF, 2));
  CHECK(!hasCollection({0xff, 0xff, 0xff}, 0xFFFF, 2));
  CHECK(!hasCollection({}, 0xFFFF, 2));
}

static void testSupportTableHasOurKeyboard()
{
  bool found = false;

  for (const auto &e : supportTable())
  {
    if (e.vid == 0x3151 && e.pid == 0x5030)
    {
      found = e.usagePage == 0xFFFF && e.usage == 2 && e.interfaceNumber == 2;
    }
  }

  CHECK(found);
  CHECK(supportTable().size() > 100);
}

static void testScanFixture()
{
  auto nodes = scanHidraw(fixtureDir("sys/class/hidraw"));
  CHECK(nodes.size() == 4);
  bool sawNine = false;

  for (const auto &n : nodes)
  {
    if (n.devPath == "/dev/hidraw9")
    {
      sawNine = true;
      CHECK(n.usbDevice == "1-10");
      CHECK(n.interfaceNumber == 2);
      CHECK(n.vid == 0x3151 && n.pid == 0x5030);
    }
  }

  CHECK(sawNine);
}

static void testMatchFixture()
{
  auto kbs = matchKeyboards(scanHidraw(fixtureDir("sys/class/hidraw")));
  CHECK(kbs.size() == 1);

  if (kbs.size() == 1)
  {
    CHECK(kbs[0].featurePath == "/dev/hidraw9");
    CHECK(kbs[0].venderPath == "/dev/hidraw8");
    CHECK(kbs[0].vid == 0x3151 && kbs[0].pid == 0x5030);
  }
}

static void testScanMissingDir()
{
  CHECK(scanHidraw("/нет/такой/папки").empty());
}

int main(int argc, char **argv)
{
  const TestCase tests[] = {
      {"descriptor_collections", testDescriptorCollections},
      {"descriptor_garbage", testDescriptorGarbage},
      {"support_table", testSupportTableHasOurKeyboard},
      {"scan_fixture", testScanFixture},
      {"match_fixture", testMatchFixture},
      {"scan_missing_dir", testScanMissingDir},
  };
  return runTests(tests, sizeof(tests) / sizeof(tests[0]), argc, argv);
}
