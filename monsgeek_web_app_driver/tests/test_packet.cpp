// тесты сборки пакета для клавиатуры
// дополнение до 64 байт, контрольная сумма, номер модели

#include "check.hpp"
#include "hid/packet.hpp"

static void testBit7()
{
  std::string error;
  auto p = buildPacket({0x8F}, ChecksumType::Bit7, error);
  CHECK(p.has_value());
  CHECK((*p)[0] == 0x8F);
  CHECK((*p)[7] == 0x70);
  CHECK((*p)[63] == 0x00);
}

static void testBit8()
{
  std::string error;
  auto p = buildPacket({0x07, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}, ChecksumType::Bit8, error);
  CHECK(p.has_value());
  CHECK((*p)[8] == 255 - ((0x07 + 1 + 2 + 3 + 4 + 5 + 6 + 7) & 0xFF));
}

static void testNoneKeepsBytes()
{
  std::string error;
  auto p = buildPacket({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x99}, ChecksumType::None, error);
  CHECK(p.has_value());
  CHECK((*p)[7] == 0x99);
}

static void testTooLongAndEmpty()
{
  std::string error;
  CHECK(!buildPacket(std::vector<uint8_t>(65, 1), ChecksumType::Bit7, error).has_value());
  CHECK(!error.empty());
  error.clear();
  CHECK(!buildPacket({}, ChecksumType::Bit7, error).has_value());
  CHECK(!error.empty());
}

static void testReportId()
{
  Packet p{};
  p[0] = 0x8F;
  auto r = withReportId(p);
  CHECK(r[0] == 0x00);
  CHECK(r[1] == 0x8F);
}

static void testModelId()
{
  Packet a{};
  a[0] = 0x8F;
  a[1] = 0x53;
  a[2] = 0x09;
  CHECK(parseModelId(a) == 2387);
  a[1] = a[2] = a[3] = a[4] = 0xFF;
  CHECK(parseModelId(a) == -1);
}

int main(int argc, char **argv)
{
  const TestCase tests[] = {
      {"bit7", testBit7},
      {"bit8", testBit8},
      {"none_keeps_bytes", testNoneKeepsBytes},
      {"too_long_and_empty", testTooLongAndEmpty},
      {"report_id", testReportId},
      {"model_id", testModelId},
  };
  return runTests(tests, sizeof(tests) / sizeof(tests[0]), argc, argv);
}
