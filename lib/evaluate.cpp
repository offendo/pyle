#include "pyle/evaluate.hpp"
#include "lean/lean.h"
#include "pyle/capsule.hpp"
#include "pyle/lean.hpp"
#include <chrono>
#include <iostream>
using namespace std::chrono;

namespace py = pybind11;

namespace pyle {

lean_obj_res evaluate_one(
  const std::string &lean_code,
  b_lean_obj_arg state,
  uint32_t timeout) {

  // Box the cstring into a lean_object. This object will be consumed by the
  // function
  lean_object *boxed = lean_mk_string(lean_code.c_str());

  // Wrap the state in an Option type to pass to Lean
  lean_object *opt_state;
  if (state) {
    // opt_state = some state
    opt_state = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(opt_state, 0, state);
  } else {
    // opt_state = none
    opt_state = lean_alloc_ctor(0, 0, 0);
  }

  // Return the lean output. Note that `evaluate` here is not pyle::evaluate() -
  // i.e., this is not a recursive call.
  return lean_evaluate(boxed, opt_state, timeout);
}

py::tuple py_evaluate(
  const std::string &lean_code,
  std::optional<py::capsule> capsule,
  uint32_t timeout) {

  // extract the given state, if any
  lean_object *state =
    capsule.has_value() ? unpack_lean_object(capsule.value()) : nullptr;

  // Run the actual evaluation, and grab the result out
  auto start = high_resolution_clock::now();
  lean_object *lean_response = evaluate_one(lean_code, state, timeout);
  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);
  std::cout << "Eval took: " << duration.count() << std::endl;

  // note that we return an Except object, so we have to dig one value in to
  // grab the internals before we can parse the output.
  lean_object *except_obj = lean_ctor_get(lean_response, 0);

  // If we have an error type, pull out the error and exit early.
  // This is mostly going to happen if Lean times out.
  if (lean_obj_tag(except_obj) == 0) {
    std::string err = lean_string_cstr(lean_ctor_get(except_obj, 0));
    return py::make_tuple("", "", err, duration, py::none());
  }

  // Otherwise, we got a real result back and we can parse the output.
  lean_object *result = lean_ctor_get(except_obj, 0);
  lean_object *new_state = lean_ctor_get(result, 0);
  lean_object *msgs = lean_ctor_get(result, 1);
  lean_object *trees = lean_ctor_get(result, 2);

  std::string msg_str = lean_string_cstr(msgs);
  std::string tree_str = lean_string_cstr(trees);

  // This step is a little subtlely weird. `pack_lean_object` calls lean_inc to
  // prevent the new_state from going out of scope. Then we call
  // lean_dec(lean_response) which decrements the new_state ref count also,
  // since it's a child object.
  py::capsule return_capsule = pack_lean_object(new_state);
  if (lean_response) {
    lean_dec(lean_response);
  }
  return py::make_tuple(msg_str, tree_str, "", duration, return_capsule);
}

} // namespace pyle
