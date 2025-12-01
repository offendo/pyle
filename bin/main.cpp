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
  std::string input_file = "examples.json";
  std::string output_file = "benchmark.json";
  size_t sample_size = 25;
  size_t cache_size = 5;
  std::vector<std::string> examples;
  uint32_t timeout = 20000;
  uint32_t n_workers = 4;
  bool return_info_trees = false;

  // Loading input file
  std::ifstream fin(input_file);
  if (!fin.is_open()) {
    std::cerr << "Error: could not open " << input_file << std::endl;
    return 1;
  }
  Json::CharReaderBuilder rbuilder;
  rbuilder["collectComments"] = false;
  std::string errs;
  Json::Value root;
  Json::parseFromStream(rbuilder, fin, &root, &errs);
  std::shared_ptr<pyle::Cache> cache = pyle::make_cache(cache_size);

  for (auto &it : root) {
    examples.push_back(it["full_proof"].asString());
  }

  // Import header
  auto [header, body] = parse_header_and_body(examples[0]);
  lean_object *state = cache->get(header).get();
  auto start = high_resolution_clock::now();
  lean_object *result = pyle::evaluate_one(examples[0], state, 0);
  auto [response, header_env, new_state] = pyle::parse_lean_output(result);
  cache->put(header, header_env);
  long duration =
    duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
  std::cout << "Imported header in " << duration / 1000 << "s" << std::endl;

  {
    // Take a sample to process
    std::vector<std::string> sample(
      examples.begin(),
      examples.begin() + sample_size);

    // Process
    auto start = high_resolution_clock::now();
    std::vector<std::string> responses;
    std::vector<long> durations;

    if (n_workers > 1) {
      auto tuple = pyle::evaluate_many(
        sample,
        cache.get(),
        timeout,
        n_workers,
        return_info_trees);
      responses = std::get<0>(tuple);
      durations = std::get<1>(tuple);
    } else {
      int i = 0;
      for (const std::string &samp : sample) {
        lean_object *state = cache->get(header).get();
        auto [header, body] = parse_header_and_body(samp);
        lean_object *result =
          pyle::evaluate_one(state ? body : samp, state, timeout);
        auto [response, header_env, new_state] =
          pyle::parse_lean_output(result);
        responses.push_back(response);
        durations.push_back(0);
        std::cout << "finished " << i << std::endl;
        i++;
      }
    }
    long total =
      duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
    std::cout << "Total time: " << total << std::endl;

    // Write results to JSON file
    Json::Value outjson;
    for (int i = 0; i < (int)responses.size(); i++) {
      Json::Value val;
      Json::Value inp = root[i];
      val["problem_id"] = inp["problem_id"];
      val["theorem"] = inp["full_proof"];
      val["response"] = responses[i];
      val["duration"] = Json::Int(durations[i]);
      outjson.append(val);
    }
    Json::StyledWriter writer;
    std::string output = writer.write(outjson);
    std::ofstream fout(output_file);
    if (!fout.is_open()) {
      std::cerr << "error: could not open output file: " << output_file
                << std::endl;
      return 1;
    }
    fout << output << std::endl;
    fout.close();
    fout.clear();
  }
  return 0;
}
