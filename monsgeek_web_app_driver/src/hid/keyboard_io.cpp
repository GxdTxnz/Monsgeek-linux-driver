#include <chrono>
#include <thread>
#include <algorithm>
#include "hid/keyboard_io.hpp"

static const uint8_t kCmdGetModelId = 0x8F;
static const int kModelIdAttempts = 3;
// время "на разгон" чтобы клавиатура успела подготовить ответ
static const auto kAnswerDelay = std::chrono::milliseconds(10);
// у запросов чтения (GET) номер от 0x80 после них вебка забирает ответ
// после запросов записи - нет
static const uint8_t kFirstGetCmd = 0x80;
// время ожидания чтобы вебка забрала ответ
static const auto kPendingTimeout = std::chrono::milliseconds(1500);

KeyboardIo::KeyboardIo(std::unique_ptr<FeatureChannel> channel) : channel_(std::move(channel)) {}

void KeyboardIo::waitForPendingAnswer(std::unique_lock<std::mutex> &lock)
{
  while (answerPending_)
  {
    if (answerTaken_.wait_until(lock, pendingUntil_) == std::cv_status::timeout)
    {
      answerPending_ = false;
    }
  }
}

void KeyboardIo::markSent(const std::vector<uint8_t> &msg)
{
  if (!msg.empty() && msg[0] >= kFirstGetCmd)
  {
    answerPending_ = true;
    pendingUntil_ = std::chrono::steady_clock::now() + kPendingTimeout;
  }
}

std::string KeyboardIo::send(const std::vector<uint8_t> &msg, ChecksumType type)
{
  std::unique_lock<std::mutex> lock(mutex_);
  waitForPendingAnswer(lock);
  std::string error = sendLocked(msg, type);

  if (error.empty())
  {
    markSent(msg);
  }

  return error;
}

std::string KeyboardIo::sendRaw(const std::vector<uint8_t> &msg)
{
  std::unique_lock<std::mutex> lock(mutex_);
  waitForPendingAnswer(lock);
  std::string error = sendLocked(msg, ChecksumType::None);

  if (error.empty())
  {
    markSent(msg);
  }

  return error;
}

std::optional<Packet> KeyboardIo::read(std::string &error)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto answer = readLocked(error);
  answerPending_ = false;
  answerTaken_.notify_all();
  return answer;
}

std::optional<int32_t> KeyboardIo::queryModelId()
{
  std::unique_lock<std::mutex> lock(mutex_);
  waitForPendingAnswer(lock);

  for (int attempt = 0; attempt < kModelIdAttempts; ++attempt)
  {
    if (!sendLocked({kCmdGetModelId}, ChecksumType::Bit7).empty())
    {
      continue;
    }

    std::this_thread::sleep_for(kAnswerDelay);
    std::string error;
    auto answer = readLocked(error);

    if (answer && (*answer)[0] == kCmdGetModelId)
    {
      return parseModelId(*answer);
    }
  }

  return std::nullopt;
}

std::string KeyboardIo::sendLocked(const std::vector<uint8_t> &msg, ChecksumType type)
{
  std::string error;
  auto packet = buildPacket(msg, type, error);

  if (!packet)
  {
    return error;
  }

  if (!channel_->sendFeature(withReportId(*packet), error))
  {
    return error;
  }

  return "";
}

std::optional<Packet> KeyboardIo::readLocked(std::string &error)
{
  std::array<uint8_t, kPacketSize + 1> raw{};

  if (!channel_->getFeature(raw, error))
  {
    return std::nullopt;
  }

  Packet p{};
  std::copy(raw.begin() + 1, raw.end(), p.begin());
  return p;
}
