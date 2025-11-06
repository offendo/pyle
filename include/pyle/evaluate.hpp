#pragma once

#include <lean/lean.h>
#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>

namespace py = pybind11;

namespace pyle {

pybind11::tuple py_evaluate(
  const std::string &lean_code,
  std::optional<pybind11::capsule> initial_state = std::nullopt,
  uint32_t timeout = 0);

py::tuple py_evaluate_many(
  std::vector<std::string> &lean_code,
  std::optional<std::unordered_map<std::string, std::shared_ptr<lean_object>>>
    opt_cache,
  uint32_t timeout);

} // namespace pyle
