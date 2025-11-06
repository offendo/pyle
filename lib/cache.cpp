#include "pyle/cache.hpp"
#include <iostream>
#include <lean/lean.h>
#include <memory>
#include <mutex>
#include <string>

namespace pyle {

/* Makes a cache object and returns a unique pointer to it. */
std::unique_ptr<Cache> make_cache(uint32_t capacity) {
  return std::make_unique<Cache>(capacity);
};

/* Get state associated with header. */
std::shared_ptr<lean_object> Cache::get(const std::string header) {
  const std::lock_guard<std::mutex> lock(mutex);
  if (cache.find(header) == cache.end()) {
    return nullptr;
  }
  std::shared_ptr<lean_object> state = cache[header];

  // Erase the position of the header in the lru, and move it to the end since
  // it was most recently used;
  auto it = std::find(lru.begin(), lru.end(), header);
  if (it != lru.end()) {
    lru.erase(it);
  }
  lru.push_back(header);
  return state;
}

/* Add new (header,state) pair in the cache, erasing LRU element if needed.*/
void Cache::put(const std::string header, lean_object *state) {
  const std::lock_guard<std::mutex> lock(mutex);
  // if it's already in the cache, just move the header to the end of the LRU
  // and nothing else.
  if (cache.find(header) != cache.end()) {
    auto it = std::find(lru.begin(), lru.end(), header);
    if (it != lru.end()) {
      lru.erase(it);
    }
    lru.push_back(header);
  } else {
    // Otherwise, we have to add it to the cache. If it's at capacity, gotta pop
    // the LRU element.
    if (cache.size() == capacity) {
      std::string lru_header = lru.front();
      std::cout << "popping header with use count "
                << cache[lru_header].use_count() << std::endl;
      cache.erase(lru_header);
      lru.pop_front();
    }

    // otherwise insert it
    cache.insert_or_assign(
      header,
      std::shared_ptr<lean_object>(state, [](lean_object *s) { lean_dec(s); }));
  }
}

void Cache::erase(const std::string header) {
  const std::lock_guard<std::mutex> lock(mutex);
  // If it's not in there, don't bother doing anything.
  if (cache.find(header) == cache.end()) {
    return;
  }

  // Otherwise, yeet
  auto it = std::find(lru.begin(), lru.end(), header);
  if (it != lru.end()) {
    lru.erase(it);
  }
  std::cout << "popping header with use count " << cache[header].use_count()
            << std::endl;
  cache.erase(header);
}

void Cache::erase_all() {
  for (auto &it : cache) {
    erase(it.first);
  }
}

} // namespace pyle
