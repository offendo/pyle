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
  std::optional<pybind11::capsule> state_cache = std::nullopt,
  uint32_t timeout = 0);
} // namespace pyle
