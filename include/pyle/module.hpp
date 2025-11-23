#include "pyle/cache.hpp"
#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace pyle {
pybind11::capsule pack_lean_object(lean_object *obj);
lean_object *unpack_lean_object(const pybind11::capsule &capsule);
py::tuple py_evaluate_one(
  const std::string &lean_code,
  std::optional<py::dict> dict,
  uint32_t timeout,
  uint32_t cache_size);

py::tuple py_evaluate_many(
  const std::vector<std::string> &lean_code,
  std::optional<py::dict> dict,
  uint32_t timeout = 0,
  uint32_t n_workers = 1,
  uint32_t cache_size = 0);

py::tuple py_parse_header_and_body(const std::string &s);

pybind11::dict to_dict(Cache *cache);
std::shared_ptr<Cache> from_dict(pybind11::dict dict, size_t size);

// Initialize the Lean runtime and our Pyle frontend
void initialize();

} // namespace pyle
