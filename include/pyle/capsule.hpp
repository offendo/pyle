#pragma once

#include <pybind11/pybind11.h>
#include <lean/lean.h>

namespace pyle {

// Decrement or free a lean_object*
void cleanup_lean_object(void *ptr);

// Pack a lean_object* into a py::capsule (with destructor)
pybind11::capsule pack_lean_object(lean_object *obj);

// Unpack the lean_object* out of a py::capsule
lean_object *unpack_lean_object(const pybind11::capsule &capsule);

} // namespace pyle
