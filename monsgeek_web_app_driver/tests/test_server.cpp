// тесты HTTP-сервера gRPC-web
// настоящий сервер на свободном порту
// клиент - cpp-httplib

#include <cstdlib>
#include <httplib.h>
#include <filesystem>
#include "check.hpp"
#include "config.hpp"
#include "driver.pb.h"
#include "fake_keyboards.hpp"
#include "grpc_web/frames.hpp"
#include "grpc_web/server.hpp"

static const char kOrigin[] = "https://web.monsgeek.com";

static std::vector<std::string> g_dirs;

static std::string tempDir()
{
  char tmpl[] = "/tmp/monsgeek-server-test-XXXXXX";
  char *dir = mkdtemp(tmpl);

  if (dir == nullptr)
  {
    perror("mkdtemp");
    exit(2);
  }

  g_dirs.push_back(dir);
  return dir;
}

struct TestEnv
{
  FakeKeyboards keyboards;
  Store store{tempDir()};
  Service service{keyboards, store, "2.1.5"};
  std::unique_ptr<GrpcWebServer> server;
  std::unique_ptr<httplib::Client> client;

  TestEnv()
  {
    Config config;
    config.port = 0;
    server = std::make_unique<GrpcWebServer>(service, config);

    if (!server->start())
    {
      std::cout << "server did not start" << std::endl;
      exit(2);
    }

    client = std::make_unique<httplib::Client>("127.0.0.1", server->port());
    client->set_read_timeout(5, 0);
  }

  ~TestEnv()
  {
    server->stop();
  }
};

static std::string versionPayload(const std::string &v)
{
  driver::Version ver;
  ver.set_baseversion(v);
  return ver.SerializeAsString();
}

static void testUnaryText()
{
  TestEnv env;
  httplib::Headers h{{"Origin", kOrigin}};
  auto res = env.client->Post("/driver.DriverGrpc/getVersion", h, base64Encode(encodeMessageFrame("")),
                              "application/grpc-web-text");
  CHECK(res && res->status == 200);

  if (!res)
  {
    return;
  }

  CHECK(res->get_header_value("Access-Control-Allow-Origin") == kOrigin);
  CHECK(res->get_header_value("Content-Type") == "application/grpc-web-text+proto");
  auto body = base64Decode(res->body);
  CHECK(body.has_value());
  CHECK(decodeFirstMessage(*body) == std::optional<std::string>(versionPayload("2.1.5")));
  CHECK(body->find("grpc-status:0") != std::string::npos);
}

static void testUnaryBinary()
{
  TestEnv env;
  httplib::Headers h{{"Origin", kOrigin}};
  auto res = env.client->Post("/driver.DriverGrpc/getVersion", h, encodeMessageFrame(""), "application/grpc-web+proto");
  CHECK(res && res->status == 200);

  if (res)
  {
    CHECK(res->get_header_value("Content-Type") == "application/grpc-web+proto");
    CHECK(decodeFirstMessage(res->body) == std::optional<std::string>(versionPayload("2.1.5")));
  }
}

static void testSendMsgReachesKeyboard()
{
  TestEnv env;
  driver::SendMsg req;
  req.set_devicepath("/dev/hidraw9");
  req.set_msg(std::string("\x87", 1));
  httplib::Headers h{{"Origin", kOrigin}};
  auto res = env.client->Post("/driver.DriverGrpc/sendMsg", h,
                              base64Encode(encodeMessageFrame(req.SerializeAsString())), "application/grpc-web-text");
  CHECK(res && res->status == 200);
  CHECK(env.keyboards.lastPath == "/dev/hidraw9");
  CHECK(env.keyboards.lastMsg == std::vector<uint8_t>{0x87});
}

static void testBadOrigin()
{
  TestEnv env;
  httplib::Headers h{{"Origin", "https://evil.example"}};
  auto res = env.client->Post("/driver.DriverGrpc/sendMsg", h, base64Encode(encodeMessageFrame("")),
                              "application/grpc-web-text");
  CHECK(res && res->status == 403);
  CHECK(env.keyboards.lastPath.empty());
}

// запрос без Origin значит это не браузер а какая то локальная программа
static void testNoOrigin()
{
  TestEnv env;
  auto res = env.client->Post("/driver.DriverGrpc/getVersion", base64Encode(encodeMessageFrame("")),
                              "application/grpc-web-text");
  CHECK(res && res->status == 200);
}

static void testPreflight()
{
  TestEnv env;
  httplib::Headers h{{"Origin", kOrigin}, {"Access-Control-Request-Method", "POST"}};
  auto res = env.client->Options("/driver.DriverGrpc/sendMsg", h);
  CHECK(res && res->status == 204);

  if (res)
  {
    CHECK(res->get_header_value("Access-Control-Allow-Origin") == kOrigin);
    CHECK(res->get_header_value("Access-Control-Allow-Headers").find("x-grpc-web") != std::string::npos);
    CHECK(res->get_header_value("Access-Control-Expose-Headers").find("grpc-status") != std::string::npos);
    CHECK(res->get_header_value("Access-Control-Allow-Private-Network") == "true");
  }

  httplib::Headers bad{{"Origin", "https://evil.example"}, {"Access-Control-Request-Method", "POST"}};
  auto denied = env.client->Options("/driver.DriverGrpc/sendMsg", bad);
  CHECK(denied && denied->status == 403);
}

static void testGarbageBody()
{
  TestEnv env;
  httplib::Headers h{{"Origin", kOrigin}};
  auto res = env.client->Post("/driver.DriverGrpc/sendMsg", h, "@@@ не base64", "application/grpc-web-text");
  CHECK(res && res->status == 200);

  if (res)
  {
    auto trailer = base64Decode(res->body);
    CHECK(trailer.has_value() && trailer->find("grpc-status:3") != std::string::npos);
  }

  auto cut = env.client->Post("/driver.DriverGrpc/sendMsg", h, base64Encode(encodeMessageFrame("abc").substr(0, 6)),
                              "application/grpc-web-text");
  CHECK(cut && cut->status == 200);

  if (cut)
  {
    auto trailer = base64Decode(cut->body);
    CHECK(trailer.has_value() && trailer->find("grpc-status:3") != std::string::npos);
  }
}

static void testUnknownMethod()
{
  TestEnv env;
  httplib::Headers h{{"Origin", kOrigin}};
  auto res = env.client->Post("/driver.DriverGrpc/noSuchMethod", h, base64Encode(encodeMessageFrame("")),
                              "application/grpc-web-text");
  CHECK(res && res->status == 200);

  if (res)
  {
    auto trailer = base64Decode(res->body);
    CHECK(trailer.has_value() && trailer->find("grpc-status:12") != std::string::npos);
  }
}

// открываем поток, берем первый кусок и бросаем соединение (типа как при закрытии вкладки)
static std::string takeFirstChunkAndLeave(int port, const std::string &method)
{
  httplib::Client c("127.0.0.1", port);
  c.set_read_timeout(2, 0);
  httplib::Request req;
  req.method = "POST";
  req.path = "/driver.DriverGrpc/" + method;
  req.headers = {{"Origin", kOrigin}, {"Content-Type", "application/grpc-web-text"}};
  req.body = base64Encode(encodeMessageFrame(""));
  std::string got;
  req.content_receiver = [&](const char *data, size_t len, uint64_t, uint64_t)
  {
    got.append(data, len);
    return false;
  };
  c.send(req);
  return got;
}

static void testStreamFirstFrame()
{
  TestEnv env;
  env.keyboards.devices = {{"/dev/hidraw9", 2387, 0x3151, 0x5030}};
  auto got = takeFirstChunkAndLeave(env.server->port(), "watchDevList");
  auto frame = base64Decode(got);
  CHECK(frame.has_value());

  if (frame)
  {
    auto payload = decodeFirstMessage(*frame);
    driver::DeviceList list;
    CHECK(payload.has_value() && list.ParseFromString(*payload));
    CHECK(list.devlist_size() == 1 && list.devlist(0).dev().id() == 2387);
  }
}

// больше подключений чем потоков в пуле
// если поток сервера не освобождается после отключения клиента то тест зависнет
static void testStreamClientGoneFreesThread()
{
  TestEnv env;
  env.keyboards.devices = {{"/dev/hidraw9", 2387, 0x3151, 0x5030}};

  for (int i = 0; i < 80; ++i)
  {
    CHECK(!takeFirstChunkAndLeave(env.server->port(), "watchDevList").empty());
  }

  httplib::Headers h{{"Origin", kOrigin}};
  auto res = env.client->Post("/driver.DriverGrpc/getVersion", h, base64Encode(encodeMessageFrame("")),
                              "application/grpc-web-text");
  CHECK(res && res->status == 200);
}

int main(int argc, char **argv)
{
  const TestCase tests[] = {
      {"unary_text", testUnaryText},
      {"unary_binary", testUnaryBinary},
      {"send_msg_reaches_keyboard", testSendMsgReachesKeyboard},
      {"bad_origin", testBadOrigin},
      {"no_origin", testNoOrigin},
      {"preflight", testPreflight},
      {"garbage_body", testGarbageBody},
      {"unknown_method", testUnknownMethod},
      {"stream_first_frame", testStreamFirstFrame},
      {"stream_client_gone_frees_thread", testStreamClientGoneFreesThread},
  };
  int rc = runTests(tests, sizeof(tests) / sizeof(tests[0]), argc, argv);

  for (const auto &d : g_dirs)
  {
    std::filesystem::remove_all(d);
  }

  return rc;
}
