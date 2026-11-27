#pragma once

/*
список подключенных клавиатур
на какую слать команды
на какой применить изменения
*/

#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include "hid/hidraw.hpp"
#include "hid/discovery.hpp"
#include "hid/keyboard_io.hpp"

struct KeyboardInfo
{
  // путь служебного канала по которому вебка шлет команды
  std::string path;
  // номер модели из ответа на 0x8F
  // например FUN60 Ultra - 2387
  int32_t id;
  uint16_t vid;
  uint16_t pid;
};

// значения из DeviceListChangeType в driver.proto
enum class ListChange
{
  Init = 0,
  Add = 1,
  Remove = 2,
  Change = 3,
};

class Keyboards
{
public:
  using ListCallback = std::function<void(ListChange, std::vector<KeyboardInfo>)>;
  using VenderCallback = std::function<void(std::vector<uint8_t>)>;

  virtual ~Keyboards() = default;
  virtual std::vector<KeyboardInfo> list() = 0;
  // пустая строка = норм
  // "dev is not online" = неизвестная клавиатура
  virtual std::string send(const std::string &path, const std::vector<uint8_t> &msg, ChecksumType type) = 0;
  virtual std::string sendRaw(const std::string &path, const std::vector<uint8_t> &msg) = 0;
  virtual std::optional<Packet> read(const std::string &path, std::string &error) = 0;
  /*
  "подписка" для получения списка клавиатур
  cb сразу получает текущий список (Init)
  потом вызывается при каждом отключении/подключении/изменении клавиатуры (Add/Remove/Change)
  возвращаем номер "подписки" чтобы потом "отписаться"
  */
  virtual int subscribeList(ListCallback cb) = 0;
  virtual int subscribeVender(VenderCallback cb) = 0;
  virtual void unsubscribe(int id) = 0;
};

class DeviceManager : public Keyboards
{
public:
  using Scanner = std::function<std::vector<FoundKeyboard>()>;
  using Opener = std::function<std::unique_ptr<FeatureChannel>(const std::string &)>;

  DeviceManager(Scanner scan, Opener openChannel, bool startVenderReaders);
  ~DeviceManager() override;

  /*
  обновляем список клавиатур
  для новых спрашиваем номер модели
  тех которых больше нет забываем
  */
  void rescan();

  std::vector<KeyboardInfo> list() override;
  std::string send(const std::string &path, const std::vector<uint8_t> &msg, ChecksumType type) override;
  std::string sendRaw(const std::string &path, const std::vector<uint8_t> &msg) override;
  std::optional<Packet> read(const std::string &path, std::string &error) override;

  int subscribeList(ListCallback cb) override;
  int subscribeVender(VenderCallback cb) override;
  void unsubscribe(int id) override;

private:
  struct Entry
  {
    FoundKeyboard found;
    int32_t id;
    std::shared_ptr<KeyboardIo> io;
    std::unique_ptr<VenderReader> vender;
  };

  std::shared_ptr<KeyboardIo> findIo(const std::string &path);
  std::vector<KeyboardInfo> listLocked();

  void notifyList(ListChange change);
  void notifyVender(const std::vector<uint8_t> &msg);

  Scanner scan_;
  Opener openChannel_;
  bool startVenderReaders_;

  // список клавиатур
  std::mutex entriesMutex_;
  std::map<std::string, Entry> entries_;
  std::mutex rescanMutex_;

  // список "подписчиков"
  // рекурсивно чтобы на ходу можно было "отписаться"
  std::recursive_mutex subscribersMutex_;
  int nextSubscriberId_ = 1;
  std::map<int, ListCallback> listSubscribers_;
  std::map<int, VenderCallback> venderSubscribers_;
};
