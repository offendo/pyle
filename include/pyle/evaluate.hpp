#pragma once

#include "pyle/cache.hpp"
#include <lean/lean.h>
#include <string>
#include <vector>

namespace pyle {

/* Primary workhorse function */
lean_obj_res evaluate_one(
  const std::string &lean_code,
  lean_obj_arg state,
  uint32_t timeout);

/* Utility function to parse output from lean tuple object to std::tuple */
std::tuple<std::string, lean_object *, lean_object *>
parse_lean_output(b_lean_obj_arg lean_response);

/* Wrapper around evaluate_one which runs evaluate in parallel on threads*/
std::tuple<std::vector<std::string>, std::vector<long>, Cache *> evaluate_many(
  const std::vector<std::string> &lean_code,
  Cache *state_cache,
  uint32_t timeout,
  uint32_t n_workers);
} // namespace pyle
