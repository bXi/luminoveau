#pragma once

// A small fixed-size worker pool for fan-out/join work (used by the sprite packer).
// Falls back to inline execution when constructed with zero workers — e.g. Emscripten
// without -pthread — so Enqueue + WaitAll still make progress instead of deadlocking.

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/// @cond INTERNAL
struct TaskBase {
    virtual ~TaskBase()       = default;
    virtual void operator()() = 0;
};

template <typename F>
struct TaskImpl : TaskBase {
    F f;
    TaskImpl(F &&func)
        : f(std::move(func)) { }
    void operator()() override { f(); }
};
/// @endcond

/// @brief Fixed-size thread pool. Construct with worker count; 0 runs everything inline.
class ThreadPool {
public:
    using Task = std::unique_ptr<TaskBase>;

    ThreadPool(size_t numThreads)
        : _tasksRunning(0) {
        for (size_t i = 0; i < numThreads; ++i) {
            _workers.emplace_back([this] {
                while (true) {
                    Task task;
                    {
                        std::unique_lock<std::mutex> lock(_queueMutex);
                        _condition.wait(lock, [this] { return !_tasks.empty() || _stop; });
                        if (_stop && _tasks.empty())
                            return;
                        task = std::move(_tasks.front());
                        _tasks.pop();
                        // Count under the lock so WaitAll() never observes an empty queue
                        // while a popped-but-not-yet-counted task is still in flight.
                        _tasksRunning++;
                    }
                    (*task)();
                    {
                        std::unique_lock<std::mutex> lock(_queueMutex);
                        if (--_tasksRunning == 0 && _tasks.empty())
                            _doneCondition.notify_all();
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        std::unique_lock<std::mutex> lock(_queueMutex);
        _stop = true;
        lock.unlock();
        _condition.notify_all();
        for (auto &worker : _workers)
            worker.join();
    }

    /// @brief Queues a task, or runs it inline if the pool has no workers.
    template <typename F>
    void Enqueue(F &&task) {
        if (_workers.empty()) {
            task();
            return;
        }
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _tasks.push(std::make_unique<TaskImpl<F>>(std::forward<F>(task)));
        }
        _condition.notify_one();
    }

    /// @brief Blocks until the queue is drained and all in-flight tasks finish.
    void WaitAll() {
        std::unique_lock<std::mutex> lock(_queueMutex);
        _doneCondition.wait(lock, [this] { return _tasks.empty() && _tasksRunning == 0; });
    }

    /// @brief Number of worker threads (0 = inline execution).
    int GetThreadCount() {
        return _workers.size();
    }

private:
    std::vector<std::thread> _workers;
    std::queue<Task>         _tasks;
    std::mutex               _queueMutex;
    std::condition_variable  _condition;     // signals workers: work available / stop
    std::condition_variable  _doneCondition; // signals WaitAll(): queue drained
    size_t                   _tasksRunning;  // guarded by _queueMutex
    bool                     _stop = false;
};
