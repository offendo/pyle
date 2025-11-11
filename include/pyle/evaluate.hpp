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
  std::optional<py::capsule> opt_cache,
  uint32_t timeout = 0,
  uint32_t cache_capacity = 5,
  uint32_t n_threads = 4);

py::tuple py_evaluate_test_new(
  const std::string &lean_code,
  std::optional<py::capsule> capsule,
  uint32_t timeout = 0);
} // namespace pyle
