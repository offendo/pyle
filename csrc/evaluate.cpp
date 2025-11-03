#include "pyle/evaluate.hpp"
#include "pyle/capsule.hpp"
#include "pyle/lean.hpp"
#include <algorithm>

namespace py = pybind11;

namespace pyle {

py::tuple evaluate_one(
    const std::string &lean_code,
    std::optional<py::capsule> initial_state,
    uint32_t timeout)
{
    lean_object *boxed = lean_mk_string(lean_code.c_str());
    lean_object *res = nullptr;

    if (!initial_state && timeout == 0) {
        res = evaluate(boxed);
    } else if (initial_state && timeout == 0) {
        lean_object *env = unpack_lean_object(*initial_state);
        res = evaluate_from_state(boxed, env);
    } else if (!initial_state && timeout > 0) {
        res = evaluate_with_timeout(boxed, timeout);
    } else {
        lean_object *env = unpack_lean_object(*initial_state);
        res = evaluate_from_state_with_timeout(boxed, env, timeout);
    }

    // unpack the result tuple: ( (env, msgs, trees, error?), _ )
    lean_object *obj       = lean_ctor_get(res, 0);
    lean_object *new_env   = lean_ctor_get(obj, 0);
    lean_object *msgs      = lean_ctor_get(obj, 1);
    lean_object *trees     = lean_ctor_get(obj, 2);
    lean_object *opt_error = lean_ctor_get(obj, 3);

    const char *msg_str   = lean_string_cstr(msgs);
    const char *tree_str  = lean_string_cstr(trees);
    const char *err_str   = (lean_obj_tag(opt_error) == 1
                              ? lean_string_cstr(lean_ctor_get(opt_error, 0))
                              : nullptr);

    return py::make_tuple(msg_str, tree_str, pack_lean_object(new_env), err_str);
}

std::vector<const char *> evaluate_many(
    const std::vector<std::string> &lean_code,
    py::capsule *initial_state,
    uint32_t timeout)
{
    std::vector<lean_object *> inputs(lean_code.size());
    std::transform(lean_code.begin(), lean_code.end(), inputs.begin(),
        [](auto &s){ return lean_mk_string(s.c_str()); });

    std::vector<const char *> out(lean_code.size());
    lean_object *env = unpack_lean_object(*initial_state);
    std::transform(inputs.begin(), inputs.end(), out.begin(),
        [env, timeout](lean_object *inp) {
            lean_object *r = evaluate_from_state_with_timeout(inp, env, timeout);
            lean_object *obj = lean_ctor_get(r, 0);
            lean_object *msgs = lean_ctor_get(obj, 1);
            return lean_string_cstr(msgs);
        });

    return out;
}

} // namespace pyle
