#pragma once

// команды для клавиатуры: отправить/прочитать ответ/узнать номер модели

#include <mutex>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <condition_variable>
#include "hid/hidraw.hpp"
#include "hid/packet.hpp"

class KeyboardIo
{
public:
  explicit KeyboardIo(std::unique_ptr<FeatureChannel> channel);

  // пустая строка - все норм иначе текст ошибки (на вебке его видно)
  std::string send(const std::vector<uint8_t> &msg, ChecksumType type);
  // без контрольной суммы msg просто добивается нулями до 64 байт
  std::string sendRaw(const std::vector<uint8_t> &msg);
  // 64 байта ответа без номера отчета
  std::optional<Packet> read(std::string &error);
  // шлем 0x8F и берем номер модели из ответа (3 попытки)
  std::optional<int32_t> queryModelId();

private:
  std::string sendLocked(const std::vector<uint8_t> &msg, ChecksumType type);
  std::optional<Packet> readLocked(std::string &error);

  // ждем пока тот кто отправил запрос на чтение заберет ответ (до 1.5с)
  void waitForPendingAnswer(std::unique_lock<std::mutex> &lock);
  void markSent(const std::vector<uint8_t> &msg);

  std::unique_ptr<FeatureChannel> channel_;
  // одна команда за раз
  std::mutex mutex_;
  // вебка шлет запрос и читает ответ двумя отдельными вызовами
  // пока ответ на запрос чтения не забрали чужой запрос ждет чтобы вторая вкладка не получила чужой ответ
  bool answerPending_ = false;
  std::chrono::steady_clock::time_point pendingUntil_;
  std::condition_variable answerTaken_;
};
