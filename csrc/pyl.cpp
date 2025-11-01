#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <lean/lean.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // For std::optional
#include <string>
#include <optional>
#include <stdlib.h>

namespace py = pybind11;

extern "C" void lean_initialize();
extern "C" void lean_initialize_runtime_module();
extern "C" void lean_init_search_path(lean_obj_arg);
extern "C" void lean_init_task_manager();
extern "C" void lean_initialize_runtime_module();
extern "C" void lean_io_mark_end_initialization();

extern "C" lean_object *do_init();
extern "C" lean_object *run_json_command(lean_obj_arg);
extern "C" lean_object *process_input(lean_obj_arg, lean_obj_arg, lean_obj_arg);
extern "C" lean_object *initialize_Pyl_Frontend(uint8_t builtin, lean_object *);

void cleanup_lean_object(void *ptr) {
  lean_object *obj = static_cast<lean_object *>(ptr);
  if (obj != nullptr) {
    lean_dec(obj);
  }
}

py::capsule make_capsule(lean_object *obj) {
  // Increase ref count before passing to Python
  lean_inc(obj);
  return py::capsule(obj, "lean_object", [](PyObject *capsule) {
    cleanup_lean_object(PyCapsule_GetPointer(capsule, "lean_object"));
  });
}

lean_object *unpack_capsule(const py::capsule capsule) {
  lean_object *obj = static_cast<lean_object *>(
      PyCapsule_GetPointer(capsule.ptr(), "lean_object"));
  return obj;
}

const py::tuple compile(const std::string &lean_code, std::optional<py::capsule> env, uint32_t timeout = 0) {

    // Format the input
    lean_object *lean_input = lean_mk_string(lean_code.c_str());

    // If the env is provided, unpack it and process. Otherwise, just process from blank slate
    lean_object *none = lean_alloc_ctor(0, 0, 0);
    lean_object *env_obj = env.has_value() ? unpack_capsule(env.value()) : none;

    // If the timeout is <= 0, input the timeout as NULL
    lean_object* some_timeout = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(some_timeout, 0, lean_box(timeout));
    lean_object *timeout_obj = timeout > 0 ? some_timeout : none;

    lean_object *result = process_input(lean_input, env_obj, timeout_obj);

    // Extract out what we need from the result
    lean_object *obj = lean_ctor_get(result, 0);
    lean_object *new_env = lean_ctor_get(obj, 0);
    lean_object *msgs = lean_ctor_get(obj, 1);
    const char *msg_str = lean_string_cstr(msgs);

    // Return (messages, environment)
    return py::make_tuple(msg_str, make_capsule(new_env));
}


PYBIND11_MODULE(pyl, m) {
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

  // I don't really know what this does. I think it makes things visible to lean
  do_init();

  m.def(
      "compile",
      &compile,
      (
        "Compiles input lean code. Times out after `timeout` seconds.\n\n"
        "Arguments\n"
        "=========\n"
        "lean_code : str\n    String representation of lean code to process.\n"
        "env : lean_object | None = None\n    Environment to run lean code in. Useful for keeping imports loaded in memory.\n"
        "timeout : int | None = None\n    Maximum processing time in seconds."
      ),
      py::arg("lean_code"),
      py::arg("env") = py::none(),
      py::arg("timeout") = py::none()
  );
}
