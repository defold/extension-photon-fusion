// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoroutineCompat.h"
#include <exception>
#include <utility>
#include <variant>

namespace PhotonMatchmaking {
    template<typename T = void>
    class Task {
    public:
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;

        struct promise_type {
            std::variant<std::monostate, T, std::exception_ptr> result;
            std::coroutine_handle<> continuation;

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

        ~Task() { if (handle) handle.destroy(); }

        Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (handle) handle.destroy();
                handle = std::exchange(other.handle, nullptr);
            }
            return *this;
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

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

        ~Task() { if (handle) handle.destroy(); }

        Task(Task&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (handle) handle.destroy();
                handle = std::exchange(other.handle, nullptr);
            }
            return *this;
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

    private:
        explicit Task(handle_type h) : handle(h) {}
        handle_type handle;
    };
} // namespace PhotonMatchmaking
