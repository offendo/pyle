#pragma once

#include <lean/lean.h>
#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>

namespace pyle {

lean_obj_res evaluate_one(
  const std::string &lean_code,
  b_lean_obj_arg state,
  uint32_t timeout);

pybind11::tuple py_evaluate(
  const std::string &lean_code,
  std::optional<pybind11::capsule> initial_state = std::nullopt,
  uint32_t timeout = 0);
} // namespace pyle
