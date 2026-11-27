#pragma once

// настройки для драйвера: какой адрес держать (адрес:порт) + от чего принимать запросы + какую версию отдавать вебке

#include <string>
#include <vector>

struct Config
{
  std::string host = "127.0.0.1";
  int port = 3814;
  // адрес которому можно обращаться к клавиатуре
  // другие получают 403
  std::vector<std::string> allowedOrigins{"https://web.monsgeek.com"};
  // версия которую драйвер отдает вебке в getVersion
  std::string version = "2.1.5";
};
