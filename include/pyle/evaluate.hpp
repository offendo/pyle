#pragma once

#include <lean/lean.h>
#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>

namespace py = pybind11;

namespace pyle {

py::tuple py_evaluate(
  const std::string &lean_code,
  std::optional<pybind11::capsule> state_cache = std::nullopt,
  uint32_t timeout = 0);
py::tuple py_evaluate_many(
  const std::vector<std::string> &lean_code,
  std::optional<pybind11::capsule> state_cache = std::nullopt,
  uint32_t timeout = 0,
  uint32_t n_workers = 1,
  uint32_t cache_capacity = 0);
} // namespace pyle
