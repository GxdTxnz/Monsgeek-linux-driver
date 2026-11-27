#pragma once

// 64 байтный пакет для клавиатуры: команда с параметрами (добиваем нулями если надо) + контрольная сумма

#include <array>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <optional>

// значения CheckSumType в driver.proto
enum class ChecksumType
{
  Bit7 = 0,
  Bit8 = 1,
  None = 2,
};

constexpr size_t kPacketSize = 64;

using Packet = std::array<uint8_t, kPacketSize>;

// добиваем msg нулями до 64 байт и дописывает КС
// пустой пакет или длиннее 64 = nullopt и текст в error
std::optional<Packet> buildPacket(const std::vector<uint8_t> &msg, ChecksumType type, std::string &error);

// hidraw ждет первым байтом номер отчета а у клавиатура номеров нет поэтому пишем 0
// в клаву уходят только наши 64 байта
std::array<uint8_t, kPacketSize + 1> withReportId(const Packet &packet);

// номер модели из ответа на 0x8F
// байты 1..4, little-endian, 0xFFFFFFFF = "не знаю" и дает -1
int32_t parseModelId(const Packet &answer);
