// Copyright Exit Games GmbH. All Rights Reserved.

#pragma once

#if __has_include(<span>)
#include <span>
#else
#include <cstddef>
#include <cstdint>

namespace std {
    template <typename T>
    class span {
    public:
        constexpr span() noexcept : ptr_(nullptr), size_(0) {}
        constexpr span(T* ptr, size_t size) noexcept : ptr_(ptr), size_(size) {}

        template <typename Container>
        constexpr span(Container& c) noexcept : ptr_(c.data()), size_(c.size()) {}

        constexpr T* data() const noexcept { return ptr_; }
        constexpr size_t size() const noexcept { return size_; }
        constexpr bool empty() const noexcept { return size_ == 0; }
        constexpr T& operator[](size_t idx) const { return ptr_[idx]; }
        constexpr T* begin() const noexcept { return ptr_; }
        constexpr T* end() const noexcept { return ptr_ + size_; }

    private:
        T* ptr_;
        size_t size_;
    };
}
#endif
