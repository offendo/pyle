#include "pyle/evaluate.hpp"
#include "pyle/initialize.hpp"
#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(pyle, m) {
  pyle::initialize();
  m.doc() = "Python interface for a Lean interpreter.";

  m.def(
    "evaluate",
    &pyle::py_evaluate,
    R"(Compiles input lean code. Times out after `timeout` ms.

Arguments
=========
lean_code : str
    String representation of lean code to process.
initial_state : lean_object | None = None
    Environment to run lean code in.
timeout : int | None = None
    Maximum processing time in ms.

Returns
=======
tuple[str, str, lean_object, str | None])",
    py::arg("lean_code"),
    py::arg("initial_state") = py::none(),
    py::arg("timeout") = 0);

  m.def(
    "evaluate_many",
    &pyle::py_evaluate_many,
    R"(Processes bulk)",
    py::arg("lean_code"),
    py::arg("state_cache") = py::none(),
    py::arg("timeout") = 0,
    py::arg("capacity") = 5,
    py::arg("n_threads") = 4);

  m.def(
    "evaluate_test",
    &pyle::py_evaluate_test_new,
    R"(Test)",
    py::arg("lean_code"),
    py::arg("state_cache") = py::none(),
    py::arg("timeout") = 0);
}
