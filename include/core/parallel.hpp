// Small CPU parallelism helpers.
//
// The default is one worker per hardware thread.  Set DCSTUDY_JOBS=N to cap
// the pool.  Tasks are independent by contract: each task gets a distinct
// index and must not share mutable state with another task.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace dcstudy {

inline int hardware_jobs() {
  int n = static_cast<int>(std::thread::hardware_concurrency());
  if (n <= 0)
    n = 1;
  return std::max(1, n);
}

inline int parallel_jobs() {
  if (const char *s = std::getenv("DCSTUDY_JOBS")) {
    try {
      const int n = std::stoi(s);
      if (n > 0)
        return n;   // explicit user choice; oversubscription is their call
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
  }
  return hardware_jobs();
}

template <class Fn> void parallel_for(std::size_t count, int jobs, Fn &&fn) {
  if (count == 0)
    return;
  const std::size_t workers =
      std::min<std::size_t>(count, static_cast<std::size_t>(std::max(1, jobs)));
  if (workers <= 1) {
    for (std::size_t i = 0; i < count; ++i)
      fn(i);
    return;
  }

  std::atomic<std::size_t> next{0};
  std::atomic<bool> stopped{false};
  std::mutex error_mutex;
  std::exception_ptr error;
  auto worker = [&] {
    while (!stopped.load(std::memory_order_relaxed)) {
      const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
      if (i >= count)
        return;
      try {
        fn(i);
      } catch (...) {
        std::lock_guard<std::mutex> lock(error_mutex);
        if (!error)
          error = std::current_exception();
        stopped.store(true, std::memory_order_relaxed);
        return;
      }
    }
  };
  std::vector<std::thread> pool;
  pool.reserve(workers);
  for (std::size_t t = 0; t < workers; ++t) {
    try {
      pool.emplace_back(worker);
    } catch (const std::exception &) {
      // Thread budget exhausted (a memory cap counts each stack).  Destroying
      // a joinable std::thread calls std::terminate, so keep the pool we have
      // and let this thread cover the rest instead of aborting.
      break;
    }
  }
  worker();
  for (auto &t : pool)
    t.join();
  if (error)
    std::rethrow_exception(error);
}

} // namespace dcstudy
