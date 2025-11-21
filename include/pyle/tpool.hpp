#pragma once
#include "pyle/lean.hpp"
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <vector>

class ThreadPool {
public:
  explicit ThreadPool(size_t thread_count) : stop(false) {
    for (size_t i = 0; i < thread_count; ++i) {
      workers.emplace_back([this]() {
        lean_initialize_thread();
        // Worker loop which runs forever
        while (true) {
          std::function<void()> task;

          { // wait for a task or shutdown
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this]() { return stop || !tasks.empty(); });

            if (stop && tasks.empty())
              break;

            task = std::move(tasks.front());
            tasks.pop();
          }

          task(); // execute
        }
        lean_finalize_thread();
      });
    }
  }

  // non-copyable
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  ~ThreadPool() { shutdown(); }

  template <class F, class... Args>
  auto enqueue(F &&f, Args &&...args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    using ReturnType = std::invoke_result_t<F, Args...>;

    // wrap the function in a packaged_task
    auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<ReturnType> fut = task_ptr->get_future();

    {
      std::lock_guard<std::mutex> lock(mutex);

      if (stop)
        throw std::runtime_error("enqueue on stopped ThreadPool");

      tasks.emplace([task_ptr]() { (*task_ptr)(); });
    }

    cv.notify_one();
    return fut;
  }

  void shutdown() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stop = true;
    }

    cv.notify_all();

    for (std::thread &t : workers)
      if (t.joinable()) {
        t.join();
      }
  }

private:
  std::vector<std::thread> workers;
  std::queue<std::function<void()>> tasks;

  std::mutex mutex;
  std::condition_variable cv;
  std::condition_variable shutdown_cv;
  bool stop;
};
