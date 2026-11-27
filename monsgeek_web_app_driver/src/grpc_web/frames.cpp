#include <cstdint>
#include "grpc_web/frames.hpp"

static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string frame(uint8_t flag, const std::string &payload)
{
  std::string out;
  uint32_t len = static_cast<uint32_t>(payload.size());
  out.push_back(static_cast<char>(flag));
  out.push_back(static_cast<char>((len >> 24) & 0xFF));
  out.push_back(static_cast<char>((len >> 16) & 0xFF));
  out.push_back(static_cast<char>((len >> 8) & 0xFF));
  out.push_back(static_cast<char>(len & 0xFF));
  out += payload;
  return out;
}

std::string encodeMessageFrame(const std::string &payload)
{
  return frame(0x00, payload);
}

std::string encodeTrailerFrame(int status, const std::string &message)
{
  return frame(0x80, "grpc-status:" + std::to_string(status) + "\r\ngrpc-message:" + message + "\r\n");
}

std::optional<std::string> decodeFirstMessage(const std::string &body)
{
  size_t pos = 0;

  while (pos + 5 <= body.size())
  {
    uint8_t flag = static_cast<uint8_t>(body[pos]);
    uint64_t len = (static_cast<uint64_t>(static_cast<uint8_t>(body[pos + 1])) << 24) |
                   (static_cast<uint64_t>(static_cast<uint8_t>(body[pos + 2])) << 16) |
                   (static_cast<uint64_t>(static_cast<uint8_t>(body[pos + 3])) << 8) |
                   static_cast<uint64_t>(static_cast<uint8_t>(body[pos + 4]));

    /*
    формат кадра: [флаг 1 байт][длина 4 байта][данные]
    длину присылает клиент
    проверяем что пришел весь кадр
    длина в 64 бит чтобы pos + 5 + len не переполнились
    */
    if (pos + 5 + len > body.size())
    {
      return std::nullopt;
    }

    if ((flag & 0x80) == 0)
    {
      return body.substr(pos + 5, len);
    }

    pos += 5 + len;
  }

  return std::nullopt;
}

std::string base64Encode(const std::string &data)
{
  std::string out;
  size_t i = 0;

  for (; i + 3 <= data.size(); i += 3)
  {
    uint32_t n = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8) |
                 static_cast<uint8_t>(data[i + 2]);
    out.push_back(kAlphabet[(n >> 18) & 63]);
    out.push_back(kAlphabet[(n >> 12) & 63]);
    out.push_back(kAlphabet[(n >> 6) & 63]);
    out.push_back(kAlphabet[n & 63]);
  }

  size_t rest = data.size() - i;

  if (rest == 1)
  {
    uint32_t n = static_cast<uint8_t>(data[i]) << 16;
    out.push_back(kAlphabet[(n >> 18) & 63]);
    out.push_back(kAlphabet[(n >> 12) & 63]);
    out += "==";
  }

  else if (rest == 2)
  {
    uint32_t n = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8);
    out.push_back(kAlphabet[(n >> 18) & 63]);
    out.push_back(kAlphabet[(n >> 12) & 63]);
    out.push_back(kAlphabet[(n >> 6) & 63]);
    out.push_back('=');
  }

  return out;
}

static int base64Value(char c)
{
  if (c >= 'A' && c <= 'Z')
  {
    return c - 'A';
  }

  if (c >= 'a' && c <= 'z')
  {
    return c - 'a' + 26;
  }

  if (c >= '0' && c <= '9')
  {
    return c - '0' + 52;
  }

  if (c == '+' || c == '-')
  {
    return 62;
  }

  if (c == '/' || c == '_')
  {
    return 63;
  }

  return -1;
}

std::optional<std::string> base64Decode(const std::string &text)
{
  std::string clean;

  // переносы строк и пробелы скипаем
  for (char c : text)
  {
    if (c != '\r' && c != '\n' && c != ' ')
    {
      clean.push_back(c);
    }
  }

  if (clean.size() % 4 != 0)
  {
    return std::nullopt;
  }

  std::string out;

  // вебка кодирует куски в base64 и склеивает их друг с другом
  // отсюда получаем что "=" может быть и в середине типа "aGk=b2s="
  // декодируем по 4 символа чтобы не сломаться на таком "="
  for (size_t i = 0; i < clean.size(); i += 4)
  {
    int v[4];
    int pad = 0;

    for (int k = 0; k < 4; ++k)
    {
      char c = clean[i + k];

      if (c == '=')
      {
        // "=" может быть только в конце 4ки и не больше двух подряд
        if (k < 2)
        {
          return std::nullopt;
        }

        v[k] = 0;
        ++pad;
        continue;
      }

      if (pad > 0)
      {
        return std::nullopt;
      }

      v[k] = base64Value(c);

      if (v[k] < 0)
      {
        return std::nullopt;
      }
    }

    uint32_t n = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
    out.push_back(static_cast<char>((n >> 16) & 0xFF));

    if (pad < 2)
    {
      out.push_back(static_cast<char>((n >> 8) & 0xFF));
    }

    if (pad < 1)
    {
      out.push_back(static_cast<char>(n & 0xFF));
    }
  }

  return out;
}

std::optional<WireFormat> wireFormatFromContentType(const std::string &contentType)
{
  if (contentType.rfind("application/grpc-web-text", 0) == 0)
  {
    return WireFormat::Text;
  }

  if (contentType.rfind("application/grpc-web", 0) == 0)
  {
    return WireFormat::Binary;
  }

  return std::nullopt;
}

std::string contentTypeFor(WireFormat format)
{
  return format == WireFormat::Text ? "application/grpc-web-text+proto" : "application/grpc-web+proto";
}
