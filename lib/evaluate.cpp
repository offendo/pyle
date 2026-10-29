#include "indicators/cursor_control.hpp"
#include "indicators/progress_bar.hpp"
#include "indicators/setting.hpp"
#include "lean/lean.h"
#include "pyle/cache.hpp"
#include "pyle/evaluate.hpp"
#include "pyle/lean.hpp"
#include "pyle/tpool.hpp"
#include "pyle/utils.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <shared_mutex>
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

// Module initializers mutate Lean's global extension registry. Cold imports
// need exclusive access; evaluations of cached environments remain parallel.
static std::shared_mutex evaluation_mutex;

lean_obj_res evaluate_one(
  const std::string &lean_code,
  b_lean_obj_arg state,
  uint32_t timeout,
  bool return_info_trees,
  bool persist,
  bool header_only) {

  std::unique_lock<std::shared_mutex> import_lock(evaluation_mutex, std::defer_lock);
  std::shared_lock<std::shared_mutex> evaluation_lock(evaluation_mutex, std::defer_lock);
  if (state) {
    evaluation_lock.lock();
  } else {
    import_lock.lock();
    // withImporting resets this flag after every import, not just at startup.
    lean_dec(lean_enable_initializer_execution());
  }

  // Box the cstring into a lean_object. This object will be consumed by the
  // function
  lean_object *boxed = lean_mk_string(lean_code.c_str());

  // Wrap the state in an Option type to pass to Lean
  lean_object *opt_state;
  if (state) {
    lean_inc(state); // The Option and lean_evaluate consume their reference.
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
  // Let the importer freeze its environment before publishing any references
  // to initializers or elaboration tasks, rather than marking a live result.
  return lean_evaluate(boxed, opt_state, timeout, return_info_trees, persist, header_only);
}

std::tuple<std::string, lean_object *, lean_object *>
parse_lean_output(b_lean_obj_arg lean_response) {
  if (lean_io_result_is_error(lean_response)) {
    lean_io_result_show_error(lean_response);
    throw std::runtime_error("Lean evaluation failed with an IO error");
  }
  lean_object *product_obj = lean_ctor_get(lean_response, 0);

  // response X header env X final state
  lean_object *response = lean_ctor_get(product_obj, 0);
  lean_object *header_env_and_final_state = lean_ctor_get(product_obj, 1);
  lean_object *header_env_option = lean_ctor_get(header_env_and_final_state, 0);
  lean_object *header_env = lean_obj_tag(header_env_option) == 1
    ? lean_ctor_get(header_env_option, 0) : nullptr;
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

  if (n_workers == 0)
    throw std::invalid_argument("n_workers must be positive");

  // Output vectors
  std::vector<std::future<std::tuple<std::string, long>>> futures;
  std::vector<std::string> responses(lean_code.size());
  std::vector<long> durations(lean_code.size());

  // Import once per distinct header before launching workers. Otherwise cold
  // workers can all miss the cache and each construct another Mathlib env.
  // Keep refs independently of the bounded LRU for batches with many headers.
  std::unordered_map<std::string, std::shared_ptr<lean_object>> imported;
  std::vector<std::shared_ptr<lean_object>> environments;
  environments.reserve(lean_code.size());
  for (const auto &code : lean_code) {
    auto [header, body] = parse_header_and_body(code);
    auto found = imported.find(header);
    if (found == imported.end()) {
      auto env = state_cache->get(header);
      if (!env) {
        std::unique_ptr<lean_object, decltype(&lean_dec)> result(
          // Parse the original input in Lean, without executing any body. Do
          // not treat the C++ cache-key substring as executable Lean source.
          evaluate_one(code, nullptr, 0, false, false, true), &lean_dec);
        auto [json, header_env, final_state] = parse_lean_output(result.get());
        if (header_env) env = state_cache->put(header, header_env);
      }
      found = imported.emplace(header, std::move(env)).first;
    }
    environments.push_back(found->second);
  }

  // launch the pool
  ThreadPool pool(n_workers);
  // std::cout << "Launched pool with " << n_workers << " workers" << std::endl;

  // launch the jobs
  for (size_t i = 0; i < lean_code.size(); ++i) {
    auto fut = pool.enqueue([&, i]() {
      const auto &code = lean_code[i];
      // Step 1. Get the environment
      auto [header, body] = parse_header_and_body(code);
      std::shared_ptr<lean_object> env = environments[i];

      // Step 2. Run the lean code, and parse the output
      auto start = steady_clock::now();
      // If we found the env, DON'T PASS IN THE HEADER
      // TODO make "with header" and "without header" separate functions
      std::unique_ptr<lean_object, decltype(&lean_dec)> lean_response(
        evaluate_one(code, env.get(), timeout, return_info_trees, false), &lean_dec);
      long duration =
        duration_cast<milliseconds>(steady_clock::now() - start).count();
      auto [json, header_env, final_state] = parse_lean_output(lean_response.get());
      /*
      std::cout << "(" << std::this_thread::get_id() << ") total: " << duration
                << std::endl;
                */
      // Step 3. Run the cache update.
      if (header_env) state_cache->put(header, header_env);
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
