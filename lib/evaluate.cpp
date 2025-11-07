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
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <pyerrors.h>
#include <sstream>
#include <stdio.h>
#include <string>

using namespace std::chrono;
using namespace indicators;
using result_t =
  std::tuple<std::string, std::string, std::string, std::string, long long>;

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

  // Return the lean output. Note that `evaluate` here is not pyle::evaluate()
  // - i.e., this is not a recursive call.
  auto out = lean_evaluate(boxed, opt_state, timeout);
  return out;
}

std::tuple<std::string, std::string, std::string, std::string, lean_object *>
parse_lean_output(b_lean_obj_arg lean_response) {
  // note that we return an Except object, so we have to dig one value in to
  // grab the internals before we can parse the output.
  lean_object *except_obj = lean_ctor_get(lean_response, 0);

  // If we have an error type, pull out the error and exit early.
  // This is mostly going to happen if Lean times out.
  if (lean_obj_tag(except_obj) == 0) {
    std::string err = lean_string_cstr(lean_ctor_get(except_obj, 0));
    return std::make_tuple("", "", err, "", nullptr);
  }

  // Otherwise, we got a real result back and we can parse the output.
  lean_object *result = lean_ctor_get(except_obj, 0);
  lean_object *new_state = lean_ctor_get(result, 0);
  lean_object *msgs = lean_ctor_get(result, 1);
  lean_object *trees = lean_ctor_get(result, 2);
  lean_object *tactics = lean_ctor_get(result, 3);

  std::string msg_str = lean_string_cstr(msgs);
  std::string tree_str = lean_string_cstr(trees);
  std::string tac_str = lean_string_cstr(tactics);
  return std::make_tuple(msg_str, tree_str, "", tac_str, new_state);
}

py::tuple py_evaluate(
  const std::string &lean_code,
  std::optional<py::capsule> capsule,
  uint32_t timeout) {

  // extract the given state, if any
  // Also, increment the ref counter because it'll be consumed by the
  // evaluate_one function, and we want to make sure python keep's access to
  // it.
  lean_object *state =
    capsule.has_value() ? unpack_lean_object(capsule.value()) : nullptr;
  if (state) {
    lean_inc(state);
  }

  // Run the actual evaluation, and grab the result out
  auto start = high_resolution_clock::now();
  lean_object *lean_response = evaluate_one(lean_code, state, timeout);
  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start).count();

  auto [msgs, tree, err, tacs, new_state] = parse_lean_output(lean_response);

  // This step is a little subtlely weird. We call lean_inc to on the
  // new_state to increment the ref count. Then we call
  // lean_dec(lean_response) which decrements new_state's ref count, since
  // it's a child object of lean_response. This balances the change of
  // new_state's refs to 0 so we keep it in memory.
  if (new_state) {
    lean_inc(new_state);
  }
  py::capsule return_capsule = pack_lean_object(new_state);
  if (lean_response) {
    lean_dec(lean_response);
  }
  return py::make_tuple(msgs, tree, err, tacs, duration, return_capsule);
}

py::tuple py_evaluate_many(
  std::vector<std::string> &lean_code,
  std::optional<py::capsule> opt_cache,
  uint32_t timeout,
  uint32_t cache_capacity) {

  std::unique_ptr<Cache> state_cache = opt_cache.has_value()
                                         ? unpack_cache(opt_cache.value())
                                         : make_cache(cache_capacity);

  // vector to collect the actual results
  std::vector<result_t> results(lean_code.size());

  ThreadPool pool(std::thread::hardware_concurrency());
  std::stringstream ss;

  std::vector<int> task_ids;
  for (std::string &thm : lean_code) {
    auto lambda_fn = [&state_cache, &timeout](const std::string &thm) {
      lean_initialize_thread();
      auto [header, body] = parse_header_and_body(thm);
      std::shared_ptr<lean_object> state = state_cache->get(header);
      if (!state) {
        // Evaluate the header
        lean_object *header_response = evaluate_one(header, nullptr, 0);

        // Parse the output.
        auto [m, t, err, ts, header_state] = parse_lean_output(header_response);

        // If we errored on this header, we need to count this thm as failure
        // Make sure to 'continue' to skip the rest of the loop.
        if (!err.empty()) {
          return result_t{"", "", err, "", 0};
        }

        // Otherwise, store the (header,state) in the cache!
        // First off, make sure we increment the new_state so we don't lose it
        // when we decrement header_response. Then we can safely add it and
        // grab the shared_ptr on return.
        lean_inc(header_state);
        state = state_cache->put(header, header_state);
        if (header_response) {
          lean_dec(header_response);
        }
      }

      // Now we definitely have the header state in the variable `state`.
      // **IMPORTANT** evaluate_one will consume the state reference, so we
      // need to preemptively increment it if we want to keep it around.
      lean_inc(state.get());

      // Now run the actual evaluation. Also measure the time it takes
      auto start = high_resolution_clock::now();
      lean_object *lean_response = evaluate_one(body, state.get(), timeout);
      auto stop = high_resolution_clock::now();
      auto duration = duration_cast<milliseconds>(stop - start).count();

      // Parse the output.
      auto [msgs, tree, err, tactics, thm_state] =
        parse_lean_output(lean_response);

      // **IMPORTANT**: This lean_dec will delete thm_state! Which is good
      // because we don't want it - it'll just eat up memory. In the future,
      // if there's a need for a feature for incremental verification, we can
      // package thm_state up in a py::capsule and return it to python, or
      // stick it in the cache, or something. Whatever we do, make sure we
      // lean_inc it.
      if (lean_response) {
        lean_dec(lean_response);
      }
      lean_finalize_thread();
      return result_t{msgs, tree, err, tactics, duration};
    };
    int task_id = pool.enqueue(lambda_fn, thm);
    task_ids.push_back(task_id);
  }

  // Now that we've got all the tasks enqueued, let's start waiting
  // TODO add progress bar
  std::unique_ptr<ProgressBar> pbar = make_progress_bar();
  show_console_cursor(false);
  for (int i = 1; i < 1 + task_ids.size(); i++) {
    // make sure ctrl-c works, at least partially. Not perfect solution but
    // it'll kill the main process. The threads will keep going so we need to
    // shut them down. IDK if pool.shutdown() will take care of it because the
    // entire ThreadPool code was AI'd.
    if (PyErr_CheckSignals() != 0) {
      pool.shutdown();
      show_console_cursor(true);
      throw py::error_already_set();
    }

    // Wait for task to be completed
    std::optional<ThreadPool::CompletedTask> res = pool.wait_pop_completed();
    if (!res.has_value()) {
      continue;
    }
    ThreadPool::CompletedTask val = res.value();
    result_t &result = val.value<result_t>();
    results[val.id] = result;
    pbar->set_progress(100 * i / results.size());

    // Format a postfix string
    ss << "(" << i << "/" << results.size() << ")";
    pbar->set_option(option::PostfixText{ss.str()});
    // empty out the sstream
    ss.str("");
    ss.clear();
  }
  show_console_cursor(true);

  // Finally, we need to release the unique_ptr on the state_cache and
  // transfer ownership to python.
  Cache *cache = state_cache.release();

  // Also kill the threads.
  // TODO maybe we need to keep these alive for successive calls?
  pool.shutdown();

  py::capsule return_capsule = pack_cache(cache);

  return py::make_tuple(results, return_capsule);
}

} // namespace pyle
