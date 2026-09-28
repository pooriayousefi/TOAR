// ============================================================================
//  io_thread_pool.hpp — Cross-Platform Async Thread Pool Executor
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
// ============================================================================
#pragma once

#include <algorithm>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <memory>
#include <vector>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <future>
#include <expected>
#include <span>
#include <system_error>
#include <optional>
#include <exception>
#include <type_traits>
#include <utility>
#include <functional>

#include "asyncore.hpp"

namespace pooriayousefi::io_bound
{
    using namespace core;

    // ---- ThreadPool: standard thread pool with coroutine bridging ----
    //
    // Pragmatic simplicity: Instead of a complex OS-level reactor (epoll/IOCP),
    // this executor uses standard C++ threads. Coroutines suspend and yield
    // execution to the pool. Blocking I/O (like reading from an MCP server
    // subprocess) is dispatched via run_blocking(), which safely parks the
    // coroutine, executes the blocking OS call on a worker thread, and
    // resumes the coroutine when data is ready. This is highly efficient
    // for single-user, multi-agent local runtimes.

    class ThreadPool
    {
    public:
        explicit ThreadPool(std::size_t worker_count = 0)
        {
            if (worker_count == 0)
            {
                worker_count = std::thread::hardware_concurrency();
                if (worker_count == 0)
                {
                    worker_count = 2;
                }
            }

            workers_.reserve(worker_count);

            for (std::size_t id = 0; id < worker_count; ++id)
            {
                workers_.emplace_back(
                    [this]()
                    {
                        worker_loop();
                    }
                );
            }
        }

        ~ThreadPool()
        {
            stopped_.store(true, std::memory_order_release);
            cv_.notify_all();

            for (auto& w : workers_)
            {
                if (w.joinable())
                {
                    w.join();
                }
            }
        }

        ThreadPool(ThreadPool const&) = delete;
        ThreadPool& operator=(ThreadPool const&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        // ----------------------------------------------------------------
        //  Awaitables (public so auto return type is accessible)
        // ----------------------------------------------------------------

        template <typename F, typename R>
        struct BlockingAwaitableValue
        {
            ThreadPool* pool_;
            F func_;
            std::optional<R> result_{};
            std::exception_ptr exc_{nullptr};

            bool await_ready() const noexcept
            {
                bool is_ready = false;
                return is_ready;
            }

            void await_suspend(std::coroutine_handle<> h)
            {
                pool_->enqueue_raw(
                    [this, h]()
                    {
                        try
                        {
                            result_ = func_();
                        }
                        catch (...)
                        {
                            exc_ = std::current_exception();
                        }
                        h.resume();
                    }
                );
            }

            R await_resume()
            {
                if (exc_)
                {
                    std::rethrow_exception(exc_);
                }
                return std::move(*result_);
            }
        };

        template <typename F>
        struct BlockingAwaitableVoid
        {
            ThreadPool* pool_;
            F func_;
            std::exception_ptr exc_{nullptr};

            bool await_ready() const noexcept
            {
                bool is_ready = false;
                return is_ready;
            }

            void await_suspend(std::coroutine_handle<> h)
            {
                pool_->enqueue_raw(
                    [this, h]()
                    {
                        try
                        {
                            func_();
                        }
                        catch (...)
                        {
                            exc_ = std::current_exception();
                        }
                        h.resume();
                    }
                );
            }

            void await_resume()
            {
                if (exc_)
                {
                    std::rethrow_exception(exc_);
                }
            }
        };

        // ----------------------------------------------------------------
        //  submit — run a plain callable on a worker thread, return future
        // ----------------------------------------------------------------

        template <typename F, typename... Args>
            requires std::invocable<F, Args...>
        [[nodiscard]] auto submit(F&& f, Args&&... args)
            -> std::future<std::invoke_result_t<F, Args...>>
        {
            using R = std::invoke_result_t<F, Args...>;

            auto prom = std::make_shared<std::promise<R>>();
            auto fut = prom->get_future();

            auto bound = [f = std::forward<F>(f),
                          args_tuple = std::make_tuple(std::forward<Args>(args)...)]() mutable -> R
            {
                return std::apply(std::move(f), std::move(args_tuple));
            };

            enqueue_raw(
                [prom, bound = std::move(bound)]() mutable
                {
                    try
                    {
                        if constexpr (std::is_void_v<R>)
                        {
                            bound();
                            prom->set_value();
                        }
                        else
                        {
                            prom->set_value(bound());
                        }
                    }
                    catch (...)
                    {
                        prom->set_exception(std::current_exception());
                    }
                }
            );

            return fut;
        }

        // ----------------------------------------------------------------
        //  schedule — coroutine awaitable: yield to a worker thread
        // ----------------------------------------------------------------

        struct ScheduleAwaitable
        {
            ThreadPool* pool;

            bool await_ready() const noexcept
            {
                bool is_ready = false;
                return is_ready;
            }

            void await_suspend(std::coroutine_handle<> h) const
            {
                pool->enqueue_raw([h]() { h.resume(); });
            }

            void await_resume() const noexcept
            {
            }
        };

        ScheduleAwaitable schedule() noexcept
        {
            return ScheduleAwaitable{this};
        }

        // ----------------------------------------------------------------
        //  run_blocking — coroutine awaitable: run blocking I/O on a worker
        // 
        //  Usage: auto bytes = co_await pool.run_blocking([&](){ return read(fd, buf, size); });
        // ----------------------------------------------------------------

        template <typename F>
        auto run_blocking(F&& f)
        {
            using R = std::invoke_result_t<F>;

            if constexpr (std::is_void_v<R>)
            {
                return BlockingAwaitableVoid<std::decay_t<F>>{this, std::forward<F>(f)};
            }
            else
            {
                return BlockingAwaitableValue<std::decay_t<F>, R>{this, std::forward<F>(f)};
            }
        }

        // ----------------------------------------------------------------
        //  run — execute an AsyncTask<T> on a worker thread, return future
        // ----------------------------------------------------------------

        template <typename T>
        std::future<T> run(AsyncTask<T> t)
        {
            auto prom = std::make_shared<std::promise<T>>();
            auto fut = prom->get_future();

            auto exec = [](AsyncTask<T> t, std::shared_ptr<std::promise<T>> prom) -> DetachedTask
            {
                try
                {
                    if constexpr (std::is_void_v<T>)
                    {
                        co_await t;
                        prom->set_value();
                    }
                    else
                    {
                        T val = co_await t;
                        prom->set_value(std::move(val));
                    }
                }
                catch (...)
                {
                    prom->set_exception(std::current_exception());
                }
                co_return;
            };

            // DetachedTask starts suspended — safe to capture handle here.
            auto dt = exec(std::move(t), prom);
            auto h = dt.handle;
            dt.detach();

            // Resume the coroutine on a worker thread.
            enqueue_raw(
                [h]()
                {
                    h.resume();
                }
            );

            return fut;
        }

        // ----------------------------------------------------------------
        //  Accessors
        // ----------------------------------------------------------------

        std::size_t worker_count() const noexcept
        {
            return workers_.size();
        }

        // ----------------------------------------------------------------
        //  Internal
        // ----------------------------------------------------------------

        void enqueue_raw(MoveOnlyFunction task)
        {
            bool drop_task = false;
            if (stopped_.load(std::memory_order_acquire))
            {
                drop_task = true; // Silently drop — pool is shutting down.
            }
            else if (workers_.empty())
            {
                drop_task = true; // Defensive — no workers available.
            }

            if (!drop_task)
            {
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    queue_.push(std::move(task));
                }
                cv_.notify_one();
            }
        }

    private:
        std::vector<std::thread> workers_;
        std::queue<MoveOnlyFunction> queue_;
        std::mutex mtx_;
        std::condition_variable cv_;
        std::atomic<bool> stopped_{false};

        void worker_loop()
        {
            bool running = true;
            while (running)
            {
                MoveOnlyFunction task;
                bool has_task = false;

                {
                    std::unique_lock<std::mutex> lock(mtx_);
                    cv_.wait(lock, [this]() { return stopped_.load() || !queue_.empty(); });

                    if (stopped_.load() && queue_.empty())
                    {
                        running = false;
                    }
                    else if (!queue_.empty())
                    {
                        task = std::move(queue_.front());
                        queue_.pop();
                        has_task = true;
                    }
                }

                if (has_task)
                {
                    try
                    {
                        task();
                    }
                    catch (...)
                    {
                        // Swallow exceptions in scheduled tasks — don't crash the pool.
                    }
                }
            }
        }
    };
}