#include "pyle/evaluate.hpp"
#include "pyle/initialize.hpp"
#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(pyle, m) {
  pyle::initialize();
  m.doc() = "Python interface for a Lean interpreter.";

  m.def(
    "evaluate_many",
    &pyle::py_evaluate_many,
    R"(Evaluates input lean code. Times out after `timeout` ms.)",
    py::arg("lean_code"),
    py::arg("state_cache") = py::none(),
    py::arg("timeout") = 0,
    py::arg("n_workers") = 1,
    py::arg("cache_capacity") = 5);
}
