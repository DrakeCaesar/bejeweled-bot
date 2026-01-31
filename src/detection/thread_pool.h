#pragma once

#include <array>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// ============================================================================
// Static Thread Pool - Persistent worker threads for gem detection
// ============================================================================

class StaticThreadPool {
public:
  static constexpr int NUM_THREADS = 8;

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
      work_ = [&workFunc](int idx, int total) { workFunc(idx, total); };
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
      }

      // Execute work
      if (work_) {
        work_(threadIndex, NUM_THREADS);
      }

      // Signal completion
      {
        std::lock_guard<std::mutex> lock(mutex_);
        completedCount_++;
      }
      doneCv_.notify_one();
    }
  }

  std::array<std::thread, NUM_THREADS> threads_;
  std::function<void(int, int)> work_;
  std::mutex mutex_;
  std::condition_variable startCv_;
  std::condition_variable doneCv_;
  int completedCount_ = 0;
  int generation_ = 0;
  bool shouldExit_ = false;
};
