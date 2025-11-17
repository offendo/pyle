#pragma once
#include <cstdint>
#include <lean/lean.h>
#include <list>
#include <memory>
#include <mutex>
#include <pybind11/pybind11.h>
#include <string>
#include <unordered_map>

namespace pyle {
class Cache {
private:
public:
  std::mutex mutex;
  uint32_t capacity;
  std::list<std::string> lru;
  std::unordered_map<std::string, std::shared_ptr<lean_object>> cache;

  Cache(uint32_t capacity) : capacity(capacity) {};
  std::shared_ptr<lean_object> get(const std::string header);
  std::shared_ptr<lean_object>
  put(const std::string header, lean_object *state);
  void erase(const std::string header);
  void erase_all();
  pybind11::dict to_dict();
};

std::shared_ptr<Cache> make_cache(uint32_t capacity);
std::shared_ptr<Cache> from_dict(pybind11::dict dict, size_t capacity);
} // namespace pyle
