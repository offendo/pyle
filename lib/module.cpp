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
    R"(Evaluates input lean code. Times out after `timeout` ms.)",
    py::arg("lean_code"),
    py::arg("state_cache") = py::none(),
    py::arg("timeout") = 0);
}
