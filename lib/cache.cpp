#include "pyle/cache.hpp"
#include "pyle/capsule.hpp"
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <lean/lean.h>
#include <memory>
#include <mutex>
#include <string>

namespace py = pybind11;
namespace pyle {

/* Makes a cache object and returns a unique pointer to it. */
std::shared_ptr<Cache> make_cache(uint32_t capacity) {
  return std::make_shared<Cache>(capacity);
};

/* Get state associated with header. */
std::shared_ptr<lean_object> Cache::get(const std::string header) {
  const std::lock_guard<std::mutex> lock(mutex);

  // check if it exists. If not, return nullptr
  if (cache.find(header) == cache.end()) {
    return nullptr;
  }

  // Otherwise grab it
  std::shared_ptr<lean_object> state = cache[header];

  // and update the LRU
  auto it = std::find(lru.begin(), lru.end(), header);
  if (it != lru.end()) {
    lru.erase(it);
  }
  lru.push_back(header);

  return state;
}

/* Add new (header,state) pair in the cache, erasing LRU element if needed.*/
std::shared_ptr<lean_object>
Cache::put(const std::string header, lean_object *state) {
  const std::lock_guard<std::mutex> lock(mutex);

  // if it's already in the cache, just move the header to the end of the LRU
  // and nothing else.
  if (cache.find(header) != cache.end()) {
    auto it = std::find(lru.begin(), lru.end(), header);
    if (it != lru.end()) {
      lru.erase(it);
    }
    lru.push_back(header);
    return cache[header];
  } else {
    // Otherwise, we have to add it to the cache. If it's at capacity, gotta pop
    // the LRU element.
    if (cache.size() == capacity) {
      std::string lru_header = lru.front();
      cache.erase(lru_header);
      lru.pop_front();
    }

    // otherwise insert it
    auto new_state = std::shared_ptr<lean_object>(state, [](lean_object *s) {
      // std::cout << "Deleting shared pointer. Ref count: " << s->m_rc <<
      // std::endl;
      lean_dec(s);
    });
    cache.insert_or_assign(header, new_state);
    // Note - we return the value we just inserted because we will need to use
    // it to process the associated theorem. Instead of doing a second get()
    // call, just return here to ensure thread safety. Otherwise, it might
    // happen that we return from this function, another thread pops this
    // header, and then we fail the get() call.
    return new_state;
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
  cache.erase(header);
}

void Cache::erase_all() {
  for (auto &it : cache) {
    erase(it.first);
  }
}

std::shared_ptr<Cache> from_dict(pybind11::dict dict, size_t capacity) {
  std::shared_ptr<Cache> cache = make_cache(capacity);
  for (auto &pair : dict) {
    auto key = pair.first.cast<std::string>();
    lean_object *env = static_cast<lean_object *>(
      PyCapsule_GetPointer(pair.second.ptr(), "lean_object"));
    cache->put(key, env);
  }
  return cache;
}
pybind11::dict Cache::to_dict() {
  std::lock_guard<std::mutex> lock(mutex);
  py::dict dict;
  for (auto &[header, env] : cache) {
    py::capsule capsule = pack_lean_object(env.get());
    dict[py::cast(header)] = capsule;
  }
  return dict;
}

} // namespace pyle
