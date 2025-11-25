#include "indicators/cursor_control.hpp"
#include "indicators/progress_bar.hpp"
#include "indicators/setting.hpp"
#include "lean/lean.h"
#include "pyle/cache.hpp"
#include "pyle/lean.hpp"
#include "pyle/tpool.hpp"
#include "pyle/utils.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdio.h>
#include <string>
#include <sys/types.h>

using namespace std::chrono;
using namespace indicators;
using result_t =
  std::tuple<std::string, std::string, std::string, std::string, long>;

std::mutex cache_mutex;

/* Creates a progress bar with some default settings. */
std::unique_ptr<ProgressBar> make_progress_bar() {
  return std::unique_ptr<ProgressBar>(new ProgressBar{
    option::BarWidth{100},
    option::Start{"["},
    option::Fill{"="},
    option::Lead{"="},
    option::Remainder{" "},
    option::End{" ]"},
    option::PrefixText{"Verifying:"},
    option::ForegroundColor{Color::white},
    option::ShowPercentage{true},
    option::ShowElapsedTime{true},
    option::ShowRemainingTime{true},
    option::FontStyles{std::vector<FontStyle>{FontStyle::bold}}});
}

namespace pyle {

lean_obj_res evaluate_one(
  const std::string &lean_code,
  lean_obj_arg state,
  uint32_t timeout,
  bool return_info_trees) {

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
  auto out = lean_evaluate(boxed, opt_state, timeout, return_info_trees);
  return out;
}

std::tuple<std::string, lean_object *, lean_object *>
parse_lean_output(b_lean_obj_arg lean_response) {
  lean_object *product_obj = lean_ctor_get(lean_response, 0);

  // response X header env X final state
  lean_object *response = lean_ctor_get(product_obj, 0);
  lean_object *header_env_and_final_state = lean_ctor_get(product_obj, 1);
  lean_object *header_env = lean_ctor_get(header_env_and_final_state, 0);
  lean_object *final_state = lean_ctor_get(header_env_and_final_state, 1);
  std::string json_response = lean_string_cstr(response);
  return std::make_tuple(json_response, header_env, final_state);
}

std::tuple<std::vector<std::string>, std::vector<long>, Cache *> evaluate_many(
  const std::vector<std::string> &lean_code,
  Cache *state_cache,
  uint32_t timeout,
  uint32_t n_workers,
  bool return_info_trees) {

  // Output vectors
  std::vector<std::future<std::tuple<std::string, long>>> futures;
  std::vector<std::string> responses(lean_code.size());
  std::vector<long> durations(lean_code.size());

  // launch the pool
  ThreadPool pool(n_workers);
  // std::cout << "Launched pool with " << n_workers << " workers" << std::endl;

  // launch the jobs
  for (size_t i = 0; i < lean_code.size(); ++i) {
    const std::string &code = lean_code[i];
    auto fut = pool.enqueue([&]() {
      // Step 1. Get the environment
      auto [header, body] = parse_header_and_body(code);
      std::shared_ptr<lean_object> env = state_cache->get(header);

      // Step 2. Run the lean code, and parse the output
      auto start = steady_clock::now();
      // If we found the env, DON'T PASS IN THE HEADER
      // TODO make "with header" and "without header" separate functions
      lean_object *lean_response =
        evaluate_one(env ? body : code, env.get(), timeout, return_info_trees);
      long duration =
        duration_cast<milliseconds>(steady_clock::now() - start).count();
      auto [json, header_env, final_state] = parse_lean_output(lean_response);
      /*
      std::cout << "(" << std::this_thread::get_id() << ") total: " << duration
                << std::endl;
                */
      // Step 3. Run the cache update.
      lean_inc(header_env);
      state_cache->put(header, header_env);
      return std::make_tuple(json, duration);
    });
    futures.push_back(std::move(fut));
  }

  // extract out the futures results
  std::unique_ptr<ProgressBar> pbar = make_progress_bar();
  for (size_t i = 0; i < futures.size(); ++i) {
    pbar->set_progress(100 * i / responses.size());
    futures[i].wait();
    auto [response, duration] = futures[i].get();
    responses[i] = response;
    durations[i] = duration;
  }
  pbar->mark_as_completed();
  pool.shutdown();

  return std::make_tuple(responses, durations, state_cache);
}

} // namespace pyle
