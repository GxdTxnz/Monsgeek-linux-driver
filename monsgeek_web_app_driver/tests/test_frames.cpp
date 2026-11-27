// тесты кадров gRPC-web и base64

#include <cstdint>
#include "check.hpp"
#include "grpc_web/frames.hpp"

static void testMessageFrame()
{
  auto f = encodeMessageFrame("abc");
  CHECK(f == std::string("\x00\x00\x00\x00\x03"
                         "abc",
                         8));
}

static void testTrailer()
{
  auto f = encodeTrailerFrame(0, "");
  CHECK(static_cast<uint8_t>(f[0]) == 0x80);
  CHECK(f.substr(5) == "grpc-status:0\r\ngrpc-message:\r\n");
}

static void testDecodeRoundTrip()
{
  CHECK(decodeFirstMessage(encodeMessageFrame("hello")) == std::optional<std::string>("hello"));
  CHECK(decodeFirstMessage(encodeMessageFrame("")) == std::optional<std::string>(""));
}

static void testDecodeTruncated()
{
  auto f = encodeMessageFrame("hello");
  CHECK(!decodeFirstMessage(f.substr(0, 7)).has_value());
  CHECK(!decodeFirstMessage("").has_value());
}

// длина больше 4 ГБ не должна приводить к переполнению и чтению мимо буфера
static void testDecodeHugeLength()
{
  CHECK(!decodeFirstMessage(std::string("\x00\xff\xff\xff\xff", 5)).has_value());
}

static void testBase64()
{
  CHECK(base64Encode(std::string("\x00\x01\xff", 3)) == "AAH/");
  CHECK(base64Decode("AAH/") == std::optional<std::string>(std::string("\x00\x01\xff", 3)));
  CHECK(base64Decode("aGk=") == std::optional<std::string>("hi"));
  CHECK(base64Encode("hi") == "aGk=");
  CHECK(base64Decode("") == std::optional<std::string>(""));
  CHECK(!base64Decode("@@@").has_value());
}

// браузер может склеить несколько кусков base64 с дополнением "="
static void testBase64Concatenated()
{
  CHECK(base64Decode("aGk=aGk=") == std::optional<std::string>("hihi"));
}

static void testContentType()
{
  CHECK(wireFormatFromContentType("application/grpc-web-text") == WireFormat::Text);
  CHECK(wireFormatFromContentType("application/grpc-web-text+proto") == WireFormat::Text);
  CHECK(wireFormatFromContentType("application/grpc-web+proto") == WireFormat::Binary);
  CHECK(wireFormatFromContentType("application/grpc-web") == WireFormat::Binary);
  CHECK(!wireFormatFromContentType("text/plain").has_value());
  CHECK(contentTypeFor(WireFormat::Text) == "application/grpc-web-text+proto");
  CHECK(contentTypeFor(WireFormat::Binary) == "application/grpc-web+proto");
}

int main(int argc, char **argv)
{
  const TestCase tests[] = {
      {"message_frame", testMessageFrame},
      {"trailer", testTrailer},
      {"decode_round_trip", testDecodeRoundTrip},
      {"decode_truncated", testDecodeTruncated},
      {"decode_huge_length", testDecodeHugeLength},
      {"base64", testBase64},
      {"base64_concatenated", testBase64Concatenated},
      {"content_type", testContentType},
  };
  return runTests(tests, sizeof(tests) / sizeof(tests[0]), argc, argv);
}
