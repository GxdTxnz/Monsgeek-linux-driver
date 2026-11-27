#include <set>
#include <iostream>
#include "devices/device_manager.hpp"

static const char kNotOnline[] = "dev is not online";

DeviceManager::DeviceManager(Scanner scan, Opener openChannel, bool startVenderReaders)
    : scan_(std::move(scan)), openChannel_(std::move(openChannel)), startVenderReaders_(startVenderReaders)
{
}

DeviceManager::~DeviceManager()
{
  std::map<std::string, Entry> entries;

  {
    std::lock_guard<std::mutex> lock(entriesMutex_);
    entries.swap(entries_);
  }

  entries.clear();
}

void DeviceManager::rescan()
{
  std::lock_guard<std::mutex> rescanLock(rescanMutex_);
  auto found = scan_();
  std::set<std::string> foundPaths;

  for (const auto &kb : found)
  {
    foundPaths.insert(kb.featurePath);
  }

  std::map<std::string, Entry> removed;
  std::vector<FoundKeyboard> added;

  {
    std::lock_guard<std::mutex> lock(entriesMutex_);

    for (auto it = entries_.begin(); it != entries_.end();)
    {
      if (foundPaths.count(it->first) == 0)
      {
        removed.insert(entries_.extract(it++));
      }

      else
      {
        ++it;
      }
    }

    for (const auto &kb : found)
    {
      if (entries_.count(kb.featurePath) == 0)
      {
        added.push_back(kb);
      }
    }
  }

  // для каждой клавиатуры есть свой фоновый поток при удалении которого нужно ждать пока он завершится
  // удаление делаем без лока тк поток в этот момент тоже может ждать лок и тогда получается
  // что тот поток ждет нас и наоборот
  bool anyRemoved = !removed.empty();
  removed.clear();

  if (anyRemoved)
  {
    notifyList(ListChange::Remove);
  }

  // открытие канала и определение номера модели тоже без локов тк это долго
  for (const auto &kb : added)
  {
    auto channel = openChannel_(kb.featurePath);

    if (!channel)
    {
      continue;
    }

    Entry entry{kb, -1, std::make_shared<KeyboardIo>(std::move(channel)), nullptr};
    auto id = entry.io->queryModelId();

    if (id)
    {
      entry.id = *id;
    }

    else
    {
      std::cerr << "keyboard " << kb.featurePath << " did not return the model number" << std::endl;
    }

    if (startVenderReaders_ && !kb.venderPath.empty())
    {
      entry.vender =
          std::make_unique<VenderReader>(kb.venderPath, [this](std::vector<uint8_t> msg) { notifyVender(msg); });
    }

    {
      std::lock_guard<std::mutex> lock(entriesMutex_);
      entries_.emplace(kb.featurePath, std::move(entry));
    }

    notifyList(ListChange::Add);
  }
}

std::vector<KeyboardInfo> DeviceManager::list()
{
  std::lock_guard<std::mutex> lock(entriesMutex_);
  return listLocked();
}

std::vector<KeyboardInfo> DeviceManager::listLocked()
{
  std::vector<KeyboardInfo> out;

  for (const auto &[path, entry] : entries_)
  {
    out.push_back({path, entry.id, entry.found.vid, entry.found.pid});
  }

  return out;
}

std::shared_ptr<KeyboardIo> DeviceManager::findIo(const std::string &path)
{
  std::lock_guard<std::mutex> lock(entriesMutex_);
  auto it = entries_.find(path);
  return it == entries_.end() ? nullptr : it->second.io;
}

std::string DeviceManager::send(const std::string &path, const std::vector<uint8_t> &msg, ChecksumType type)
{
  auto io = findIo(path);

  if (!io)
  {
    return kNotOnline;
  }

  auto error = io->send(msg, type);

  // ошибка как правило означает что клавиатуру отключили
  // выводим это через список
  if (!error.empty())
  {
    rescan();
  }

  return error;
}

std::string DeviceManager::sendRaw(const std::string &path, const std::vector<uint8_t> &msg)
{
  auto io = findIo(path);

  if (!io)
  {
    return kNotOnline;
  }

  auto error = io->sendRaw(msg);

  if (!error.empty())
  {
    rescan();
  }

  return error;
}

std::optional<Packet> DeviceManager::read(const std::string &path, std::string &error)
{
  auto io = findIo(path);

  if (!io)
  {
    error = kNotOnline;
    return std::nullopt;
  }

  auto packet = io->read(error);

  if (!packet)
  {
    rescan();
  }

  return packet;
}

int DeviceManager::subscribeList(ListCallback cb)
{
  std::lock_guard<std::recursive_mutex> lock(subscribersMutex_);
  int id = nextSubscriberId_++;
  listSubscribers_[id] = cb;
  // начальный список возвращаем под тем же локом чтобы событие Add не обогнало Init
  cb(ListChange::Init, list());
  return id;
}

int DeviceManager::subscribeVender(VenderCallback cb)
{
  std::lock_guard<std::recursive_mutex> lock(subscribersMutex_);
  int id = nextSubscriberId_++;
  venderSubscribers_[id] = std::move(cb);
  return id;
}

void DeviceManager::unsubscribe(int id)
{
  std::lock_guard<std::recursive_mutex> lock(subscribersMutex_);
  listSubscribers_.erase(id);
  venderSubscribers_.erase(id);
}

void DeviceManager::notifyList(ListChange change)
{
  std::lock_guard<std::recursive_mutex> lock(subscribersMutex_);
  auto current = list();
  // обходим КОПИЮ списка "подписчиков" тк обработчик может "отписаться" прямо во время обхода
  // при обходе по оригиналу удаленный элемент сломает цикл
  auto subscribers = listSubscribers_;

  for (auto &[id, cb] : subscribers)
  {
    if (listSubscribers_.count(id) != 0)
    {
      cb(change, current);
    }
  }
}

void DeviceManager::notifyVender(const std::vector<uint8_t> &msg)
{
  std::lock_guard<std::recursive_mutex> lock(subscribersMutex_);
  auto subscribers = venderSubscribers_;

  for (auto &[id, cb] : subscribers)
  {
    if (venderSubscribers_.count(id) != 0)
    {
      cb(msg);
    }
  }
}
