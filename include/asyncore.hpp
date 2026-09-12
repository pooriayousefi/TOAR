// ============================================================================
//  asyncore.hpp — C++23 Coroutine Foundation (Enterprise Grade)
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
// ============================================================================
#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <semaphore>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <functional>

namespace pooriayousefi::core
{
    // Enterprise Enhancement: Cancellation Exception
    struct CancelledException : public std::runtime_error
    {
        CancelledException() : std::runtime_error{"Operation was cancelled."}
        {
        }
    };

    // Cancellation Token
    class CancellationToken
    {
    public:
        CancellationToken() = default;
        CancellationToken(const CancellationToken&) = delete;
        CancellationToken& operator=(const CancellationToken&) = delete;
        CancellationToken(CancellationToken&&) = delete;
        CancellationToken& operator=(CancellationToken&&) = delete;

        void cancel() noexcept
        {
            cancelled_.store(true, std::memory_order_release);
        }

        bool is_cancelled() const noexcept
        {
            return cancelled_.load(std::memory_order_acquire);
        }

        // Enterprise Enhancement: throw_if_cancelled
        void throw_if_cancelled() const
        {
            if (is_cancelled())
            {
                throw CancelledException{};
            }
        }

    private:
        std::atomic<bool> cancelled_{false};
    };

    // Asynchronous Generator for streaming
    template <class T>
    struct AsyncGenerator
    {
        struct Promise
        {
            T current_value{};
            std::exception_ptr exception{nullptr};
            std::coroutine_handle<> continuation{nullptr};

            AsyncGenerator get_return_object()
            {
                return AsyncGenerator{std::coroutine_handle<Promise>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct YieldAwaitable
            {
                Promise& p;

                bool await_ready() const noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise>) noexcept
                {
                    return p.continuation ? p.continuation : std::noop_coroutine();
                }

                void await_resume()
                {
                    if (p.exception)
                    {
                        std::rethrow_exception(p.exception);
                    }
                }
            };

            YieldAwaitable yield_value(T value)
            {
                current_value = std::move(value);
                return {*this};
            }

            void return_void() noexcept
            {
            }

            void unhandled_exception()
            {
                exception = std::current_exception();
            }

            struct FinalAwaitable
            {
                bool await_ready() const noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept
                {
                    auto cont = h.promise().continuation;
                    return cont ? cont : std::noop_coroutine();
                }

                void await_resume() noexcept
                {
                }
            };

            FinalAwaitable final_suspend() noexcept
            {
                return {};
            }
        };

        using promise_type = Promise;

        struct Iterator
        {
            using iterator_category = std::input_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = T*;
            using reference = T&;

            std::coroutine_handle<promise_type> handle{nullptr};

            explicit Iterator(std::coroutine_handle<promise_type> h) noexcept : handle{h}
            {
            }

            Iterator& operator++()
            {
                handle.promise().continuation = std::noop_coroutine();
                handle.resume();
                if (handle.promise().exception)
                {
                    std::rethrow_exception(handle.promise().exception);
                }
                return *this;
            }

            void operator++(int)
            {
                (void)operator++();
            }

            reference operator*() const
            {
                return handle.promise().current_value;
            }

            pointer operator->() const
            {
                return std::addressof(operator*());
            }

            bool operator==(std::default_sentinel_t) const noexcept
            {
                return !handle || handle.done();
            }
        };

        std::coroutine_handle<promise_type> handle{nullptr};

        explicit AsyncGenerator(std::coroutine_handle<promise_type> h) noexcept : handle{h}
        {
        }

        AsyncGenerator() noexcept = default;

        ~AsyncGenerator()
        {
            if (handle)
            {
                handle.destroy();
            }
        }

        AsyncGenerator(AsyncGenerator const&) = delete;
        AsyncGenerator& operator=(AsyncGenerator const&) = delete;

        AsyncGenerator(AsyncGenerator&& other) noexcept : handle{other.handle}
        {
            other.handle = nullptr;
        }

        AsyncGenerator& operator=(AsyncGenerator&& other) noexcept
        {
            if (this != &other)
            {
                if (handle)
                {
                    handle.destroy();
                }
                handle = other.handle;
                other.handle = nullptr;
            }
            return *this;
        }

        Iterator begin()
        {
            if (handle)
            {
                handle.promise().continuation = std::noop_coroutine();
                handle.resume();
                if (handle.promise().exception)
                {
                    std::rethrow_exception(handle.promise().exception);
                }
            }
            return Iterator{handle};
        }

        std::default_sentinel_t end() noexcept
        {
            return {};
        }
    };

    // Fire-and-forget AsyncTask
    struct FireAndForget
    {
        struct Promise
        {
            [[nodiscard]] FireAndForget get_return_object() noexcept
            {
                return FireAndForget{std::coroutine_handle<Promise>::from_promise(*this)};
            }

            std::suspend_never initial_suspend() noexcept
            {
                return {};
            }

            struct FinalAwaitable
            {
                bool await_ready() const noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise>) noexcept
                {
                    return std::noop_coroutine();
                }

                void await_resume() noexcept
                {
                }
            };

            FinalAwaitable final_suspend() noexcept
            {
                return {};
            }

            void return_void() noexcept
            {
            }

            [[noreturn]] void unhandled_exception()
            {
                std::terminate();
            }
        };

        using promise_type = Promise;

        std::coroutine_handle<promise_type> handle{nullptr};

        explicit FireAndForget(std::coroutine_handle<promise_type> h) noexcept : handle{h}
        {
        }

        FireAndForget() noexcept = default;

        FireAndForget(FireAndForget&& other) noexcept : handle{other.handle}
        {
            other.handle = nullptr;
        }

        FireAndForget& operator=(FireAndForget&& other) noexcept
        {
            if (this != &other)
            {
                if (handle)
                {
                    handle.destroy();
                }
                handle = other.handle;
                other.handle = nullptr;
            }
            return *this;
        }

        ~FireAndForget()
        {
            if (handle)
            {
                handle.destroy();
            }
        }

        FireAndForget(FireAndForget const&) = delete;
        FireAndForget& operator=(FireAndForget const&) = delete;

        void detach() noexcept
        {
            handle = nullptr;
        }
    };

    // ---- DetachedTask: self-destroying coroutine ----
    //
    // Starts suspended (lazy). When resumed, runs to completion. On
    // completion, the FinalAwaitable destroys the frame — no leak.
    // Must be detached before the handle is resumed on another thread.

    struct DetachedTask
    {
        struct Promise
        {
            [[nodiscard]] DetachedTask get_return_object() noexcept
            {
                return DetachedTask{std::coroutine_handle<Promise>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct FinalAwaitable
            {
                bool await_ready() const noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept
                {
                    h.destroy();
                    return std::noop_coroutine();
                }

                void await_resume() noexcept
                {
                }
            };

            FinalAwaitable final_suspend() noexcept
            {
                return {};
            }

            void return_void() noexcept
            {
            }

            [[noreturn]] void unhandled_exception()
            {
                std::terminate();
            }
        };

        using promise_type = Promise;

        std::coroutine_handle<Promise> handle{nullptr};

        explicit DetachedTask(std::coroutine_handle<Promise> h) noexcept : handle{h}
        {
        }

        DetachedTask() noexcept = default;

        DetachedTask(DetachedTask&& other) noexcept : handle{other.handle}
        {
            other.handle = nullptr;
        }

        DetachedTask& operator=(DetachedTask&& other) noexcept
        {
            if (this != &other)
            {
                if (handle)
                {
                    handle.destroy();
                }
                handle = other.handle;
                other.handle = nullptr;
            }
            return *this;
        }

        ~DetachedTask()
        {
            if (handle)
            {
                handle.destroy();
            }
        }

        DetachedTask(DetachedTask const&) = delete;
        DetachedTask& operator=(DetachedTask const&) = delete;

        void detach() noexcept
        {
            handle = nullptr;
        }
    };

    // Awaitable Task
    template <class T>
    struct AsyncTask
    {
        using value_type = T;

        struct Promise
        {
            std::variant<std::monostate, T, std::exception_ptr> result;
            std::coroutine_handle<> continuation{};

            [[nodiscard]] AsyncTask get_return_object() noexcept
            {
                return AsyncTask{std::coroutine_handle<Promise>::from_promise(*this)};
            }

            void return_value(T value)
            {
                result.template emplace<1>(std::move(value));
            }

            void unhandled_exception() noexcept
            {
                result.template emplace<2>(std::current_exception());
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct FinalAwaitable
            {
                bool await_ready() const noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept
                {
                    auto cont = h.promise().continuation;
                    return cont ? cont : std::noop_coroutine();
                }

                void await_resume() noexcept
                {
                }
            };

            FinalAwaitable final_suspend() noexcept
            {
                return {};
            }
        };

        using promise_type = Promise;

        std::coroutine_handle<promise_type> handle{nullptr};

        explicit AsyncTask(std::coroutine_handle<promise_type> h) noexcept : handle{h}
        {
        }

        AsyncTask(AsyncTask&& t) noexcept : handle{t.handle}
        {
            t.handle = nullptr;
        }

        AsyncTask(AsyncTask const&) = delete;
        AsyncTask& operator=(AsyncTask const&) = delete;
        AsyncTask& operator=(AsyncTask&&) = delete;

        ~AsyncTask()
        {
            if (handle)
            {
                handle.destroy();
            }
        }

        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<promise_type> await_suspend(std::coroutine_handle<> c) noexcept
        {
            handle.promise().continuation = c;
            return handle;
        }

        T await_resume()
        {
            auto& r = handle.promise().result;
            T result_value{};
            if (r.index() == 2)
            {
                std::rethrow_exception(std::get<2>(std::move(r)));
            }
            else if (r.index() == 1)
            {
                result_value = std::get<1>(std::move(r));
            }
            else
            {
                std::terminate();
            }
            return result_value;
        }
    };

    template <>
    struct AsyncTask<void>
    {
        using value_type = void;

        struct Promise
        {
            std::exception_ptr exception{nullptr};
            std::coroutine_handle<> continuation{};

            [[nodiscard]] AsyncTask get_return_object() noexcept
            {
                return AsyncTask{std::coroutine_handle<Promise>::from_promise(*this)};
            }

            void return_void() noexcept
            {
            }

            void unhandled_exception() noexcept
            {
                exception = std::current_exception();
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            struct FinalAwaitable
            {
                bool await_ready() const noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept
                {
                    auto cont = h.promise().continuation;
                    return cont ? cont : std::noop_coroutine();
                }

                void await_resume() noexcept
                {
                }
            };

            FinalAwaitable final_suspend() noexcept
            {
                return {};
            }
        };

        using promise_type = Promise;

        std::coroutine_handle<promise_type> handle{nullptr};

        explicit AsyncTask(std::coroutine_handle<promise_type> h) noexcept : handle{h}
        {
        }

        AsyncTask(AsyncTask&& t) noexcept : handle{t.handle}
        {
            t.handle = nullptr;
        }

        AsyncTask(AsyncTask const&) = delete;
        AsyncTask& operator=(AsyncTask const&) = delete;
        AsyncTask& operator=(AsyncTask&&) = delete;

        ~AsyncTask()
        {
            if (handle)
            {
                handle.destroy();
            }
        }

        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<promise_type> await_suspend(std::coroutine_handle<> c) noexcept
        {
            handle.promise().continuation = c;
            return handle;
        }

        void await_resume()
        {
            if (handle.promise().exception)
            {
                std::rethrow_exception(handle.promise().exception);
            }
        }
    };
        
    template <class T>
    using AsyncResultType = decltype(std::declval<T&>().await_resume());

    template <class T>
    struct SyncWaitTask
    {
        struct Promise
        {
            T* value{nullptr};
            std::exception_ptr error{nullptr};
            std::binary_semaphore semaphore_{0};

            inline SyncWaitTask get_return_object() noexcept
            {
                return SyncWaitTask{std::coroutine_handle<Promise>::from_promise(*this)};
            }

            constexpr void unhandled_exception() noexcept
            {
                error = std::current_exception();
            }

            constexpr decltype(auto) yield_value(T&& x) noexcept
            {
                value = std::addressof(x);
                return final_suspend();
            }

            constexpr decltype(auto) initial_suspend() noexcept
            {
                return std::suspend_always{};
            }

            struct FinalAwaitable
            {
                constexpr bool await_ready() noexcept
                {
                    return false;
                }

                constexpr void await_suspend(std::coroutine_handle<Promise> h) noexcept
                {
                    h.promise().semaphore_.release();
                }

                constexpr void await_resume() noexcept
                {
                }
            };

            constexpr decltype(auto) final_suspend() noexcept
            {
                return FinalAwaitable{};
            }

            [[noreturn]] constexpr void return_void() noexcept
            {
                std::terminate();
            }
        };

        using promise_type = Promise;

        std::coroutine_handle<promise_type> handle{nullptr};

        explicit SyncWaitTask(std::coroutine_handle<promise_type> h) noexcept : handle{h}
        {
        }

        SyncWaitTask(SyncWaitTask&& t) noexcept : handle{t.handle}
        {
            t.handle = nullptr;
        }

        ~SyncWaitTask()
        {
            if (handle)
            {
                handle.destroy();
            }
        }

        SyncWaitTask(SyncWaitTask const&) = delete;
        SyncWaitTask& operator=(SyncWaitTask const&) = delete;
        SyncWaitTask& operator=(SyncWaitTask&&) = delete;

        inline T&& get()
        {
            auto& p = handle.promise();
            handle.resume();
            p.semaphore_.acquire();
            if (p.error)
            {
                std::rethrow_exception(p.error);
            }
            return static_cast<T&&>(*p.value);
        }
    };

    template <class T>
    AsyncResultType<T> sync_wait(T&& t)
    {
        if constexpr (std::is_void_v<AsyncResultType<T>>)
        {
            struct EmptyType
            {
            };

            auto coro = [&]() -> SyncWaitTask<EmptyType>
            {
                co_await std::forward<T>(t);
                co_yield EmptyType{};
                assert(false);
                __builtin_unreachable();
            };

            coro().get();
        }
        else
        {
            auto coro = [&]() -> SyncWaitTask<AsyncResultType<T>>
            {
                co_yield co_await std::forward<T>(t);
                assert(false);
                __builtin_unreachable();
            };

            return coro().get();
        }
    }

    class MoveOnlyFunction
    {
        struct Base
        {
            virtual ~Base() = default;
            virtual void invoke() = 0;
        };

        template <typename F>
        struct Model : Base
        {
            F func;

            explicit Model(F&& f) : func(std::move(f))
            {
            }

            void invoke() override
            {
                func();
            }
        };

        std::unique_ptr<Base> impl_;

    public:
        MoveOnlyFunction() = default;

        MoveOnlyFunction(MoveOnlyFunction&&) = default;
        MoveOnlyFunction& operator=(MoveOnlyFunction&&) = default;
        MoveOnlyFunction(const MoveOnlyFunction&) = delete;
        MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

        template <typename F>
            requires(!std::is_same_v<std::decay_t<F>, MoveOnlyFunction>)
        MoveOnlyFunction(F&& f) : impl_(std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(f)))
        {
        }

        void operator()()
        {
            if (impl_)
            {
                impl_->invoke();
            }
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return impl_ != nullptr;
        }
    };
}