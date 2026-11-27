// тесты методов driver.DriverGrpc
// то что драйвер отвечает вебке и что передает клавиатуре

#include <atomic>
#include <thread>
#include <cstdlib>
#include <filesystem>
#include "check.hpp"
#include "driver.pb.h"
#include "fake_keyboards.hpp"
#include "service/service.hpp"

static std::vector<std::string> g_dirs;

static std::string tempDir()
{
  char tmpl[] = "/tmp/monsgeek-service-test-XXXXXX";
  char *dir = mkdtemp(tmpl);

  if (dir == nullptr)
  {
    perror("mkdtemp");
    exit(2);
  }

  g_dirs.push_back(dir);
  return dir;
}

static void testSendMsg()
{
  FakeKeyboards kb;
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  driver::SendMsg req;
  req.set_devicepath("/dev/hidraw9");
  req.set_msg(std::string("\x87", 1));
  req.set_checksumtype(driver::BIT7);
  auto r = svc.unary("sendMsg", req.SerializeAsString());
  CHECK(r.status == 0);
  CHECK(kb.lastPath == "/dev/hidraw9");
  CHECK(kb.lastMsg == std::vector<uint8_t>{0x87});
  CHECK(kb.lastType == ChecksumType::Bit7);
  driver::ResSend res;
  CHECK(res.ParseFromString(r.payload) && res.err().empty());
}

static void testSendMsgBit8AndRaw()
{
  FakeKeyboards kb;
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  driver::SendMsg req;
  req.set_devicepath("/dev/hidraw9");
  req.set_msg(std::string("\x07", 1));
  req.set_checksumtype(driver::BIT8);
  svc.unary("sendMsg", req.SerializeAsString());
  CHECK(kb.lastType == ChecksumType::Bit8 && !kb.lastWasRaw);
  svc.unary("sendRawFeature", req.SerializeAsString());
  CHECK(kb.lastWasRaw);
}

static void testSendErrorPassedToSite()
{
  FakeKeyboards kb;
  kb.sendError = "dev is not online";
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  driver::SendMsg req;
  req.set_devicepath("/dev/hidraw9");
  req.set_msg(std::string("\x87", 1));
  driver::ResSend res;
  CHECK(res.ParseFromString(svc.unary("sendMsg", req.SerializeAsString()).payload));
  CHECK(res.err() == "dev is not online");
}

static void testReadMsg()
{
  FakeKeyboards kb;
  kb.readAnswer[0] = 0x87;
  kb.readAnswer[1] = 0x01;
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  driver::ReadMsg req;
  req.set_devicepath("/dev/hidraw9");
  driver::ResRead res;
  CHECK(res.ParseFromString(svc.unary("readMsg", req.SerializeAsString()).payload));
  CHECK(res.err().empty());
  CHECK(res.msg().size() == 64);
  CHECK(static_cast<uint8_t>(res.msg()[0]) == 0x87 && res.msg()[1] == 0x01);
}

static void testReadMsgError()
{
  FakeKeyboards kb;
  kb.readError = "dev is not online";
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  driver::ReadMsg req;
  req.set_devicepath("/dev/hidraw9");
  driver::ResRead res;
  CHECK(res.ParseFromString(svc.unary("readMsg", req.SerializeAsString()).payload));
  CHECK(res.err() == "dev is not online");
}

static void testDevListStream()
{
  FakeKeyboards kb;
  kb.devices = {{"/dev/hidraw9", 2387, 0x3151, 0x5030}};
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  std::vector<driver::DeviceList> got;
  int id = svc.openStream("watchDevList", "",
                          [&](const std::string &p)
                          {
                            driver::DeviceList l;
                            l.ParseFromString(p);
                            got.push_back(l);
                            return true;
                          });
  CHECK(id >= 0);
  CHECK(got.size() == 1);

  if (got.size() == 1 && got[0].devlist_size() == 1)
  {
    CHECK(got[0].type() == driver::INIT);
    CHECK(got[0].devlist(0).dev().id() == 2387);
    CHECK(got[0].devlist(0).dev().path() == "/dev/hidraw9");
    CHECK(got[0].devlist(0).dev().vid() == 0x3151);
    CHECK(got[0].devlist(0).dev().pid() == 0x5030);
    CHECK(got[0].devlist(0).dev().devtype() == driver::YZWKEYBOARD);
    CHECK(got[0].devlist(0).dev().isonline());
  }

  kb.devices.clear();
  kb.emitList(ListChange::Remove);
  CHECK(got.size() == 2 && got[1].type() == driver::REMOVE && got[1].devlist_size() == 0);
  svc.closeStream(id);
  CHECK(kb.subscriberCount() == 0);
}

static void testVenderStream()
{
  FakeKeyboards kb;
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  std::vector<std::string> got;
  svc.openStream("watchVender", "",
                 [&](const std::string &p)
                 {
                   driver::VenderMsg m;
                   m.ParseFromString(p);
                   got.push_back(m.msg());
                   return true;
                 });
  kb.emitVender({5, 15, 1, 0});
  CHECK(got.size() == 1 && got[0] == std::string("\x05\x0f\x01\x00", 4));
}

static void testStreamStopsWhenPushFails()
{
  FakeKeyboards kb;
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  svc.openStream("watchVender", "", [](const std::string &) { return false; });
  kb.emitVender({5, 15, 1, 0});
  CHECK(kb.subscriberCount() == 0);
}

// поток у которого первая же отправка (Init) не прошла тоже должен отписаться
static void testDevListStreamStopsOnFirstPush()
{
  FakeKeyboards kb;
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  svc.openStream("watchDevList", "", [](const std::string &) { return false; });
  kb.emitList(ListChange::Add);
  CHECK(kb.subscriberCount() == 0);
}

static void testStubStreams()
{
  FakeKeyboards kb;
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  CHECK(svc.isStream("watchSystemInfo") && svc.isStream("upgradeOTAGATT"));
  CHECK(!svc.isStream("sendMsg"));
  int id = svc.openStream("watchSystemInfo", "", [](const std::string &) { return true; });
  CHECK(id >= 0);
  svc.closeStream(id);
  CHECK(svc.openStream("noSuchStream", "", [](const std::string &) { return true; }) == -1);
}

static void testVersionDbAndUnknown()
{
  FakeKeyboards kb;
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  driver::Version v;
  CHECK(v.ParseFromString(svc.unary("getVersion", "").payload));
  CHECK(v.baseversion() == "2.1.5");
  driver::InsertDb ins;
  ins.set_dbpath("cfg");
  ins.set_key("a");
  ins.set_value("1");
  CHECK(svc.unary("insertDb", ins.SerializeAsString()).status == 0);
  driver::GetItem gi;
  gi.set_dbpath("cfg");
  gi.set_key("a");
  driver::Item it;
  CHECK(it.ParseFromString(svc.unary("getItemFromDb", gi.SerializeAsString()).payload));
  CHECK(it.value() == "1" && it.errstr().empty());
  driver::GetAll ga;
  ga.set_dbpath("cfg");
  driver::AllList keys;
  CHECK(keys.ParseFromString(svc.unary("getAllKeysFromDb", ga.SerializeAsString()).payload));
  CHECK(keys.data_size() == 1 && keys.data(0) == "a");
  driver::AllList values;
  CHECK(values.ParseFromString(svc.unary("getAllValuesFromDb", ga.SerializeAsString()).payload));
  CHECK(values.data_size() == 1 && values.data(0) == "1");
  driver::DeleteItem del;
  del.set_dbpath("cfg");
  del.set_key("a");
  svc.unary("deleteItemFromDb", del.SerializeAsString());
  CHECK(it.ParseFromString(svc.unary("getItemFromDb", gi.SerializeAsString()).payload));
  CHECK(it.value().empty() && it.errstr() == "not found");
  CHECK(svc.unary("noSuchMethod", "").status == 12);
  CHECK(svc.unary("sendMsg", "\xff\xff\xff").status == 3);
}

// то что вебка вызывает при старте
// при отсутствии ответа выводится окно для скачивания драйвера
static void testStartupStubs()
{
  FakeKeyboards kb;
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  driver::WirelessLoopStatus w;
  w.set_lock(true);
  CHECK(svc.unary("changeWirelessLoopStatus", w.SerializeAsString()).status == 0);

  for (const char *m :
       {"setLightType", "cleanDev", "muteMicrophone", "toggleMicrophoneMute", "getMicrophoneMute", "getWeather"})
  {
    CHECK(svc.unary(m, "").status == 0);
  }
}

// клавиатура шлет события в одном потоке а вкладки в это время открываются и закрываются в другом
static void testCloseWhileEmitting()
{
  FakeKeyboards kb;
  Store store(tempDir());
  Service svc(kb, store, "2.1.5");
  std::atomic<bool> stop{false};
  std::thread emitter(
      [&]
      {
        while (!stop)
        {
          kb.emitVender({5, 1, 0, 0});
        }
      });

  for (int i = 0; i < 2000; ++i)
  {
    int id = svc.openStream("watchVender", "", [](const std::string &) { return true; });
    svc.closeStream(id);
  }

  stop = true;
  emitter.join();
  CHECK(kb.subscriberCount() == 0);
}

int main(int argc, char **argv)
{
  const TestCase tests[] = {
      {"send_msg", testSendMsg},
      {"send_msg_bit8_and_raw", testSendMsgBit8AndRaw},
      {"send_error_passed_to_site", testSendErrorPassedToSite},
      {"read_msg", testReadMsg},
      {"read_msg_error", testReadMsgError},
      {"dev_list_stream", testDevListStream},
      {"vender_stream", testVenderStream},
      {"stream_stops_when_push_fails", testStreamStopsWhenPushFails},
      {"dev_list_stream_stops_on_first_push", testDevListStreamStopsOnFirstPush},
      {"stub_streams", testStubStreams},
      {"version_db_and_unknown", testVersionDbAndUnknown},
      {"startup_stubs", testStartupStubs},
      {"close_while_emitting", testCloseWhileEmitting},
  };
  int rc = runTests(tests, sizeof(tests) / sizeof(tests[0]), argc, argv);

  for (const auto &d : g_dirs)
  {
    std::filesystem::remove_all(d);
  }

  return rc;
}
