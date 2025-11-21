#include "pyle/cache.hpp"
#include "pyle/evaluate.hpp"
#include "pyle/lean.hpp"
#include "pyle/utils.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <json/reader.h>
#include <json/writer.h>
#include <memory>
#include <string>

using namespace std::chrono;

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

int main(int argc, char *argv[]) {
  // initialize lean
  initialize();

  // Processing parameters
  std::string file_name = "examples.json";
  std::vector<std::string> examples;
  uint32_t timeout = 20000;
  uint32_t n_workers = 4;

  // Loading input file
  std::ifstream fin(file_name);
  if (!fin.is_open()) {
    std::cerr << "Error: could not open " << file_name << std::endl;
  }
  Json::CharReaderBuilder rbuilder;
  rbuilder["collectComments"] = false;
  std::string errs;
  Json::Value root;
  Json::parseFromStream(rbuilder, fin, &root, &errs);
  std::shared_ptr<pyle::Cache> cache = pyle::make_cache(5);

  for (auto &it : root) {
    examples.push_back(it["full_proof"].asString());
  }

  // Import header
  auto [header, body] = parse_header_and_body(examples[0]);
  lean_object *state = cache->get(header).get();
  auto start = high_resolution_clock::now();
  lean_object *result = pyle::evaluate_one(examples[0], state, timeout);
  auto [response, header_env, new_state] = pyle::parse_lean_output(result);
  cache->put(header, header_env);
  long duration =
    duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
  std::cout << "Imported header in " << ((float)duration) / 1000 << std::endl;

  // Run the multithreading
  {
    std::vector<std::string> sample(examples.begin(), examples.begin() + 100);

    auto start = high_resolution_clock::now();
    auto [responses, durations, new_cache] =
      pyle::evaluate_many(sample, cache.get(), timeout, n_workers);
    long total =
      duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
    for (const auto &d : durations) {
      std::cout << d << ", ";
    }
    std::cout << std::endl;
    std::cout << "Total time: " << total << std::endl;
  }
  return 0;
}
