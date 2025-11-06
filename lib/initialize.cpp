#include "pyle/initialize.hpp"
#include "pyle/lean.hpp"
#include <cstdlib>
#include <iostream>

namespace pyle {

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
  std::cout << "Initialized!" << std::endl;
}

} // namespace pyle
