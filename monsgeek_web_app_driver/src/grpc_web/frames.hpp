#pragma once

// кадры gRPC-web: [флаг 1 байт][длина 4 байта, big-endian][данные]
// флаг 0x00 - сообщение protobuf, 0x80 - трейлеры (итог вызова: grpc-status и grpc-message)

#include <string>
#include <optional>

std::string encodeMessageFrame(const std::string &payload);
std::string encodeTrailerFrame(int status, const std::string &message);

// данные первого кадра, nullopt если кадр обрезан или его нет
std::optional<std::string> decodeFirstMessage(const std::string &body);
std::string base64Encode(const std::string &data);

/*
декодируем base64 в байты
пробелы и переносы скипаем
склеенные куски через "=" обрабатываем тк так присылает вебка
при лишних "=", обрезанных 4х и чужих символах возвращаем nullopt
*/
std::optional<std::string> base64Decode(const std::string &text);

/*
формат тела запроса определяем по заголовку Content-Type и отвечаем так же
text (application/grpc-web-text) - base64
binary (application/grpc-web) - байты как есть
*/
enum class WireFormat
{
  Binary,
  Text,
};

std::optional<WireFormat> wireFormatFromContentType(const std::string &contentType);

std::string contentTypeFor(WireFormat format);
