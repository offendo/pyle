#pragma once
#include <cstdint>
#include <lean/lean.h>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <stdexcept>

namespace pyle {
class Cache {
public:
  std::mutex mutex;
  uint32_t size;
  std::list<std::string> lru;
  std::unordered_map<std::string, std::shared_ptr<lean_object>> cache;

  Cache(uint32_t size) : size(size) {
    if (size == 0) throw std::invalid_argument("cache_size must be positive");
  };
  std::shared_ptr<lean_object> get(const std::string header);
  // Borrows state; stores its own reference, marked for sharing across threads.
  std::shared_ptr<lean_object>
  put(const std::string header, lean_object *state);
  void erase(const std::string header);
  void erase_all();
};

std::shared_ptr<Cache> make_cache(uint32_t size);
} // namespace pyle
