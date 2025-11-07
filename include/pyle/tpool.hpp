// ThreadPool implementation used by pyle to fan out work across a fixed set of
// worker threads. Each worker blocks on a shared task queue, executes jobs, and
// records their results (or exceptions) so clients can harvest completions.
#pragma once

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
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// The pool owns a background worker set and two queues: one inbound task queue
// and one outbound completion queue. A client enqueues callables and later
// harvests CompletedTask objects to inspect results or surface exceptions.

namespace pyle {

class ThreadPool {
public:
  using TaskId = std::size_t;

  // Captures the outcome of a finished task. For non-void results, `result`
  // holds the payload via std::any. If the task threw, `error` is populated and
  // the payload remains empty.
  struct CompletedTask {
    TaskId id;
    std::any result;
    std::exception_ptr error;
    bool has_value{false};

    bool succeeded() const { return !error; }
    bool is_void() const { return !has_value && !error; }

    template <class T> T &value() {
      rethrow_if_error();
      if (!has_value) {
        throw std::bad_any_cast();
      }
      auto ptr = std::any_cast<T>(&result);
      if (!ptr) {
        throw std::bad_any_cast();
      }
      return *ptr;
    }

    template <class T> T &&move_value() {
      rethrow_if_error();
      if (!has_value) {
        throw std::bad_any_cast();
      }
      auto ptr = std::any_cast<T>(&result);
      if (!ptr) {
        throw std::bad_any_cast();
      }
      return std::move(*ptr);
    }

    void rethrow_if_error() const {
      if (error) {
        std::rethrow_exception(error);
      }
    }
  };

  explicit ThreadPool(std::size_t thread_count);
  ~ThreadPool();

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;

  template <class F, class... Args> TaskId enqueue(F &&f, Args &&...args);

  // Non-blocking attempt to harvest a finished task. Returns true on success.
  bool try_pop_completed(CompletedTask &out);

  // Waits until either a completed task is available or the pool begins its
  // shutdown sequence. Returns std::nullopt if the pool is draining.
  std::optional<CompletedTask> wait_pop_completed();
  template <class Rep, class Period>
  std::optional<CompletedTask>
  wait_pop_completed_for(const std::chrono::duration<Rep, Period> &timeout);

  std::size_t pending_tasks() const;
  void shutdown();
  bool is_shutdown() const;

private:
  // The worker stores task return values in TaskPayload before forwarding them
  // to the completion queue. We need this indirection so we can track whether
  // the callable returned void.
  struct TaskPayload {
    bool has_value{false};
    std::any value;
  };

  struct Task {
    TaskId id;
    std::function<TaskPayload()> callable;
  };

  void worker_loop();

  mutable std::mutex task_mutex;
  std::condition_variable task_cv;
  std::queue<Task> tasks;

  mutable std::mutex result_mutex;
  std::condition_variable result_cv;
  std::queue<CompletedTask> completed;

  std::vector<std::thread> workers;
  TaskId next_id{0};
  bool stopping{false};
};

inline ThreadPool::ThreadPool(std::size_t thread_count) {
  workers.reserve(thread_count);
  for (std::size_t i = 0; i < thread_count; ++i) {
    workers.emplace_back([this] { worker_loop(); });
  }
}

inline ThreadPool::~ThreadPool() {
  shutdown();
  for (std::thread &worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

inline void ThreadPool::shutdown() {
  {
    std::lock_guard<std::mutex> lock(task_mutex);
    if (stopping) {
      return;
    }
    stopping = true;
  }
  task_cv.notify_all();
  result_cv.notify_all();
}

inline bool ThreadPool::is_shutdown() const {
  std::lock_guard<std::mutex> lock(task_mutex);
  return stopping;
}

inline void ThreadPool::worker_loop() {
  for (;;) {
    Task task;
    {
      std::unique_lock<std::mutex> lock(task_mutex);
      task_cv.wait(lock, [this] { return stopping || !tasks.empty(); });
      if (stopping && tasks.empty()) {
        return;
      }
      task = std::move(tasks.front());
      tasks.pop();
    }

    CompletedTask completed_task;
    completed_task.id = task.id;

    try {
      TaskPayload payload = task.callable();
      completed_task.has_value = payload.has_value;
      completed_task.result = std::move(payload.value);
    } catch (...) {
      completed_task.error = std::current_exception();
    }

    {
      std::lock_guard<std::mutex> lock(result_mutex);
      completed.push(std::move(completed_task));
    }
    result_cv.notify_one();
  }
}

template <class F, class... Args>
ThreadPool::TaskId ThreadPool::enqueue(F &&f, Args &&...args) {
  using ReturnType = std::invoke_result_t<F, Args...>;

  // Bind both the callable and its arguments into a zero-arg closure so each
  // worker can execute it without extra coordination.
  auto bound = [fn = std::forward<F>(f),
                tuple_args =
                  std::make_tuple(std::forward<Args>(args)...)]() mutable {
    TaskPayload payload;
    if constexpr (std::is_void_v<ReturnType>) {
      std::apply(std::move(fn), std::move(tuple_args));
      payload.has_value = false;
    } else {
      payload.value =
        std::any(std::apply(std::move(fn), std::move(tuple_args)));
      payload.has_value = true;
    }
    return payload;
  };

  TaskId id;
  {
    std::lock_guard<std::mutex> lock(task_mutex);
    if (stopping) {
      throw std::runtime_error("enqueue on stopped ThreadPool");
    }
    id = next_id++;
    tasks.push(Task{id, std::move(bound)});
  }
  task_cv.notify_one();
  return id;
}

inline bool ThreadPool::try_pop_completed(CompletedTask &out) {
  std::lock_guard<std::mutex> lock(result_mutex);
  if (completed.empty()) {
    return false;
  }
  out = std::move(completed.front());
  completed.pop();
  return true;
}

inline std::optional<ThreadPool::CompletedTask>
ThreadPool::wait_pop_completed() {
  std::unique_lock<std::mutex> lock(result_mutex);
  result_cv.wait(lock, [this] {
    return !completed.empty() || (stopping && tasks.empty());
  });
  if (completed.empty()) {
    return std::nullopt;
  }
  CompletedTask out = std::move(completed.front());
  completed.pop();
  return out;
}

template <class Rep, class Period>
std::optional<ThreadPool::CompletedTask> ThreadPool::wait_pop_completed_for(
  const std::chrono::duration<Rep, Period> &timeout) {
  std::unique_lock<std::mutex> lock(result_mutex);
  if (!result_cv.wait_for(lock, timeout, [this] {
        return !completed.empty() || (stopping && tasks.empty());
      })) {
    return std::nullopt;
  }
  if (completed.empty()) {
    return std::nullopt;
  }
  CompletedTask out = std::move(completed.front());
  completed.pop();
  return out;
}

inline std::size_t ThreadPool::pending_tasks() const {
  std::lock_guard<std::mutex> lock(task_mutex);
  return tasks.size();
}

/* Example usage:
 *
 *   pyle::ThreadPool pool(10);
 *   std::unique_ptr<pyle::Cache> state_cache = make_cache(capacity);
 *
 *   // Schedule work; lambdas can capture references to shared state such as
 *   // a cache guarded by its own mutex.
 *   auto job_id = pool.enqueue([&state_cache](const std::string &header) {
 *     auto state = state_cache->get(header); // Cache exposes its own locking.
 *     if (!state) {
 *       throw std::runtime_error("missing header state");
 *     }
 *     return state;
 *   }, "header_name");
 *
 *   // Harvest completions from another thread (or the same one).
 *   while (auto completed = pool.wait_pop_completed()) {
 *     if (completed->id == job_id) {
 *       completed->rethrow_if_error();
 *       auto &value = completed->value<std::shared_ptr<lean_object>>();
 *       use_state(value);
 *       break;
 *     }
 *   }
 *
 *   pool.shutdown(); // optional: destructor calls this automatically
 */

} // namespace pyle
