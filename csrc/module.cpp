#include <pybind11/pybind11.h>
#include "pyle/initialize.hpp"
#include "pyle/evaluate.hpp"

namespace py = pybind11;

PYBIND11_MODULE(pyle, m) {
    pyle::initialize();
    m.doc() = "Python interface for a Lean interpreter.";

    m.def("evaluate", &pyle::evaluate_one,
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
}
