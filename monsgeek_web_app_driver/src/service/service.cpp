#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "driver.pb.h"
#include "service/service.hpp"

static const int kStatusOk = 0;
static const int kStatusInvalidArgument = 3;
static const int kStatusUnimplemented = 12;

// описание одного открытого потока: номер подписки и флаг "вебка закрыта"
struct Service::StreamState
{
  std::mutex mutex;
  int subscriptionId = -1;
  bool dead = false;
};

Service::Service(Keyboards &keyboards, Store &store, std::string version)
    : keyboards_(keyboards), store_(store), version_(std::move(version))
{
}

Service::~Service()
{
  std::map<int, std::shared_ptr<StreamState>> streams;

  {
    std::lock_guard<std::mutex> lock(streamsMutex_);
    streams.swap(streams_);
  }

  for (auto &[id, state] : streams)
  {
    int subscription = -1;

    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->dead = true;
      subscription = state->subscriptionId;
      state->subscriptionId = -1;
    }

    if (subscription >= 0)
    {
      keyboards_.unsubscribe(subscription);
    }
  }
}

static CallResult ok(const google::protobuf::Message &reply)
{
  return {kStatusOk, "", reply.SerializeAsString()};
}

static CallResult badRequest()
{
  return {kStatusInvalidArgument, "bad request", ""};
}

static std::vector<uint8_t> toBytes(const std::string &s)
{
  return std::vector<uint8_t>(s.begin(), s.end());
}

static ChecksumType toChecksum(driver::CheckSumType t)
{
  switch (t)
  {
  case driver::BIT8:
    return ChecksumType::Bit8;

  case driver::NONE:
    return ChecksumType::None;

  default:
    return ChecksumType::Bit7;
  }
}

static driver::DeviceList toDeviceList(ListChange change, const std::vector<KeyboardInfo> &list)
{
  driver::DeviceList out;
  out.set_type(static_cast<driver::DeviceListChangeType>(change));

  for (const auto &kb : list)
  {
    auto *dev = out.add_devlist()->mutable_dev();
    dev->set_devtype(driver::YZWKEYBOARD);
    dev->set_path(kb.path);
    dev->set_id(kb.id);
    dev->set_vid(kb.vid);
    dev->set_pid(kb.pid);
    dev->set_isonline(true);
  }

  return out;
}

template <typename Request> static bool parse(Request &req, const std::string &bytes)
{
  return req.ParseFromString(bytes);
}

// MONSGEEK_BRIDGE_TRACE=1 - вывод каждого запроса к клавиатуре и ответа чтобы видеть что приходит от вебки
static bool traceOn()
{
  static const bool on = std::getenv("MONSGEEK_BRIDGE_TRACE") != nullptr;
  return on;
}

static void trace(const char *what, const std::string &bytes)
{
  if (!traceOn())
  {
    return;
  }

  std::string line = what;
  char hex[4];

  for (size_t i = 0; i < bytes.size() && i < 24; ++i)
  {
    std::snprintf(hex, sizeof(hex), " %02x", static_cast<unsigned char>(bytes[i]));
    line += hex;
  }

  std::cerr << line << std::endl;
}

CallResult Service::unary(const std::string &method, const std::string &request)
{
  if (method == "sendMsg" || method == "sendRawFeature")
  {
    driver::SendMsg req;

    if (!parse(req, request))
    {
      return badRequest();
    }

    driver::ResSend res;
    trace(method == "sendMsg" ? "send" : "send-raw", req.msg());

    if (method == "sendMsg")
    {
      res.set_err(keyboards_.send(req.devicepath(), toBytes(req.msg()), toChecksum(req.checksumtype())));
    }

    else
    {
      res.set_err(keyboards_.sendRaw(req.devicepath(), toBytes(req.msg())));
    }

    return ok(res);
  }

  if (method == "readMsg" || method == "readRawFeature")
  {
    driver::ReadMsg req;

    if (!parse(req, request))
    {
      return badRequest();
    }

    driver::ResRead res;
    std::string error;
    auto packet = keyboards_.read(req.devicepath(), error);

    if (packet)
    {
      res.set_msg(std::string(packet->begin(), packet->end()));
      trace("read", res.msg());
    }

    else
    {
      res.set_err(error.empty() ? "read failed" : error);
    }

    return ok(res);
  }

  if (method == "getVersion")
  {
    driver::Version res;
    res.set_baseversion(version_);
    return ok(res);
  }

  if (method == "insertDb")
  {
    driver::InsertDb req;

    if (!parse(req, request))
    {
      return badRequest();
    }

    driver::ResSend res;
    res.set_err(store_.insert(req.dbpath(), req.key(), req.value()));
    return ok(res);
  }

  if (method == "deleteItemFromDb")
  {
    driver::DeleteItem req;

    if (!parse(req, request))
    {
      return badRequest();
    }

    driver::ResSend res;
    res.set_err(store_.remove(req.dbpath(), req.key()));
    return ok(res);
  }

  if (method == "getItemFromDb")
  {
    driver::GetItem req;

    if (!parse(req, request))
    {
      return badRequest();
    }

    driver::Item res;
    auto value = store_.get(req.dbpath(), req.key());

    if (value)
    {
      res.set_value(*value);
    }

    else
    {
      res.set_errstr("not found");
    }

    return ok(res);
  }

  if (method == "getAllKeysFromDb" || method == "getAllValuesFromDb")
  {
    driver::GetAll req;

    if (!parse(req, request))
    {
      return badRequest();
    }

    driver::AllList res;
    auto items = method == "getAllKeysFromDb" ? store_.keys(req.dbpath()) : store_.values(req.dbpath());

    for (const auto &item : items)
    {
      res.add_data(item);
    }

    return ok(res);
  }

  // методы которые не нужны для настройки клавиатуры но вызываются вебкой (заглушки)
  if (method == "changeWirelessLoopStatus" || method == "setLightType")
  {
    return ok(driver::Empty());
  }

  if (method == "cleanDev" || method == "muteMicrophone" || method == "toggleMicrophoneMute")
  {
    return ok(driver::ResSend());
  }

  if (method == "getMicrophoneMute")
  {
    return ok(driver::MicrophoneMuteStatus());
  }

  if (method == "getWeather")
  {
    return ok(driver::WeatherRes());
  }

  return {kStatusUnimplemented, "method not implemented", ""};
}

bool Service::isStream(const std::string &method) const
{
  return method == "watchDevList" || method == "watchVender" || method == "watchSystemInfo" ||
         method == "upgradeOTAGATT";
}

int Service::openStream(const std::string &method, const std::string &, Push push)
{
  if (!isStream(method))
  {
    return -1;
  }

  auto state = std::make_shared<StreamState>();
  // weak_ptr чтобы спокойно обработчик не мешал удалить состояние при закрытии потока
  std::weak_ptr<StreamState> weak = state;
  Keyboards &keyboards = keyboards_;

  // отправка одного события в сторону вебки
  // не дошло = вебку закрыли тогда помечаем поток "мертвым" и отписываемся
  auto deliver = [weak, push, &keyboards](const std::string &payload)
  {
    auto s = weak.lock();

    if (!s)
    {
      return;
    }

    std::lock_guard<std::mutex> lock(s->mutex);

    if (s->dead)
    {
      return;
    }

    if (!push(payload))
    {
      s->dead = true;

      if (s->subscriptionId >= 0)
      {
        keyboards.unsubscribe(s->subscriptionId);
        s->subscriptionId = -1;
      }
    }
  };

  int subscription = -1;

  if (method == "watchDevList")
  {
    subscription = keyboards_.subscribeList([deliver](ListChange change, std::vector<KeyboardInfo> list)
                                            { deliver(toDeviceList(change, list).SerializeAsString()); });
  }

  else if (method == "watchVender")
  {
    subscription = keyboards_.subscribeVender(
        [deliver](std::vector<uint8_t> msg)
        {
          driver::VenderMsg m;
          m.set_msg(std::string(msg.begin(), msg.end()));
          deliver(m.SerializeAsString());
        });
  }

  bool alreadyDead = false;

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    alreadyDead = state->dead;

    if (!alreadyDead)
    {
      state->subscriptionId = subscription;
    }
  }

  // первая отправка (начальный список) могла не дойти и тогда подписку сразу снимаем вне мьютекса потока
  if (alreadyDead && subscription >= 0)
  {
    keyboards_.unsubscribe(subscription);
  }

  std::lock_guard<std::mutex> lock(streamsMutex_);
  int id = nextStreamId_++;
  streams_[id] = state;
  return id;
}

void Service::closeStream(int id)
{
  std::shared_ptr<StreamState> state;

  {
    std::lock_guard<std::mutex> lock(streamsMutex_);
    auto it = streams_.find(id);

    if (it == streams_.end())
    {
      return;
    }

    state = it->second;
    streams_.erase(it);
  }

  int subscription = -1;

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->dead = true;
    subscription = state->subscriptionId;
    state->subscriptionId = -1;
  }

  // отписываемся вне мьютекса потока чтобы при удержании мьютекса потока
  // и ожидании мьютекса подписчиков не было взаимной блокировки
  if (subscription >= 0)
  {
    keyboards_.unsubscribe(subscription);
  }
}
