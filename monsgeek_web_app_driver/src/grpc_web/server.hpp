#pragma once

/*
вебка лезет на 127.0.0.1:3814 по http
держим на этом адреса http-сервер который будет разбирать и собирать обертку gRPC-web
пускаем только вебку monsgeek и ничего больше
за разбор вызовов (sendMsg readMsg и тд) отвечает Service
*/
#include <atomic>
#include <memory>
#include <thread>
#include "config.hpp"
#include "service/service.hpp"

namespace httplib
{
class Server;
}

class GrpcWebServer
{
public:
  GrpcWebServer(Service &service, Config config);
  ~GrpcWebServer();
  // false если порт занят
  // config.port = 0 = любой свободный (для тестов)
  bool start();
  void stop();
  int port() const;

private:
  void setupRoutes();
  bool originAllowed(const std::string &origin) const;

  Service &service_;
  Config config_;
  std::unique_ptr<httplib::Server> server_;
  std::thread thread_;
  int port_ = 0;
  // флаг для остановки драйвера чтобы открытые потоки (watchDevList, watchVender) перестали ждать событий
  std::atomic<bool> stopping_{false};
};
