// тесты списка клавиатур: подключение, отключение, номер модели, ошибки

#include "check.hpp"
#include "devices/device_manager.hpp"

// симулируем поведение клавитуры
// на любой запрос отвечает номером модели
// может "отвалиться" после первого ответа
class FakeChannel : public FeatureChannel
{
public:
  FakeChannel(int32_t modelId, bool failAfterFirstAnswer)
      : modelId_(modelId), failAfterFirstAnswer_(failAfterFirstAnswer)
  {
  }

  bool sendFeature(const std::array<uint8_t, 65> &, std::string &error) override
  {
    if (dead_)
    {
      error = "No such device";
      return false;
    }

    return true;
  }

  bool getFeature(std::array<uint8_t, 65> &data, std::string &error) override
  {
    if (dead_)
    {
      error = "No such device";
      return false;
    }

    data.fill(0);
    data[1] = 0x8F;
    data[2] = static_cast<uint8_t>(modelId_ & 0xFF);
    data[3] = static_cast<uint8_t>((modelId_ >> 8) & 0xFF);
    dead_ = failAfterFirstAnswer_;
    return true;
  }

private:
  int32_t modelId_;
  bool failAfterFirstAnswer_;
  bool dead_ = false;
};

static DeviceManager::Opener fakeOpenReturningModel(int32_t id)
{
  return [id](const std::string &)
  {
    return std::make_unique<FakeChannel>(id, false);
  };
}

static DeviceManager::Opener fakeOpenFailingAfterModel(int32_t id)
{
  return [id](const std::string &)
  {
    return std::make_unique<FakeChannel>(id, true);
  };
}

static void testInitThenAddRemove()
{
  std::vector<FoundKeyboard> present;
  DeviceManager dm([&] { return present; }, fakeOpenReturningModel(2387), false);
  std::vector<std::pair<ListChange, size_t>> events;
  dm.subscribeList([&](ListChange c, std::vector<KeyboardInfo> l) { events.push_back({c, l.size()}); });
  present.push_back({"/dev/hidraw9", "/dev/hidraw8", 0x3151, 0x5030});
  dm.rescan();
  present.clear();
  dm.rescan();
  CHECK(events.size() == 3);

  if (events.size() == 3)
  {
    CHECK(events[0].first == ListChange::Init && events[0].second == 0);
    CHECK(events[1].first == ListChange::Add && events[1].second == 1);
    CHECK(events[2].first == ListChange::Remove && events[2].second == 0);
  }
}

// повторное сканирование без изменений не должнр дергать подписчиков
static void testNoChangeNoEvent()
{
  std::vector<FoundKeyboard> present{{"/dev/hidraw9", "", 0x3151, 0x5030}};
  DeviceManager dm([&] { return present; }, fakeOpenReturningModel(2387), false);
  dm.rescan();
  int events = 0;
  dm.subscribeList([&](ListChange, std::vector<KeyboardInfo>) { ++events; });
  dm.rescan();
  CHECK(events == 1);
}

static void testModelIdInList()
{
  std::vector<FoundKeyboard> present{{"/dev/hidraw9", "", 0x3151, 0x5030}};
  DeviceManager dm([&] { return present; }, fakeOpenReturningModel(2387), false);
  dm.rescan();
  auto list = dm.list();
  CHECK(list.size() == 1);

  if (list.size() == 1)
  {
    CHECK(list[0].id == 2387);
    CHECK(list[0].path == "/dev/hidraw9");
    CHECK(list[0].vid == 0x3151 && list[0].pid == 0x5030);
  }
}

static void testUnknownPath()
{
  DeviceManager dm([] { return std::vector<FoundKeyboard>{}; }, fakeOpenReturningModel(2387), false);
  CHECK(dm.send("/dev/hidraw42", {0x87}, ChecksumType::Bit7) == "dev is not online");
  std::string error;
  CHECK(!dm.read("/dev/hidraw42", error).has_value());
  CHECK(error == "dev is not online");
}

static void testOpenFailSkipsKeyboard()
{
  std::vector<FoundKeyboard> present{{"/dev/hidraw9", "", 0x3151, 0x5030}};
  DeviceManager dm([&] { return present; }, [](const std::string &) { return nullptr; }, false);
  dm.rescan();
  CHECK(dm.list().empty());
}

static void testErrorTriggersRescan()
{
  std::vector<FoundKeyboard> present{{"/dev/hidraw9", "", 0x3151, 0x5030}};
  DeviceManager dm([&] { return present; }, fakeOpenFailingAfterModel(2387), false);
  dm.rescan();
  int removes = 0;
  dm.subscribeList([&](ListChange c, std::vector<KeyboardInfo>) { removes += c == ListChange::Remove; });
  present.clear();
  CHECK(!dm.send("/dev/hidraw9", {0x87}, ChecksumType::Bit7).empty());
  CHECK(removes == 1);
  CHECK(dm.list().empty());
}

// подписчик может отписаться прямо из своего же обработчика (когда вкладку закрыли)
static void testUnsubscribeInsideCallback()
{
  std::vector<FoundKeyboard> present;
  DeviceManager dm([&] { return present; }, fakeOpenReturningModel(2387), false);
  int id = -1;
  int calls = 0;
  id = dm.subscribeList(
      [&](ListChange c, std::vector<KeyboardInfo>)
      {
        ++calls;

        if (c != ListChange::Init)
        {
          dm.unsubscribe(id);
        }
      });
  present.push_back({"/dev/hidraw9", "", 0x3151, 0x5030});
  dm.rescan();
  present.clear();
  dm.rescan();
  CHECK(calls == 2);
}

int main(int argc, char **argv)
{
  const TestCase tests[] = {
      {"init_then_add_remove", testInitThenAddRemove},
      {"no_change_no_event", testNoChangeNoEvent},
      {"model_id_in_list", testModelIdInList},
      {"unknown_path", testUnknownPath},
      {"open_fail_skips_keyboard", testOpenFailSkipsKeyboard},
      {"error_triggers_rescan", testErrorTriggersRescan},
      {"unsubscribe_inside_callback", testUnsubscribeInsideCallback},
  };
  return runTests(tests, sizeof(tests) / sizeof(tests[0]), argc, argv);
}
