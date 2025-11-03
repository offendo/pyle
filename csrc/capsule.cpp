#include "pyle/capsule.hpp"

namespace pyle {

void cleanup_lean_object(void *ptr) {
    lean_object *obj = static_cast<lean_object *>(ptr);
    if (obj) lean_dec(obj);
}

pybind11::capsule pack_lean_object(lean_object *obj) {
    lean_inc(obj);
    return pybind11::capsule(obj, "lean_object", [](PyObject *capsule) {
        cleanup_lean_object(PyCapsule_GetPointer(capsule, "lean_object"));
    });
}

lean_object *unpack_lean_object(const pybind11::capsule &capsule) {
    return static_cast<lean_object *>(
        PyCapsule_GetPointer(capsule.ptr(), "lean_object"));
}

} // namespace pyle
