#pragma once

#include "pyle/cache.hpp"
#include <lean/lean.h>
#include <pybind11/pybind11.h>

namespace pyle {

// Decrement or free a lean_object*
void cleanup_lean_object(void *ptr);

// Pack a lean_object* into a py::capsule (with destructor)
pybind11::capsule pack_lean_object(lean_object *obj);

// Unpack the lean_object* out of a py::capsule
lean_object *unpack_lean_object(const pybind11::capsule &capsule);

// Clean up the cache by decrementing the refs of all the lean_objects in there.
void cleanup_cache(void *ptr);

// Pack up the cache.
pybind11::capsule pack_cache(Cache *cache);

// Unpack the cache
std::unique_ptr<Cache> unpack_cache(const pybind11::capsule &capsule);
} // namespace pyle
