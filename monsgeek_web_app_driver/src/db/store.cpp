#include <cstdint>
#include <fstream>
#include <iterator>
#include <filesystem>
#include "db/store.hpp"
/*
простое хранилище в формате kv для вебки где она держит макросы профили и прочие настройки
вебка делит данные на несколько "баз"; dbPath - имя базы
у каждого dbPath свой файл ключи и значения
*/
namespace fs = std::filesystem;

Store::Store(std::string baseDir) : baseDir_(std::move(baseDir)) {}

std::string Store::fileFor(const std::string &dbPath) const
{
  static const char hex[] = "0123456789abcdef";
  std::string name;

  for (unsigned char c : dbPath)
  {
    name.push_back(hex[c >> 4]);
    name.push_back(hex[c & 0xF]);
  }

  return baseDir_ + "/" + (name.empty() ? "_" : name) + ".db";
}

static void putU32(std::string &out, uint32_t v)
{
  for (int i = 0; i < 4; ++i)
  {
    out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
  }
}

static bool getU32(const std::string &in, size_t &pos, uint32_t &v)
{
  if (pos + 4 > in.size())
  {
    return false;
  }

  v = 0;

  for (int i = 0; i < 4; ++i)
  {
    v |= static_cast<uint32_t>(static_cast<uint8_t>(in[pos + i])) << (8 * i);
  }

  pos += 4;
  return true;
}

// читаем файл хранилища для одного dbPath (например ~/.local/share/monsgeek-bridge/<имя dbPath в hex>.db)
// формат: [длина ключа][ключ][длина значения][значение]
// длина - 4 байта в little-endian: 08 00 00 00 = 8
// обрезанный хвост (разрыв посреди записи) просто отбрасывается
Store::Table Store::load(const std::string &dbPath)
{
  std::ifstream f(fileFor(dbPath), std::ios::binary);
  std::string data(std::istreambuf_iterator<char>(f), {});
  Table table;
  size_t pos = 0;

  while (pos < data.size())
  {
    uint32_t len = 0;

    if (!getU32(data, pos, len) || pos + len > data.size())
    {
      break;
    }

    std::string key = data.substr(pos, len);
    pos += len;

    if (!getU32(data, pos, len) || pos + len > data.size())
    {
      break;
    }

    table[key] = data.substr(pos, len);
    pos += len;
  }

  return table;
}

std::string Store::save(const std::string &dbPath, const Table &table)
{
  std::error_code ec;
  fs::create_directories(baseDir_, ec);
  std::string data;

  for (const auto &[key, value] : table)
  {
    putU32(data, static_cast<uint32_t>(key.size()));
    data += key;
    putU32(data, static_cast<uint32_t>(value.size()));
    data += value;
  }

  // сначала во временный файл потом подменяем в основном
  // чтобы старый файл уцелел при разрыве
  std::string path = fileFor(dbPath);
  std::string tmp = path + ".tmp";

  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);

    if (!f.write(data.data(), static_cast<std::streamsize>(data.size())))
    {
      return "write error " + tmp;
    }
  }

  fs::rename(tmp, path, ec);

  if (ec)
  {
    return "write error " + path + ": " + ec.message();
  }

  return "";
}

std::string Store::insert(const std::string &dbPath, const std::string &key, const std::string &value)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto table = load(dbPath);
  table[key] = value;
  return save(dbPath, table);
}

std::optional<std::string> Store::get(const std::string &dbPath, const std::string &key)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto table = load(dbPath);
  auto it = table.find(key);

  if (it == table.end())
  {
    return std::nullopt;
  }

  return it->second;
}

std::string Store::remove(const std::string &dbPath, const std::string &key)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto table = load(dbPath);

  if (table.erase(key) == 0)
  {
    return "";
  }

  return save(dbPath, table);
}

std::vector<std::string> Store::keys(const std::string &dbPath)
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> out;

  for (const auto &[key, value] : load(dbPath))
  {
    out.push_back(key);
  }

  return out;
}

std::vector<std::string> Store::values(const std::string &dbPath)
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> out;

  for (const auto &[key, value] : load(dbPath))
  {
    out.push_back(value);
  }

  return out;
}
