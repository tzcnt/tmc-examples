#pragma once

// A minimal, self-contained thread pool that stands in for a "3rd party"
// library that TMC knows nothing about. It has no knowledge of TMC: no TMC
// headers are included here and no TMC types appear in its interface.
//
// It owns nothing but a work queue. Threads are supplied externally by calling
// run(); each caller of run() participates in draining the queue until
// request_stop() is called. This mirrors how many real thread pool libraries
// separate "the pool" from "the threads that drive it".
//
// See external_executor.cpp for the TMC adapter that wraps this type.

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

class external_executor {
  std::mutex mut;
  std::condition_variable work_available;
  std::deque<std::function<void()>> queue;
  bool stop_requested = false;

public:
  /// Enqueue a work item. It will be executed by whichever thread is currently
  /// inside run(). Note that this pool has no concept of priority.
  void post(std::function<void()> Work) {
    {
      std::lock_guard<std::mutex> lock(mut);
      queue.push_back(std::move(Work));
    }
    work_available.notify_one();
  }

  /// Participate in the run loop. This blocks the calling thread, executing
  /// work items as they arrive, until request_stop() is called and the queue
  /// has been fully drained.
  void run() {
    std::unique_lock<std::mutex> lock(mut);
    while (true) {
      work_available.wait(lock, [this] {
        return stop_requested || !queue.empty();
      });
      while (!queue.empty()) {
        std::function<void()> work = std::move(queue.front());
        queue.pop_front();
        // Release the lock while running user work so that work items may post
        // more work (e.g. a resumed coroutine scheduling its continuation).
        lock.unlock();
        work();
        lock.lock();
      }
      if (stop_requested) {
        return;
      }
    }
  }

  /// Signal every thread inside run() to return once the queue is empty.
  void request_stop() {
    {
      std::lock_guard<std::mutex> lock(mut);
      stop_requested = true;
    }
    work_available.notify_all();
  }
};
