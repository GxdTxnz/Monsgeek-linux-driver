#pragma once

/*
методы driver.DriverGrpc которые web.monsgeek.com ждет от драйвера
на входе/выходе байты protobuf + кадры gRPC-web
HTTP здесь не видны
*/
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <functional>
#include "db/store.hpp"
#include "devices/device_manager.hpp"

struct CallResult
{
  // 0 - все норм
  // 3 - кривой запрос
  // 12 - нет такого метода
  int status;
  std::string message;
  std::string payload;
};

class Service
{
public:
  using Push = std::function<bool(const std::string &payload)>;

  Service(Keyboards &keyboards, Store &store, std::string version);

  ~Service();

  // method - без префикса типа "sendMsg"
  // request - байты protobuf
  CallResult unary(const std::string &method, const std::string &request);

  bool isStream(const std::string &method) const;

  /*
  каждое событие отдается в push
  если push вернул false (закрыли вебки) то поток отпишется сам
  возвращаем номер для closeStream
  -1 если такого потока нет
  */
  int openStream(const std::string &method, const std::string &request, Push push);

  void closeStream(int id);

private:
  struct StreamState;

  Keyboards &keyboards_;
  Store &store_;
  std::string version_;

  std::mutex streamsMutex_;
  int nextStreamId_ = 1;
  std::map<int, std::shared_ptr<StreamState>> streams_;
};
