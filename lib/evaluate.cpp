#include "pyle/evaluate.hpp"
#include "indicators/cursor_control.hpp"
#include "indicators/progress_bar.hpp"
#include "indicators/setting.hpp"
#include "lean/lean.h"
#include "pyle/cache.hpp"
#include "pyle/capsule.hpp"
#include "pyle/lean.hpp"
#include "pyle/tpool.hpp"
#include "pyle/utils.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <pyerrors.h>
#include <sstream>
#include <stdio.h>
#include <string>
#include <thread>

using namespace std::chrono;
using namespace indicators;
using result_t =
  std::tuple<std::string, std::string, std::string, std::string, long long>;

const int N_THREADS = 2;
std::mutex eval_mutex;

namespace py = pybind11;

/* Creates a progress bar with some default settings. */
std::unique_ptr<ProgressBar> make_progress_bar() {
  return std::unique_ptr<ProgressBar>(new ProgressBar{
    option::BarWidth{80},
    option::Start{"["},
    option::Fill{"■"},
    option::Lead{"■"},
    option::Remainder{"-"},
    option::End{" ]"},
    option::PrefixText{"Verifying:"},
    option::ForegroundColor{Color::cyan},
    option::ShowPercentage{true},
    option::ShowElapsedTime{true},
    option::ShowRemainingTime{true},
    option::FontStyles{std::vector<FontStyle>{FontStyle::bold}}});
}

namespace pyle {

lean_obj_res evaluate_one(
  const std::string &lean_code,
  lean_obj_arg state,
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

  // Return the lean output. Note that `lean_evaluate` here is not
  // pyle::evaluate()
  // - i.e., this is not a recursive call.
  auto out = lean_evaluate(boxed, opt_state, timeout);
  return out;
}

lean_obj_res evaluate_batch(
  const std::vector<std::string> &lean_code,
  lean_obj_arg state_cache,
  uint32_t timeout) {

  // Box the cstring into a lean_object. This object will be consumed by the
  // function
  lean_object *inputs = lean_mk_array_of_strings(lean_code);

  // Wrap the state in an Option type to pass to Lean
  lean_object *opt_state_cache;
  if (state_cache) {
    // opt_state = some state
    opt_state_cache = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(opt_state_cache, 0, state_cache);
  } else {
    // opt_state = none
    opt_state_cache = lean_alloc_ctor(0, 0, 0);
  }

  // Return the lean output. Note that `evaluate` here is not pyle::evaluate()
  // - i.e., this is not a recursive call.
  auto out = lean_evaluate_batch(inputs, opt_state_cache, timeout);
  return out;
}

std::tuple<std::string, lean_object *>
parse_lean_output(b_lean_obj_arg lean_response) {
  lean_object *product_obj = lean_ctor_get(lean_response, 0);

  // cache X response
  lean_object *cache = lean_ctor_get(product_obj, 0);
  lean_object *response = lean_ctor_get(product_obj, 1);
  std::string json_response = lean_string_cstr(response);
  return std::make_tuple(json_response, cache);
}

py::tuple py_evaluate(
  const std::string &lean_code,
  std::optional<py::capsule> capsule,
  uint32_t timeout) {

  // extract the state cache possibly
  lean_object *state_cache =
    capsule.has_value() ? unpack_lean_object(capsule.value()) : nullptr;
  if (state_cache) {
    lean_inc(state_cache);
  }

  // Run the actual evaluation, and grab the result out
  auto start = high_resolution_clock::now();
  lean_object *lean_response = evaluate_one(lean_code, state_cache, timeout);
  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start).count();

  auto [json_response, new_state_cache] = parse_lean_output(lean_response);

  // This step is a little subtlely weird. We call lean_inc to on the
  // new_state to increment the ref count. Then we call
  // lean_dec(lean_response) which decrements new_state's ref count, since
  // it's a child object of lean_response. This balances the change of
  // new_state's refs to 0 so we keep it in memory.
  if (new_state_cache) {
    lean_inc(new_state_cache);
  }
  py::capsule return_capsule = pack_lean_object(new_state_cache);
  if (lean_response) {
    lean_dec(lean_response);
  }
  return py::make_tuple(json_response, duration, return_capsule);
}

py::tuple py_evaluate_many(
  const std::vector<std::string> &lean_code,
  std::optional<py::capsule> capsule,
  uint32_t timeout) {

  // extract the state cache possibly
  lean_object *state_cache =
    capsule.has_value() ? unpack_lean_object(capsule.value()) : nullptr;

  // launch the pool
  ThreadPool pool(N_THREADS);

  // store the futures
  std::vector<std::future<std::tuple<std::string, long long>>> futures;

  // launch the jobs
  for (const std::string code : lean_code) {
    auto fut = pool.enqueue([&code, &state_cache, timeout]() {
      // Run the actual evaluation, and grab the result out
      // make sure evaluate_one doesn't consume the cache
      auto start = high_resolution_clock::now();
      if (state_cache) {
        lean_inc(state_cache);
      }
      lean_object *lean_response = evaluate_one(code, state_cache, timeout);
      auto stop = high_resolution_clock::now();
      auto duration = duration_cast<milliseconds>(stop - start).count();
      auto [json, new_cache] = parse_lean_output(lean_response);
      if (new_cache) {
        lean_inc(new_cache);
      }
      {
        std::lock_guard<std::mutex> lock(eval_mutex);
        state_cache = new_cache;
      }
      return std::make_tuple(json, duration);
    });
    futures.push_back(std::move(fut));
  }

  // This step is a little subtlely weird. We call lean_inc to on the
  // new_state to increment the ref count. Then we call
  // lean_dec(lean_response) which decrements new_state's ref count, since
  // it's a child object of lean_response. This balances the change of
  // new_state's refs to 0 so we keep it in memory.
  if (state_cache) {
    lean_inc(state_cache);
  }

  std::vector<std::string> responses(lean_code.size());
  std::vector<long long> durations(lean_code.size());
  for (int i = 0; i < futures.size(); ++i) {
    auto [response, duration] = futures[i].get();
    responses[i] = response;
    durations[i] = duration;
  }

  py::capsule return_capsule = pack_lean_object(state_cache);
  return py::make_tuple(responses, durations, return_capsule);
}

} // namespace pyle
