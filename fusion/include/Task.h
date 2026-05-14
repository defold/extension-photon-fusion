// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoroutineCompat.h"
#include "Result.h"
#include <exception>
#include <type_traits>
#include <utility>
#include <variant>

namespace PhotonCommon {
    template<typename T = void>
    class Task;

    namespace detail {
        template<typename>
        struct is_task : std::false_type {};

        template<typename U>
        struct is_task<Task<U>> : std::true_type {};

        template<typename T>
        inline constexpr bool is_task_v = is_task<T>::value;

        template<typename X>
        struct unwrap_task {
            using type = X;
        };

        template<typename U>
        struct unwrap_task<Task<U>> {
            using type = U;
        };

        template<typename X>
        using unwrap_task_t = typename unwrap_task<X>::type;
    } // namespace detail

    template<typename T>
    class Task {
    public:
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;

        struct promise_type {
            std::variant<std::monostate, T, std::exception_ptr> result;
            std::coroutine_handle<> continuation;
            bool detached = false;

            Task get_return_object() {
                return Task{handle_type::from_promise(*this)};
            }

            std::suspend_never initial_suspend() noexcept { return {}; }

            auto final_suspend() noexcept {
                struct FinalAwaiter {
                    bool await_ready() noexcept { return false; }
                    std::coroutine_handle<> await_suspend(handle_type h) noexcept {
                        if (h.promise().continuation)
                            return h.promise().continuation;
                        if (h.promise().detached) {
                            h.destroy();
                            return std::noop_coroutine();
                        }
                        return std::noop_coroutine();
                    }
                    void await_resume() noexcept {}
                };
                return FinalAwaiter{};
            }

            void return_value(T value) {
                result.template emplace<1>(std::move(value));
            }

            void unhandled_exception() {
                result.template emplace<2>(std::current_exception());
            }
        };

        bool await_ready() const noexcept { return handle.done(); }

        void await_suspend(std::coroutine_handle<> caller) noexcept {
            handle.promise().continuation = caller;
        }

        T await_resume() {
            std::variant<std::monostate, T, std::exception_ptr>& r = handle.promise().result;
            if (r.index() == 2) std::rethrow_exception(std::get<2>(r));
            return std::move(std::get<1>(r));
        }

        bool IsReady() const noexcept { return handle && handle.done(); }

        T Get() {
            std::variant<std::monostate, T, std::exception_ptr>& r = handle.promise().result;
            if (r.index() == 2) std::rethrow_exception(std::get<2>(r));
            return std::move(std::get<1>(r));
        }

        ~Task() {
            if (handle) {
                if (handle.done())
                    handle.destroy();
                else
                    handle.promise().detached = true;
            }
        }

        Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (handle) {
                    if (handle.done())
                        handle.destroy();
                    else
                        handle.promise().detached = true;
                }
                handle = std::exchange(other.handle, nullptr);
            }
            return *this;
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        // ---- Monadic interface ----

        template<typename F>
        auto Transform(F&& f) && {
            if constexpr (detail::is_result_v<T>) {
                using V = typename T::ValueType;
                using C = typename T::CodeType;
                if constexpr (std::is_void_v<V>) {
                    using U = std::invoke_result_t<F>;
                    return [](Task self, std::decay_t<F> fn) -> Task<Result<U, C>> {
                        auto r = co_await self;
                        if (r.IsErr()) co_return Result<U, C>::Err(std::move(r).GetError());
                        if constexpr (std::is_void_v<U>) {
                            fn();
                            co_return Result<U, C>::Ok();
                        } else {
                            co_return Result<U, C>::Ok(fn());
                        }
                    }(std::move(*this), std::forward<F>(f));
                } else {
                    using U = std::invoke_result_t<F, V&&>;
                    return [](Task self, std::decay_t<F> fn) -> Task<Result<U, C>> {
                        auto r = co_await self;
                        if (r.IsErr()) co_return Result<U, C>::Err(std::move(r).GetError());
                        if constexpr (std::is_void_v<U>) {
                            fn(std::move(r).GetValue());
                            co_return Result<U, C>::Ok();
                        } else {
                            co_return Result<U, C>::Ok(fn(std::move(r).GetValue()));
                        }
                    }(std::move(*this), std::forward<F>(f));
                }
            } else {
                using U = std::invoke_result_t<F, T&&>;
                return [](Task self, std::decay_t<F> fn) -> Task<U> {
                    if constexpr (std::is_void_v<U>) {
                        fn(co_await self);
                        co_return;
                    } else {
                        co_return fn(co_await self);
                    }
                }(std::move(*this), std::forward<F>(f));
            }
        }

        template<typename F>
        auto AndThen(F&& f) && {
            if constexpr (detail::is_result_v<T>) {
                using V = typename T::ValueType;
                using C = typename T::CodeType;
                if constexpr (std::is_void_v<V>) {
                    using InvokeResult = std::invoke_result_t<F>;
                    using InnerResult = detail::unwrap_task_t<InvokeResult>;
                    static_assert(detail::is_result_v<InnerResult>,
                        "AndThen: callable must return Result<U, C> or Task<Result<U, C>>");
                    static_assert(std::is_same_v<typename InnerResult::CodeType, C>,
                        "AndThen: callable must preserve the error code type");
                    return [](Task self, std::decay_t<F> fn) -> Task<InnerResult> {
                        auto r = co_await self;
                        if (r.IsErr()) co_return InnerResult::Err(std::move(r).GetError());
                        if constexpr (detail::is_task_v<InvokeResult>) {
                            co_return co_await fn();
                        } else {
                            co_return fn();
                        }
                    }(std::move(*this), std::forward<F>(f));
                } else {
                    using InvokeResult = std::invoke_result_t<F, V&&>;
                    using InnerResult = detail::unwrap_task_t<InvokeResult>;
                    static_assert(detail::is_result_v<InnerResult>,
                        "AndThen: callable must return Result<U, C> or Task<Result<U, C>>");
                    static_assert(std::is_same_v<typename InnerResult::CodeType, C>,
                        "AndThen: callable must preserve the error code type");
                    return [](Task self, std::decay_t<F> fn) -> Task<InnerResult> {
                        auto r = co_await self;
                        if (r.IsErr()) co_return InnerResult::Err(std::move(r).GetError());
                        if constexpr (detail::is_task_v<InvokeResult>) {
                            co_return co_await fn(std::move(r).GetValue());
                        } else {
                            co_return fn(std::move(r).GetValue());
                        }
                    }(std::move(*this), std::forward<F>(f));
                }
            } else {
                using InvokeResult = std::invoke_result_t<F, T&&>;
                static_assert(detail::is_task_v<InvokeResult>,
                    "AndThen: callable must return a Task<U>");
                using U = detail::unwrap_task_t<InvokeResult>;
                return [](Task self, std::decay_t<F> fn) -> Task<U> {
                    if constexpr (std::is_void_v<U>) {
                        co_await fn(co_await self);
                        co_return;
                    } else {
                        co_return co_await fn(co_await self);
                    }
                }(std::move(*this), std::forward<F>(f));
            }
        }

        template<typename F>
        auto OrElse(F&& f) && {
            static_assert(detail::is_result_v<T>,
                "OrElse is only available on Task<Result<V, C>>");
            using V = typename T::ValueType;
            using C = typename T::CodeType;
            using InvokeResult = std::invoke_result_t<F, Error<C>&&>;
            using RecoveredResult = detail::unwrap_task_t<InvokeResult>;
            static_assert(std::is_same_v<RecoveredResult, T>,
                "OrElse: callable must return Result<V, C> or Task<Result<V, C>> (same V, C)");
            return [](Task self, std::decay_t<F> fn) -> Task<T> {
                auto r = co_await self;
                if (r.IsOk()) co_return std::move(r);
                if constexpr (detail::is_task_v<InvokeResult>) {
                    co_return co_await fn(std::move(r).GetError());
                } else {
                    co_return fn(std::move(r).GetError());
                }
            }(std::move(*this), std::forward<F>(f));
        }

        template<typename F>
        auto TransformError(F&& f) && {
            static_assert(detail::is_result_v<T>,
                "TransformError is only available on Task<Result<V, C>>");
            using C = typename T::CodeType;
            static_assert(std::is_same_v<std::invoke_result_t<F, Error<C>&&>, Error<C>>,
                "TransformError: callable must return Error<C>");
            return [](Task self, std::decay_t<F> fn) -> Task<T> {
                auto r = co_await self;
                if (r.IsOk()) co_return std::move(r);
                co_return T::Err(fn(std::move(r).GetError()));
            }(std::move(*this), std::forward<F>(f));
        }

    private:
        explicit Task(handle_type h) : handle(h) {}
        handle_type handle;
    };

    template<>
    class Task<void> {
    public:
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;

        struct promise_type {
            std::exception_ptr exception;
            std::coroutine_handle<> continuation;
            bool detached = false;

            Task get_return_object() {
                return Task{handle_type::from_promise(*this)};
            }

            std::suspend_never initial_suspend() noexcept { return {}; }

            auto final_suspend() noexcept {
                struct FinalAwaiter {
                    bool await_ready() noexcept { return false; }
                    std::coroutine_handle<> await_suspend(handle_type h) noexcept {
                        if (h.promise().continuation)
                            return h.promise().continuation;
                        if (h.promise().detached) {
                            h.destroy();
                            return std::noop_coroutine();
                        }
                        return std::noop_coroutine();
                    }
                    void await_resume() noexcept {}
                };
                return FinalAwaiter{};
            }

            void return_void() {}

            void unhandled_exception() {
                exception = std::current_exception();
            }
        };

        bool await_ready() const noexcept { return handle.done(); }

        void await_suspend(std::coroutine_handle<> caller) noexcept {
            handle.promise().continuation = caller;
        }

        void await_resume() {
            if (handle.promise().exception)
                std::rethrow_exception(handle.promise().exception);
        }

        bool IsReady() const noexcept { return handle && handle.done(); }

        ~Task() {
            if (handle) {
                if (handle.done())
                    handle.destroy();
                else
                    handle.promise().detached = true;
            }
        }

        Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (handle) {
                    if (handle.done())
                        handle.destroy();
                    else
                        handle.promise().detached = true;
                }
                handle = std::exchange(other.handle, nullptr);
            }
            return *this;
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        // ---- Monadic interface (Task<void>) ----

        template<typename F>
        auto Transform(F&& f) && {
            using U = std::invoke_result_t<F>;
            return [](Task self, std::decay_t<F> fn) -> Task<U> {
                co_await self;
                if constexpr (std::is_void_v<U>) {
                    fn();
                    co_return;
                } else {
                    co_return fn();
                }
            }(std::move(*this), std::forward<F>(f));
        }

        template<typename F>
        auto AndThen(F&& f) && {
            using InvokeResult = std::invoke_result_t<F>;
            static_assert(detail::is_task_v<InvokeResult>,
                "AndThen: callable must return a Task<U>");
            using U = detail::unwrap_task_t<InvokeResult>;
            return [](Task self, std::decay_t<F> fn) -> Task<U> {
                co_await self;
                if constexpr (std::is_void_v<U>) {
                    co_await fn();
                    co_return;
                } else {
                    co_return co_await fn();
                }
            }(std::move(*this), std::forward<F>(f));
        }

    private:
        explicit Task(handle_type h) : handle(h) {}
        handle_type handle;
    };
} // namespace PhotonCommon
