#include <deque>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <httplib.h>
#include <condition_variable>
#include "grpc_web/frames.hpp"
#include "grpc_web/server.hpp"

/*
кол-во запросов которые сервер обрабатывает параллельно
каждая вкладка вебки держит 2 запроса: watchDevList и watchVender
берем кол-во с запасом чтобы на обычные запросы типа sendMsg всегда было место
*/
static const size_t kThreadPoolSize = 64;
// как часто проверять что вебка еще открыта если нет событий
static const auto kStreamPollInterval = std::chrono::seconds(1);
static const int kStatusInvalidArgument = 3;
static const char kRoutePrefix[] = "/driver.DriverGrpc/";

// очередь кадров одного открытого потока: Service -> HTTP-поток -> web.monsgeek.com
struct StreamQueue
{
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::string> frames;
  bool clientGone = false;
};

GrpcWebServer::GrpcWebServer(Service &service, Config config)
    : service_(service), config_(std::move(config)), server_(std::make_unique<httplib::Server>())
{
  server_->new_task_queue = []
  {
    return new httplib::ThreadPool(kThreadPoolSize);
  };
  setupRoutes();
}

GrpcWebServer::~GrpcWebServer()
{
  stop();
}

bool GrpcWebServer::start()
{
  if (config_.port == 0)
  {
    port_ = server_->bind_to_any_port(config_.host);
  }

  else if (server_->bind_to_port(config_.host, config_.port))
  {
    port_ = config_.port;
  }

  else
  {
    port_ = -1;
  }

  if (port_ <= 0)
  {
    return false;
  }

  thread_ = std::thread([this] { server_->listen_after_bind(); });
  server_->wait_until_ready();
  return true;
}

void GrpcWebServer::stop()
{
  stopping_ = true;

  if (thread_.joinable())
  {
    server_->stop();
    thread_.join();
  }
}

int GrpcWebServer::port() const
{
  return port_;
}

bool GrpcWebServer::originAllowed(const std::string &origin) const
{
  // ответы без Origin это ответы от локальных программы - у них и так есть доступ к hidraw
  if (origin.empty())
  {
    return true;
  }

  return std::find(config_.allowedOrigins.begin(), config_.allowedOrigins.end(), origin) !=
         config_.allowedOrigins.end();
}

static void addCors(httplib::Response &res, const std::string &origin)
{
  if (origin.empty())
  {
    return;
  }

  res.set_header("Access-Control-Allow-Origin", origin);
  res.set_header("Vary", "Origin");
  res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
  res.set_header("Access-Control-Allow-Headers", "content-type, x-grpc-web, x-user-agent, grpc-timeout");
  res.set_header("Access-Control-Expose-Headers", "grpc-status, grpc-message");
  // chrome спрашивает разрешение когда вебка лезет на localhost
  res.set_header("Access-Control-Allow-Private-Network", "true");
}

// каждый кадр в текстовом формате отдельно кодируем в base64
static std::string encodeForWire(const std::string &frame, WireFormat format)
{
  return format == WireFormat::Text ? base64Encode(frame) : frame;
}

static void replyStatus(httplib::Response &res, WireFormat format, int status, const std::string &message,
                        const std::string *payload)
{
  std::string body;

  if (payload)
  {
    body += encodeForWire(encodeMessageFrame(*payload), format);
  }

  body += encodeForWire(encodeTrailerFrame(status, message), format);
  res.set_header("grpc-status", std::to_string(status));
  res.set_header("grpc-message", message);
  res.status = 200;
  res.set_content(body, contentTypeFor(format));
}

void GrpcWebServer::setupRoutes()
{
  std::string pattern = std::string(kRoutePrefix) + "([A-Za-z0-9_]+)";

  // браузер перед запросом POST с нестандартными заголовками всегда спрашивает разрешение
  server_->Options(pattern,
                   [this](const httplib::Request &req, httplib::Response &res)
                   {
                     std::string origin = req.get_header_value("Origin");

                     if (!originAllowed(origin))
                     {
                       res.status = 403;
                       return;
                     }

                     addCors(res, origin);
                     res.status = 204;
                   });

  server_->Post(
      pattern,
      [this](const httplib::Request &req, httplib::Response &res)
      {
        std::string origin = req.get_header_value("Origin");

        if (!originAllowed(origin))
        {
          res.status = 403;
          return;
        }

        addCors(res, origin);
        std::string method = req.matches[1];
        WireFormat format =
            wireFormatFromContentType(req.get_header_value("Content-Type")).value_or(WireFormat::Binary);
        std::optional<std::string> body = req.body;

        if (format == WireFormat::Text)
        {
          body = base64Decode(req.body);
        }

        std::optional<std::string> request = body ? decodeFirstMessage(*body) : std::nullopt;

        if (!request)
        {
          replyStatus(res, format, kStatusInvalidArgument, "bad grpc-web body", nullptr);
          return;
        }

        if (!service_.isStream(method))
        {
          auto result = service_.unary(method, *request);
          replyStatus(res, format, result.status, result.message, result.status == 0 ? &result.payload : nullptr);
          return;
        }

        auto queue = std::make_shared<StreamQueue>();
        std::weak_ptr<StreamQueue> weakQueue = queue;
        int streamId = service_.openStream(method, *request,
                                           [weakQueue, format](const std::string &payload)
                                           {
                                             auto q = weakQueue.lock();

                                             if (!q)
                                             {
                                               return false;
                                             }

                                             std::lock_guard<std::mutex> lock(q->mutex);

                                             if (q->clientGone)
                                             {
                                               return false;
                                             }

                                             q->frames.push_back(encodeForWire(encodeMessageFrame(payload), format));
                                             q->cv.notify_one();
                                             return true;
                                           });

        res.status = 200;
        res.set_header("grpc-status", "0");

        res.set_chunked_content_provider(
            contentTypeFor(format),
            [this, queue](size_t, httplib::DataSink &sink)
            {
              std::deque<std::string> ready;

              {
                std::unique_lock<std::mutex> lock(queue->mutex);
                queue->cv.wait_for(lock, kStreamPollInterval, [&] { return !queue->frames.empty(); });
                ready.swap(queue->frames);
              }

              if (stopping_)
              {
                return false;
              }

              for (const auto &frame : ready)
              {
                if (!sink.write(frame.data(), frame.size()))
                {
                  return false;
                }
              }

              // если событий не было то проверяем открыта ли вебка monsgeek
              return sink.is_writable();
            },
            [this, queue, streamId](bool)
            {
              {
                std::lock_guard<std::mutex> lock(queue->mutex);
                queue->clientGone = true;
              }

              service_.closeStream(streamId);
            });
      });
}
