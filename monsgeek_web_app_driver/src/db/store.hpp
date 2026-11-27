#pragma once

/*
простое хранилище в формате kv для вебки где она держит макросы профили и прочие настройки
вебка делит данные на несколько "баз"; dbPath - имя базы
у каждого dbPath свой файл ключи и значения
*/
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <optional>

class Store
{
public:
  explicit Store(std::string baseDir);

  // пустая строка = записано
  // иначе текст ошибки
  std::string insert(const std::string &dbPath, const std::string &key, const std::string &value);

  std::optional<std::string> get(const std::string &dbPath, const std::string &key);

  std::string remove(const std::string &dbPath, const std::string &key);

  std::vector<std::string> keys(const std::string &dbPath);

  // в том же порядке что keys
  std::vector<std::string> values(const std::string &dbPath);

private:
  using Table = std::map<std::string, std::string>;

  std::string fileFor(const std::string &dbPath) const;

  Table load(const std::string &dbPath);

  std::string save(const std::string &dbPath, const Table &table);

  std::string baseDir_;
  std::mutex mutex_;
};
