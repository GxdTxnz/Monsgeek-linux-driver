#pragma once

#include <mutex>
#include "devices/device_manager.hpp"

class FakeKeyboards : public Keyboards
{
public:
  std::vector<KeyboardInfo> devices;
  std::string lastPath;
  std::vector<uint8_t> lastMsg;
  ChecksumType lastType = ChecksumType::None;
  bool lastWasRaw = false;
  std::string sendError;
  std::string readError;
  Packet readAnswer{};

  std::vector<KeyboardInfo> list() override
  {
    return devices;
  }

  std::string send(const std::string &path, const std::vector<uint8_t> &msg, ChecksumType type) override
  {
    lastPath = path;
    lastMsg = msg;
    lastType = type;
    lastWasRaw = false;
    return sendError;
  }

  std::string sendRaw(const std::string &path, const std::vector<uint8_t> &msg) override
  {
    lastPath = path;
    lastMsg = msg;
    lastWasRaw = true;
    return sendError;
  }

  std::optional<Packet> read(const std::string &path, std::string &error) override
  {
    lastPath = path;

    if (!readError.empty())
    {
      error = readError;
      return std::nullopt;
    }

    return readAnswer;
  }

  int subscribeList(ListCallback cb) override
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    int id = nextId_++;
    lists_[id] = cb;
    cb(ListChange::Init, devices);
    return id;
  }

  int subscribeVender(VenderCallback cb) override
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    int id = nextId_++;
    venders_[id] = cb;
    return id;
  }

  void unsubscribe(int id) override
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    lists_.erase(id);
    venders_.erase(id);
  }

  void emitList(ListChange change)
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto copy = lists_;

    for (auto &[id, cb] : copy)
    {
      cb(change, devices);
    }
  }

  void emitVender(const std::vector<uint8_t> &msg)
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto copy = venders_;

    for (auto &[id, cb] : copy)
    {
      cb(msg);
    }
  }

  size_t subscriberCount()
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return lists_.size() + venders_.size();
  }

private:
  std::recursive_mutex mutex_;
  int nextId_ = 1;
  std::map<int, ListCallback> lists_;
  std::map<int, VenderCallback> venders_;
};
