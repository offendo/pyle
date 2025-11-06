#pragma once
#include <cstdint>
#include <lean/lean.h>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace pyle {
class Cache {
private:
  std::mutex mutex;
  uint32_t capacity;
  std::list<std::string> lru;
  std::unordered_map<std::string, std::shared_ptr<lean_object>> cache;

public:
  Cache(uint32_t capacity) : capacity(capacity) {};
  std::shared_ptr<lean_object> get(const std::string header);
  void put(const std::string header, lean_object *state);
  void erase(const std::string header);
  void erase_all();
};

std::unique_ptr<Cache> make_cache(uint32_t capacity);
} // namespace pyle
