#pragma once

#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>

namespace pyle {

// Evaluate a single snippet of Lean code, returning (msgs, trees, new_env,
// error?)
pybind11::tuple
evaluate_one(const std::string &lean_code,
             std::optional<pybind11::capsule> initial_state = std::nullopt,
             uint32_t timeout = 0);

// Evaluate multiple snippets in the same state; returns vector of messages
std::vector<const char *>
evaluate_many(const std::vector<std::string> &lean_code,
              pybind11::capsule *initial_state, uint32_t timeout = 0);

} // namespace pyle
