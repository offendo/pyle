#include "pyle/module.hpp"
#include "pyle/cache.hpp"
#include "pyle/evaluate.hpp"
#include "pyle/lean.hpp"
#include "pyle/utils.hpp"
#include <iostream>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

using namespace std::chrono;

namespace pyle {

pybind11::capsule pack_lean_object(lean_object *obj) {
  return pybind11::capsule(obj, "lean_object", [](PyObject *capsule) {
    cleanup_lean_object(PyCapsule_GetPointer(capsule, "lean_object"));
  });
}

lean_object *unpack_lean_object(const pybind11::capsule &capsule) {
  lean_object *obj = static_cast<lean_object *>(
    PyCapsule_GetPointer(capsule.ptr(), "lean_object"));
  return obj;
}

std::shared_ptr<Cache> from_dict(pybind11::dict dict, size_t size) {
  std::shared_ptr<Cache> cache = make_cache(size);
  for (auto &pair : dict) {
    auto key = pair.first.cast<std::string>();
    lean_object *env = static_cast<lean_object *>(
      PyCapsule_GetPointer(pair.second.ptr(), "lean_object"));
    cache->put(key, env);
  }
  return cache;
}

pybind11::dict to_dict(Cache *cache) {
  std::lock_guard<std::mutex> lock(cache->mutex);
  py::dict dict;
  for (auto &[header, env] : cache->cache) {
    py::capsule capsule = pack_lean_object(env.get());
    dict[py::cast(header)] = capsule;
  }
  return dict;
}

py::tuple py_evaluate_one(
  const std::string &lean_code,
  std::optional<py::dict> opt_cache,
  uint32_t timeout,
  uint32_t cache_size) {
  // Unwrap the cache
  std::shared_ptr<Cache> state_cache =
    opt_cache.has_value() ? from_dict(opt_cache.value(), cache_size)
                          : make_cache(cache_size);

  // Step 1. Get the environment
  auto [header, body] = parse_header_and_body(lean_code);
  std::shared_ptr<lean_object> env = state_cache->get(header);

  // Step 2. Run the lean code, and parse the output
  auto start = high_resolution_clock::now();
  // If we found the env, DON'T PASS IN THE HEADER
  // TODO make "with header" and "without header" computation separate functions
  lean_object *lean_response;
  if (env) {
    lean_response = evaluate_one(body, env.get(), timeout);
  } else {
    lean_response = evaluate_one(lean_code, env.get(), timeout);
  }
  long duration =
    duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
  auto [response, header_env, final_state] = parse_lean_output(lean_response);

  // Step 3. Run the cache update.
  lean_inc(header_env);
  state_cache->put(header, header_env);
  py::dict cache_dict = to_dict(state_cache.get());
  return py::make_tuple(response, duration, cache_dict);
}

py::tuple py_evaluate_many(
  const std::vector<std::string> &lean_code,
  std::optional<py::dict> opt_cache,
  uint32_t timeout,
  uint32_t n_workers,
  uint32_t cache_size) {

  // Unwrap the cache
  std::shared_ptr<Cache> state_cache =
    opt_cache.has_value() ? from_dict(opt_cache.value(), cache_size)
                          : make_cache(cache_size);
  std::vector<std::string> responses;
  std::vector<long> durations;
  Cache *new_cache_ptr;
  {
    py::gil_scoped_release release_gil;
    auto tuple =
      evaluate_many(lean_code, state_cache.get(), timeout, n_workers);
    responses = std::get<0>(tuple);
    durations = std::get<1>(tuple);
    new_cache_ptr = std::get<2>(tuple);
  }

  py::dict dict = to_dict(new_cache_ptr);
  return py::make_tuple(responses, durations, dict);
}

py::tuple py_parse_header_and_body(const std::string &s) {
  auto [header, body] = parse_header_and_body(s);
  return py::make_tuple(header, body);
}

void initialize() {
  lean_initialize();
  // lean_initialize_runtime_module();
  uint8_t builtin = 1;
  lean_init_search_path(lean_io_mk_world());
  lean_object *res = initialize_Pyle_Frontend(builtin, lean_io_mk_world());
  if (lean_io_result_is_ok(res)) {
    lean_dec_ref(res);
  } else {
    lean_io_result_show_error(res);
    lean_dec(res);
    std::cerr << "Error: could not initialize Lean!" << std::endl;
    std::exit(1);
  }
  lean_init_task_manager();
  lean_io_mark_end_initialization();
  run_search_path_init();
}

} // namespace pyle

PYBIND11_MODULE(pyle, m) {
  pyle::initialize();
  m.doc() = "Python interface for a Lean interpreter.";

  m.def(
    "evaluate",
    &pyle::py_evaluate_one,
    R"(Evaluates input lean code. Times out after `timeout` ms.)",
    py::arg("lean_code"),
    py::arg("state_cache") = py::none(),
    py::arg("timeout") = 0,
    py::arg("cache_size") = 5);

  m.def(
    "evaluate_many",
    &pyle::py_evaluate_many,
    R"(Evaluates input lean code. Times out after `timeout` ms.)",
    py::arg("lean_code"),
    py::arg("state_cache") = py::none(),
    py::arg("timeout") = 0,
    py::arg("n_workers") = 1,
    py::arg("cache_size") = 5);

  m.def(
    "parse_header_and_body",
    &pyle::py_parse_header_and_body,
    "Parse out header and body.",
    py::arg("lean_code"));
}
