// Standalone stress test for patches/lean-completed-task.patch.
// Build with the selected Lean runtime; see OPTIMIZATION.md.
#include "pyle/lean.hpp"
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

static lean_object *make_value(lean_object *) {
  return lean_mk_string("completed task result");
}
static lean_object *identity(lean_object *value) { return value; }
static lean_object *make_task(lean_object *value) { return lean_task_pure(value); }
static lean_object *closure(void *fn) { return lean_alloc_closure(fn, 1, 0); }

int main() {
  lean_initialize();
  lean_init_task_manager_using(32);
  std::vector<std::thread> workers;
  for (unsigned w = 0; w < 32; ++w) {
    workers.emplace_back([] {
      lean_initialize_thread();
      for (unsigned i = 0; i < 3000; ++i) {
        auto task = lean_task_spawn_core(closure((void *)make_value), 0, false);
        // Exercise completed, pending, mapped and bound tasks, including
        // dropping the last external reference while completion is racing.
        if (i % 4 == 0) {
          task = lean_task_map_core(closure((void *)identity), task, 0, false, false);
        } else if (i % 4 == 1) {
          task = lean_task_bind_core(task, closure((void *)make_task), 0, false, false);
        }
        if (i % 3 == 0) {
          auto value = lean_task_get(task);
          if (lean_string_size(value) != sizeof("completed task result")) std::abort();
        } else if (i % 3 == 1) {
          lean_io_cancel_core(task);
        }
        lean_dec(task);
        auto pure = lean_task_pure(lean_mk_string("pure"));
        lean_dec(pure);
      }
      lean_finalize_thread();
    });
  }
  for (auto &worker : workers) worker.join();
  lean_finalize_task_manager();
  std::cout << "task stress passed\n";
}
