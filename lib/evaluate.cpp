#include "pyle/evaluate.hpp"
#include "lean/lean.h"
#include "pyle/cache.hpp"
#include "pyle/capsule.hpp"
#include "pyle/lean.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <unordered_map>
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

std::tuple<std::string, std::string, std::string, lean_object *>
parse_lean_output(lean_object *lean_response) {
  // note that we return an Except object, so we have to dig one value in to
  // grab the internals before we can parse the output.
  lean_object *except_obj = lean_ctor_get(lean_response, 0);

  // If we have an error type, pull out the error and exit early.
  // This is mostly going to happen if Lean times out.
  if (lean_obj_tag(except_obj) == 0) {
    std::string err = lean_string_cstr(lean_ctor_get(except_obj, 0));
    return std::make_tuple("", "", err, nullptr);
  }

  // Otherwise, we got a real result back and we can parse the output.
  lean_object *result = lean_ctor_get(except_obj, 0);
  lean_object *new_state = lean_ctor_get(result, 0);
  lean_object *msgs = lean_ctor_get(result, 1);
  lean_object *trees = lean_ctor_get(result, 2);

  std::string msg_str = lean_string_cstr(msgs);
  std::string tree_str = lean_string_cstr(trees);
  return std::make_tuple(msg_str, tree_str, "", new_state);
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
  auto duration = duration_cast<milliseconds>(stop - start).count();
  std::cout << "Eval took: " << duration << std::endl;

  auto [msgs, tree, err, new_state] = parse_lean_output(lean_response);

  // This step is a little subtlely weird. We call lean_inc to on the new_state
  // to increment the ref count. Then we call lean_dec(lean_response) which
  // decrements new_state's ref count, since it's a child object of
  // lean_response. This balances the change of new_state's refs to 0 so we keep
  // it in memory.
  lean_inc(new_state);
  py::capsule return_capsule = pack_lean_object(new_state);
  if (lean_response) {
    lean_dec(lean_response);
  }
  return py::make_tuple(msgs, tree, err, duration, return_capsule);
}

py::tuple py_evaluate_many(
  std::vector<std::string> &lean_code,
  std::optional<py::capsule> opt_cache,
  uint32_t timeout,
  uint32_t cache_capacity = 5) {

  std::unique_ptr<Cache> state_cache = opt_cache.has_value()
                                         ? unpack_cache(opt_cache.value())
                                         : make_cache(cache_capacity);

  // Loop through and run each input. Collate them all in this vector
  std::vector<std::tuple<std::string, std::string, std::string, long long>>
    results(lean_code.size());

  for (auto &thm : lean_code) {
    std::string header = "";
    std::shared_ptr<lean_object> state = state_cache->get(header);
    if (state) {
      std::cout << "Got item from the cache: " << state.use_count()
                << std::endl;
    } else {
      std::cout << "Null state from cache" << state.use_count() << std::endl;
    }

    // Run the actual evaluation, and grab the result out
    lean_object *lean_response = evaluate_one(thm, state.get(), timeout);

    // Parse the output.
    auto [msgs, tree, err, new_state] = parse_lean_output(lean_response);

    // **Important**: This lean_dec will delete new_state! Which is good because
    // we don't want it - it'll just eat up memory. In the future, if there's a
    // need for a feature for incremental verification, we can package new_state
    // up in a py::capsule and return it to python, or stick it in the cache.
    if (lean_response) {
      lean_dec(lean_response);
    }
  }
  py::capsule return_capsule =
    py::capsule(&state_cache, "StateCache", [](PyObject *obj) {});

  return py::make_tuple(results, return_capsule);
}

} // namespace pyle
