#pragma once

#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

// ============================================================================
// Static Thread Pool - Persistent worker threads for gem detection
// ============================================================================

class StaticThreadPool {
public:
  static constexpr int NUM_THREADS = 12;

  static StaticThreadPool &Instance() {
    static StaticThreadPool instance;
    return instance;
  }

  // Submit work and wait for completion
  // workFunc(threadIndex, numThreads) - each thread calls this with its index
  template <typename Func> void ParallelFor(Func &&workFunc) {
    // Store work function and signal new generation
    {
      std::lock_guard<std::mutex> lock(mutex_);
      using WorkFunc = std::decay_t<Func>;
      workContext_ = static_cast<void *>(std::addressof(workFunc));
      workFn_ = &StaticThreadPool::InvokeWork<WorkFunc>;
      generation_++;
      completedCount_ = 0;
    }
    startCv_.notify_all();

    // Wait for all workers to complete this generation
    {
      std::unique_lock<std::mutex> lock(mutex_);
      doneCv_.wait(lock, [this] { return completedCount_ >= NUM_THREADS; });
    }
  }

  ~StaticThreadPool() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      shouldExit_ = true;
    }
    startCv_.notify_all();

    for (auto &t : threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
  }

private:
  StaticThreadPool() {
    for (int i = 0; i < NUM_THREADS; i++) {
      threads_[i] = std::thread(&StaticThreadPool::WorkerLoop, this, i);
    }
  }

  void WorkerLoop(int threadIndex) {
    int lastGeneration = 0;

    while (true) {
      WorkFn fn = nullptr;
      void *ctx = nullptr;

      // Wait for new work (new generation)
      {
        std::unique_lock<std::mutex> lock(mutex_);
        startCv_.wait(lock, [this, &lastGeneration] {
          return generation_ > lastGeneration || shouldExit_;
        });

        if (shouldExit_) {
          return;
        }

        lastGeneration = generation_;
        fn = workFn_;
        ctx = workContext_;
      }

      // Execute work
      if (fn) {
        fn(ctx, threadIndex, NUM_THREADS);
      }

      // Signal completion
      {
        std::lock_guard<std::mutex> lock(mutex_);
        completedCount_++;
      }
      doneCv_.notify_one();
    }
  }

  using WorkFn = void (*)(void *, int, int);

  template <typename Func>
  static void InvokeWork(void *ctx, int idx, int total) {
    auto *func = static_cast<Func *>(ctx);
    (*func)(idx, total);
  }

  std::array<std::thread, NUM_THREADS> threads_;
  WorkFn workFn_ = nullptr;
  void *workContext_ = nullptr;
  std::mutex mutex_;
  std::condition_variable startCv_;
  std::condition_variable doneCv_;
  int completedCount_ = 0;
  int generation_ = 0;
  bool shouldExit_ = false;
};