#include "pyle/capsule.hpp"
#include <iostream>

namespace pyle {

void cleanup_lean_object(void *ptr) {
  lean_object *obj = static_cast<lean_object *>(ptr);
  if (obj)
    lean_dec(obj);
}

pybind11::capsule pack_lean_object(lean_object *obj) {
  lean_inc(obj);
  return pybind11::capsule(obj, "lean_object", [](PyObject *capsule) {
    cleanup_lean_object(PyCapsule_GetPointer(capsule, "lean_object"));
  });
}

lean_object *unpack_lean_object(const pybind11::capsule &capsule) {
  lean_object *obj = static_cast<lean_object *>(
    PyCapsule_GetPointer(capsule.ptr(), "lean_object"));
  lean_inc(obj);
  return obj;
}

// void cleanup_cache(void *ptr) { static_cast<Cache *>(ptr)->erase_all(); }

pybind11::capsule pack_cache(Cache *cache) {
  // TODO increment the refs of all the objects in the capsule
  return pybind11::capsule(cache, "Cache", [](PyObject *capsule) {
    // cleanup_cache(PyCapsule_GetPointer(capsule, "Cache"));
  });
}

std::unique_ptr<Cache> unpack_cache(const pybind11::capsule &capsule) {
  Cache *cache =
    static_cast<Cache *>(PyCapsule_GetPointer(capsule.ptr(), "Cache"));
  return std::unique_ptr<Cache>(cache);
}

} // namespace pyle
