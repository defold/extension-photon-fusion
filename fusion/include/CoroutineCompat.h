// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#if __has_include(<coroutine>)
    #include <coroutine>

#elif defined(__clang__)
    #include <cstddef>
    namespace std {
        template<typename Promise = void>
        struct coroutine_handle;

        template<>
        struct coroutine_handle<void> {
            coroutine_handle() noexcept = default;
            coroutine_handle(std::nullptr_t) noexcept : ptr(nullptr) {}
            coroutine_handle& operator=(std::nullptr_t) noexcept { ptr = nullptr; return *this; }
            void* address() const noexcept { return ptr; }
            static coroutine_handle from_address(void* addr) noexcept { coroutine_handle h; h.ptr = addr; return h; }
            explicit operator bool() const noexcept { return ptr != nullptr; }
            bool operator==(std::nullptr_t) const noexcept { return ptr == nullptr; }
            bool operator!=(std::nullptr_t) const noexcept { return ptr != nullptr; }
            void resume() const { __builtin_coro_resume(ptr); }
            void destroy() const { __builtin_coro_destroy(ptr); }
            bool done() const { return __builtin_coro_done(ptr); }
            void operator()() const { resume(); }
        protected:
            void* ptr = nullptr;
        };

        template<typename Promise>
        struct coroutine_handle : coroutine_handle<void> {
            using coroutine_handle<void>::coroutine_handle;
            static coroutine_handle from_address(void* addr) noexcept {
                coroutine_handle h; h.ptr = addr; return h;
            }
            static coroutine_handle from_promise(Promise& p) noexcept {
                coroutine_handle h;
                h.ptr = __builtin_coro_promise((char*)&p, alignof(Promise), true);
                return h;
            }
            Promise& promise() const {
                return *static_cast<Promise*>(__builtin_coro_promise(ptr, alignof(Promise), false));
            }
        };

        struct suspend_never {
            bool await_ready() const noexcept { return true; }
            void await_suspend(coroutine_handle<>) const noexcept {}
            void await_resume() const noexcept {}
        };

        struct suspend_always {
            bool await_ready() const noexcept { return false; }
            void await_suspend(coroutine_handle<>) const noexcept {}
            void await_resume() const noexcept {}
        };

        struct noop_coroutine_promise {};
        using noop_coroutine_handle = coroutine_handle<noop_coroutine_promise>;

    #if __has_builtin(__builtin_coro_noop)
        inline noop_coroutine_handle noop_coroutine() noexcept {
            return noop_coroutine_handle::from_address(__builtin_coro_noop());
        }
    #else
        namespace _detail {
            struct _noop_frame {
                static void _noop(void*) noexcept {}
                void (*resume_fn)(void*) = &_noop;
                void (*destroy_fn)(void*) = &_noop;
            };
            inline _noop_frame _global_noop_frame{};
        }
        inline noop_coroutine_handle noop_coroutine() noexcept {
            return noop_coroutine_handle::from_address(&_detail::_global_noop_frame);
        }
    #endif

        template<typename R, typename... Args>
        struct coroutine_traits {
            using promise_type = typename R::promise_type;
        };
    }

#elif __has_include(<experimental/coroutine>)
    #include <experimental/coroutine>
    namespace std {
        using std::experimental::coroutine_handle;
        using std::experimental::coroutine_traits;
        using std::experimental::suspend_never;
        using std::experimental::suspend_always;
        using std::experimental::noop_coroutine;
    }

#else
    #error "No coroutine support available. Requires <coroutine>, <experimental/coroutine>, or Clang __builtin_coro_* intrinsics."
#endif
