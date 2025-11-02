#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <lean/lean.h>
#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // For std::optional
#include <stdlib.h>
#include <string>

namespace py = pybind11;

/* Lean FFI initialization stuff */
extern "C" void lean_initialize();
extern "C" void lean_initialize_runtime_module();
extern "C" void lean_init_search_path(lean_obj_arg);
extern "C" void lean_init_task_manager();
extern "C" void lean_initialize_runtime_module();
extern "C" void lean_io_mark_end_initialization();

/* Lean evaluation code. */
extern "C" lean_object *run_lean_initialization();
extern "C" lean_object *evaluate_from_state(lean_obj_arg, lean_obj_arg,
                                            lean_obj_arg);
extern "C" lean_object *initialize_Pyl_Frontend(uint8_t builtin, lean_object *);

/* Destructor for lean_object. */
void cleanup_lean_object(void *ptr) {
  lean_object *obj = static_cast<lean_object *>(ptr);
  if (obj != nullptr) {
    lean_dec(obj);
  }
}

/* Pack a lean_object pointer inside a py::capsule. */
py::capsule pack_lean_object(lean_object *obj) {
  lean_inc(obj);
  return py::capsule(obj, "lean_object", [](PyObject *capsule) {
    cleanup_lean_object(PyCapsule_GetPointer(capsule, "lean_object"));
  });
}

/* Unpack a py::capsule to get the contained lean_object pointer. */
lean_object *unpack_lean_object(const py::capsule capsule) {
  lean_object *obj = static_cast<lean_object *>(
      PyCapsule_GetPointer(capsule.ptr(), "lean_object"));
  return obj;
}

/* Initializes the Lean FFI/interpreter. Essentially voodoo as far as I'm concerned. */
void initialize(){
    lean_initialize();
    lean_initialize_runtime_module();
    // use same default as for Lean executables
    uint8_t builtin = 1;
    lean_init_search_path(lean_io_mk_world());
    lean_object *res = initialize_Pyl_Frontend(builtin, lean_io_mk_world());
    if (lean_io_result_is_ok(res)) {
      lean_dec_ref(res);
    } else {
      lean_io_result_show_error(res);
      lean_dec(res);
      std::cerr << "Error: could not initialize Lean!" << std::endl;
      exit(1);
    }
    lean_init_task_manager();
    lean_io_mark_end_initialization();
    std::cout << "Successfully initialized Lean!" << std::endl;
}

/* Interface to evaluate Lean 4 code.
 */
const py::tuple evaluate(const std::string &lean_code,
                         std::optional<py::capsule> env, uint32_t timeout = 0) {

  // Format the input
  lean_object *lean_input = lean_mk_string(lean_code.c_str());

  // Initialize the optional arguments
  lean_object *option_env;
  lean_object *option_timeout;

  // Fill in the optional env if provided, or init to none
  if (env.has_value()) {
    lean_object *env_obj = unpack_lean_object(env.value());
    option_env = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(option_env, 0, env_obj);
  } else {
    option_env = lean_alloc_ctor(0, 0, 0);
  }

  // Fill in the optional timouet if provided, or init to none
  if (timeout > 0) {
    option_timeout = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(option_timeout, 0, lean_box(timeout));
  } else {
    option_timeout = lean_alloc_ctor(0, 0, 0);
  }

  // Run the evaluation
  lean_object *result =
      evaluate_from_state(lean_input, option_env, option_timeout);

  // Extract out what we need from the result
  lean_object *obj = lean_ctor_get(result, 0);
  lean_object *new_env = lean_ctor_get(obj, 0);
  lean_object *msgs = lean_ctor_get(obj, 1);
  const char *msg_str = lean_string_cstr(msgs);

  // Return (messages, environment)
  return py::make_tuple(msg_str, pack_lean_object(new_env));
}

PYBIND11_MODULE(pyl, m) {
  initialize();
  m.def(
      "evaluate", &evaluate,
      ("Compiles input lean code. Times out after `timeout` seconds.\n\n"
       "Arguments\n"
       "=========\n"
       "lean_code : str\n    String representation of lean code to process.\n"
       "env : lean_object | None = None\n    Environment to run lean code in. "
       "Useful for keeping imports loaded in memory.\n"
       "timeout : int | None = None\n    Maximum processing time in seconds."),
      py::arg("lean_code"), py::arg("env") = py::none(),
      py::arg("timeout") = py::none());
}
