#include <algorithm>
#include "hid/packet.hpp"

// контрольная сумма
// складываем первые count байт и берем от суммы младший байт
// ответ = 255 - младший байт
// клавиатура складывает те же байты вместе с ответом и если пакет цел то выходит 0xFF
static uint8_t checksum(const Packet &p, size_t count)
{
  unsigned sum = 0;

  for (size_t i = 0; i < count; ++i)
  {
    sum += p[i];
  }

  return static_cast<uint8_t>(255 - (sum & 0xFF));
}

std::optional<Packet> buildPacket(const std::vector<uint8_t> &msg, ChecksumType type, std::string &error)
{
  if (msg.empty())
  {
    error = "empty message";
    return std::nullopt;
  }

  if (msg.size() > kPacketSize)
  {
    error = "message longer than 64 bytes";
    return std::nullopt;
  }

  Packet p{};
  std::copy(msg.begin(), msg.end(), p.begin());

  if (type == ChecksumType::Bit7)
  {
    p[7] = checksum(p, 7);
  }

  else if (type == ChecksumType::Bit8)
  {
    p[8] = checksum(p, 8);
  }

  return p;
}

std::array<uint8_t, kPacketSize + 1> withReportId(const Packet &packet)
{
  std::array<uint8_t, kPacketSize + 1> out{};
  std::copy(packet.begin(), packet.end(), out.begin() + 1);
  return out;
}

int32_t parseModelId(const Packet &answer)
{
  uint32_t id = static_cast<uint32_t>(answer[1]) | (static_cast<uint32_t>(answer[2]) << 8) |
                (static_cast<uint32_t>(answer[3]) << 16) | (static_cast<uint32_t>(answer[4]) << 24);

  if (id == 0xFFFFFFFF)
  {
    return -1;
  }

  return static_cast<int32_t>(id);
}
