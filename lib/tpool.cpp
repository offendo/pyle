/* Example usage:
 *
 *   pyle::ThreadPool pool(10);
 *
 *   // Schedule work; lambdas can capture references to shared state such as
 *   // a cache guarded by its own mutex.
 *   auto job_id = pool.enqueue([&state_cache](std::string key) {
 *     return state_cache.lookup(key); // returns any value type
 *   }, "header_name");
 *
 *   // Harvest completions from another thread (or the same one).
 *   while (auto completed = pool.wait_pop_completed()) {
 *     if (completed->id == job_id) {
 *       completed->rethrow_if_error();
 *       auto &value = completed->value<StateType>();
 *       use(value);
 *       break;
 *     }
 *   }
 *
 *   pool.shutdown(); // optional: destructor calls this automatically
 */
#include "pyle/tpool.hpp"
#include <any>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace pyle {} // namespace pyle
