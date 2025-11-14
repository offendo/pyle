#pragma once

#include <cstdint>
#include <lean/lean.h>

// Lean FFI initialization
extern "C" void lean_initialize();
extern "C" void lean_initialize_runtime_module();
extern "C" void lean_init_search_path(lean_obj_arg);
extern "C" void lean_init_task_manager();
extern "C" void lean_io_mark_end_initialization();
extern "C" void lean_initialize_thread();
extern "C" void lean_finalize_thread();

// exported Lean functions
extern "C" lean_object *run_search_path_init();
extern "C" lean_object *
initialize_Pyle_Frontend(uint8_t builtin, lean_object *);
extern "C" lean_object *lean_evaluate(lean_obj_arg, lean_obj_arg, uint32_t);
extern "C" lean_object *
lean_evaluate_batch(lean_obj_arg, lean_obj_arg, uint32_t);
