// тесты отправки и чтения команд
// вместо hidraw подставляется FakeChannel

#include <atomic>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include "check.hpp"
#include "hid/hidraw.hpp"
#include "hid/keyboard_io.hpp"

class FakeChannel : public FeatureChannel
{
public:
  std::array<uint8_t, 65> lastSent{};
  std::array<uint8_t, 65> answer{};
  int sendCount = 0;
  std::string failWith;

  bool sendFeature(const std::array<uint8_t, 65> &data, std::string &error) override
  {
    if (!failWith.empty())
    {
      error = failWith;
      return false;
    }

    lastSent = data;
    ++sendCount;
    return true;
  }

  bool getFeature(std::array<uint8_t, 65> &data, std::string &error) override
  {
    if (!failWith.empty())
    {
      error = failWith;
      return false;
    }

    data = answer;
    return true;
  }
};

static void testSendPadsAndChecksums()
{
  auto fake = std::make_unique<FakeChannel>();
  auto *raw = fake.get();
  KeyboardIo io(std::move(fake));
  CHECK(io.send({0x87}, ChecksumType::Bit7).empty());
  CHECK(raw->lastSent[0] == 0x00);
  CHECK(raw->lastSent[1] == 0x87);
  CHECK(raw->lastSent[8] == 255 - 0x87);
}

static void testSendRawNoChecksum()
{
  auto fake = std::make_unique<FakeChannel>();
  auto *raw = fake.get();
  KeyboardIo io(std::move(fake));
  CHECK(io.sendRaw({0x87}).empty());
  CHECK(raw->lastSent[1] == 0x87);
  CHECK(raw->lastSent[8] == 0x00);
}

static void testReadStripsReportId()
{
  auto fake = std::make_unique<FakeChannel>();
  fake->answer[0] = 0x00;
  fake->answer[1] = 0x87;
  fake->answer[2] = 0x01;
  KeyboardIo io(std::move(fake));
  std::string error;
  auto p = io.read(error);
  CHECK(p.has_value() && (*p)[0] == 0x87 && (*p)[1] == 0x01);
}

static void testModelId()
{
  auto fake = std::make_unique<FakeChannel>();
  fake->answer[1] = 0x8F;
  fake->answer[2] = 0x53;
  fake->answer[3] = 0x09;
  KeyboardIo io(std::move(fake));
  CHECK(io.queryModelId() == std::optional<int32_t>(2387));
}

// клавиатура ответила не на ту команду - номер модели не берем
static void testModelIdWrongAnswer()
{
  auto fake = std::make_unique<FakeChannel>();
  auto *raw = fake.get();
  fake->answer[1] = 0x87;
  KeyboardIo io(std::move(fake));
  CHECK(!io.queryModelId().has_value());
  CHECK(raw->sendCount == 3);
}

static void testUnpluggedGivesError()
{
  auto fake = std::make_unique<FakeChannel>();
  fake->failWith = "устройство отключено";
  KeyboardIo io(std::move(fake));
  CHECK(!io.send({0x87}, ChecksumType::Bit7).empty());
  std::string error;
  CHECK(!io.read(error).has_value());
  CHECK(!error.empty());
}

static void testTooLongNotSent()
{
  auto fake = std::make_unique<FakeChannel>();
  auto *raw = fake.get();
  KeyboardIo io(std::move(fake));
  CHECK(!io.send(std::vector<uint8_t>(65, 1), ChecksumType::Bit7).empty());
  CHECK(!io.send({}, ChecksumType::Bit7).empty());
  CHECK(!io.sendRaw(std::vector<uint8_t>(65, 1)).empty());
  CHECK(raw->sendCount == 0);
}

static void testOpenMissing()
{
  std::string error;
  auto ch = HidrawChannel::open("/dev/hidraw-нет-такого", error);
  CHECK(ch == nullptr);
  CHECK(!error.empty());
}

// вместо hidraw обычный pipe
// в него пишем отчеты
// VenderReader должен отдать только отчет номер 5
static void testVenderReaderFilters()
{
  int fds[2];
  CHECK(pipe(fds) == 0);
  std::string path = "/proc/self/fd/" + std::to_string(fds[0]);
  std::mutex m;
  std::vector<std::vector<uint8_t>> got;

  {
    VenderReader reader(path,
                        [&](std::vector<uint8_t> msg)
                        {
                          std::lock_guard<std::mutex> lock(m);
                          got.push_back(std::move(msg));
                        });
    uint8_t other[] = {3, 0xE9, 0x00};
    CHECK(write(fds[1], other, sizeof(other)) == sizeof(other));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    uint8_t ours[] = {5, 15, 1, 0};
    CHECK(write(fds[1], ours, sizeof(ours)) == sizeof(ours));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  close(fds[0]);
  close(fds[1]);
  CHECK(got.size() == 1);

  if (got.size() == 1)
  {
    CHECK((got[0] == std::vector<uint8_t>{5, 15, 1, 0}));
  }
}

// отвечаем на последний запрос - возвращаем его команду и номер куска
class EchoChannel : public FeatureChannel
{
public:
  bool sendFeature(const std::array<uint8_t, 65> &data, std::string &) override
  {
    lastSent = data;
    return true;
  }

  bool getFeature(std::array<uint8_t, 65> &data, std::string &) override
  {
    data = {};
    data[1] = lastSent[1];
    data[2] = lastSent[3];
    return true;
  }

private:
  std::array<uint8_t, 65> lastSent{};
};

static long msSince(std::chrono::steady_clock::time_point start)
{
  auto d = std::chrono::steady_clock::now() - start;
  return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}

// две вкладки читают раскладку одновременно поэтому вторая не должна влезть между запросом и чтением первой
static void testTwoClientsDontCross()
{
  KeyboardIo io(std::make_unique<EchoChannel>());
  std::atomic<int> wrong{0};

  auto client = [&](uint8_t chunk)
  {
    for (int i = 0; i < 20; ++i)
    {
      std::string error;
      CHECK(io.send({0x8A, 0, chunk}, ChecksumType::Bit7).empty());
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      auto p = io.read(error);

      if (!p || (*p)[0] != 0x8A || (*p)[1] != chunk)
      {
        ++wrong;
      }
    }
  };

  std::thread a(client, 1);
  std::thread b(client, 2);
  a.join();
  b.join();
  CHECK(wrong == 0);
}

// после команд записи вебка ответ не читает
// если будет задердка то запись изменения раскладки будет медленней
static void testSetCommandsDontWait()
{
  KeyboardIo io(std::make_unique<EchoChannel>());
  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < 9; ++i)
  {
    CHECK(io.send({0x0A, 0, static_cast<uint8_t>(i)}, ChecksumType::Bit7).empty());
  }

  CHECK(msSince(start) < 50);
}

// вкладка отправила запрос и была закрыта не успев прочитать ответ - клавиатура не должна зависнуть
static void testUnreadAnswerTimesOut()
{
  KeyboardIo io(std::make_unique<EchoChannel>());
  CHECK(io.send({0x8A, 0, 1}, ChecksumType::Bit7).empty());
  auto start = std::chrono::steady_clock::now();
  CHECK(io.send({0x8A, 0, 2}, ChecksumType::Bit7).empty());
  CHECK(msSince(start) < 2500);
  std::string error;
  auto p = io.read(error);
  CHECK(p && (*p)[1] == 2);
}

int main(int argc, char **argv)
{
  const TestCase tests[] = {
      {"send_pads_and_checksums", testSendPadsAndChecksums},
      {"send_raw_no_checksum", testSendRawNoChecksum},
      {"read_strips_report_id", testReadStripsReportId},
      {"model_id", testModelId},
      {"model_id_wrong_answer", testModelIdWrongAnswer},
      {"unplugged_gives_error", testUnpluggedGivesError},
      {"too_long_not_sent", testTooLongNotSent},
      {"open_missing", testOpenMissing},
      {"vender_reader_filters", testVenderReaderFilters},
      {"two_clients_dont_cross", testTwoClientsDontCross},
      {"set_commands_dont_wait", testSetCommandsDontWait},
      {"unread_answer_times_out", testUnreadAnswerTimesOut},
  };
  return runTests(tests, sizeof(tests) / sizeof(tests[0]), argc, argv);
}
