#pragma once

// открываем /dev/hidrawN и отдаем туда feature report + обычные отчеты

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include <functional>

// канал для команд: отправить пакет и забрать ответ (65 байт где 0 номер отчета и 64 байта данных)
// отдельный интерфейс для тестов
class FeatureChannel
{
public:
  virtual ~FeatureChannel() = default;

  virtual bool sendFeature(const std::array<uint8_t, 65> &data, std::string &error) = 0;

  virtual bool getFeature(std::array<uint8_t, 65> &data, std::string &error) = 0;
};

class HidrawChannel : public FeatureChannel
{
public:
  // если открыть не вышло то nullptr и текст с ошибкой (нет файла/нет прав)
  static std::unique_ptr<HidrawChannel> open(const std::string &path, std::string &error);

  ~HidrawChannel() override;

  bool sendFeature(const std::array<uint8_t, 65> &data, std::string &error) override;

  bool getFeature(std::array<uint8_t, 65> &data, std::string &error) override;

private:
  explicit HidrawChannel(int fd);

  int fd_;
};

// отдельный поток который будет слушать канал сообщений клавиатуры и отдавать только отчеты номер 5
// остальное скип
class VenderReader
{
public:
  VenderReader(const std::string &path, std::function<void(std::vector<uint8_t>)> onMessage);

  ~VenderReader();

  void stop();

private:
  void run();

  int fd_ = -1;
  int wakeFd_ = -1;
  std::function<void(std::vector<uint8_t>)> onMessage_;
  std::thread thread_;
};
