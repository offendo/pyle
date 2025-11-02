#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <lean/lean.h>
#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // For std::optional
#include <stdlib.h>
#include <string>
#include <vector>

namespace py = pybind11;

/* Lean FFI initialization stuff */
extern "C" void lean_initialize();
extern "C" void lean_initialize_runtime_module();
extern "C" void lean_init_search_path(lean_obj_arg);
extern "C" void lean_init_task_manager();
extern "C" void lean_initialize_runtime_module();
extern "C" void lean_io_mark_end_initialization();

/* Exported Lean functions. */
extern "C" lean_object *run_lean_initialization();
extern "C" lean_object *evaluate(lean_obj_arg);
extern "C" lean_object *evaluate_from_state(lean_obj_arg, lean_obj_arg);
extern "C" lean_object *evaluate_with_timeout(lean_obj_arg, uint32_t);
extern "C" lean_object *
    evaluate_from_state_with_timeout(lean_obj_arg, lean_obj_arg, uint32_t);
extern "C" lean_object *initialize_Pyle_Frontend(uint8_t builtin,
                                                 lean_object *);

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
lean_object *unpack_lean_object(const py::capsule &capsule) {
  lean_object *obj = static_cast<lean_object *>(
      PyCapsule_GetPointer(capsule.ptr(), "lean_object"));
  return obj;
}

/* Initializes the Lean FFI/interpreter. Essentially voodoo as far as I'm
 * concerned. */
void initialize() {
  lean_initialize();
  lean_initialize_runtime_module();
  // use same default as for Lean executables
  uint8_t builtin = 1;
  lean_init_search_path(lean_io_mk_world());
  lean_object *res = initialize_Pyle_Frontend(builtin, lean_io_mk_world());
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
}

/* Interface to evaluate Lean 4 code.
 */
const py::tuple
evaluate_one(const std::string &lean_code,
             std::optional<py::capsule> initial_state_capsule = std::nullopt,
             uint32_t timeout = 0) {
  // Format the input
  lean_object *boxed_lean_code = lean_mk_string(lean_code.c_str());

  lean_object *result;
  /* Four cases:
   * =========== */
  // 1. No state, no timeout
  if (!initial_state_capsule.has_value() && timeout == 0) {
    result = evaluate(boxed_lean_code);
  }
  // 2. Given state, no timeout
  else if (initial_state_capsule.has_value() && timeout == 0) {
    lean_object *env = unpack_lean_object(*initial_state_capsule);
    result = evaluate_from_state(boxed_lean_code, env);
  }
  // 3. No state, given timeout
  else if (!initial_state_capsule.has_value() && timeout > 0) {
    result = evaluate_with_timeout(boxed_lean_code, timeout);
  }
  // 4. Given state, given timeout
  else {
    lean_object *env = unpack_lean_object(*initial_state_capsule);
    result =
        evaluate_from_state_with_timeout(boxed_lean_code, env, timeout);
  }

  // Unpack the output tuple
  lean_object *obj = lean_ctor_get(result, 0);
  lean_object *new_env = lean_ctor_get(obj, 0);
  lean_object *msgs = lean_ctor_get(obj, 1);
  lean_object *trees = lean_ctor_get(obj, 2);
  lean_object *opt_error = lean_ctor_get(obj, 3);

  // Format results
  const char *msg_str = lean_string_cstr(msgs);
  const char *tree_str = lean_string_cstr(trees);
  const char* error_str = lean_obj_tag(opt_error) == 1 ? lean_string_cstr(lean_ctor_get(opt_error, 0)) : NULL;

  // Return (messages, environment)
  return py::make_tuple(msg_str, tree_str, pack_lean_object(new_env), error_str);
}

/* Interface to evaluate Lean 4 code.
 */
const std::vector<const char *>
evaluate_many(const std::vector<std::string> &lean_code,
              py::capsule *initial_state, uint32_t timeout = 0) {

  // Map the formatting function over the input strings
  std::vector<lean_object *> lean_input{lean_code.size()};
  std::transform(lean_code.begin(), lean_code.end(), lean_input.begin(),
                 [](const std::string &lean_code) {
                   return lean_mk_string(lean_code.c_str());
                 });

  // Run the evaluation by mapping the eval function over the input
  std::vector<const char *> result{lean_code.size()};
  lean_object *env = unpack_lean_object(*initial_state);
  std::transform(lean_input.begin(), lean_input.end(), result.begin(),
                 [env, initial_state, timeout](lean_object *input) {
                   lean_object *res = evaluate_from_state_with_timeout(
                       input, env, timeout);
                   // Extract out what we need from the result
                   lean_object *obj = lean_ctor_get(res, 0);
                   lean_object *msgs = lean_ctor_get(obj, 1);
                   const char *msg_str = lean_string_cstr(msgs);
                   return msg_str;
                 });

  // Return (messages, environment)
  return result;
}

PYBIND11_MODULE(pyle, m) {
  initialize();
  m.def(
      "evaluate", &evaluate_one,
      ("Compiles input lean code. Times out after `timeout` ms.\n\n"
       "Arguments\n"
       "=========\n"
       "lean_code : str\n    String representation of lean code to process.\n"
       "initial_state : lean_object | None = None\n    Environment to run lean "
       "code in. "
       "Useful for keeping imports loaded in memory.\n"
       "timeout : int | None = None\n    Maximum processing time in ms.\n\n"
       "Returns\n"
       "=======\n"
       "tuple[str, str, lean_object, str | None] :\n"
       "    Tuple of (messages, info trees, new state, errors)"
      ),
      py::arg("lean_code"), py::arg("initial_state") = py::none(),
      py::arg("timeout") = 0);
}
