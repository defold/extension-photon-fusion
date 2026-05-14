// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdarg>
#include <memory>
#include "SpanCompat.h"

#include "StringType.h"

using namespace std::chrono;

namespace FusionCore {

    std::string stringf(const char *format, ...);

    int64_t ClockQuantizeEncode(double clock);

    double ClockQuantizeDecode(int64_t clock);

    int64_t ZigZagEncode(int64_t i);

    int64_t ZigZagDecode(int64_t i);

    uint64_t CRC64(const void *data, size_t length);

    uint64_t CRC64(uint64_t crc, const void *data, size_t length);

    template<typename T, std::enable_if_t<!std::is_pointer_v<T>, bool>  = true>
    uint64_t CRC64(T data) { return CRC64(&data, sizeof(T)); }

    template<typename T, std::enable_if_t<!std::is_pointer_v<T>, bool>  = true>
    uint64_t CRC64(const uint64_t crc, T data) { return CRC64(crc, &data, sizeof(T)); }

    template<typename T, std::enable_if_t<!std::is_pointer_v<T>, bool>  = true>
    struct BufferT {
        T *Ptr{nullptr};
        size_t Length{0};

        bool IsValid() { return Ptr != nullptr && Length > 0; }

        void Init(const size_t length) {
            Ptr = new T[length]{};
            memset(Ptr, 0, length * sizeof(T));
            Length = length;
        }

        void Resize(const size_t length) {
            T *ptr = new T[length]{};

            //
            memset(ptr, 0, length * sizeof(T));

            if (Ptr != nullptr) {
                assert(Length > 0);

                //
                memcpy(ptr, Ptr, Length * sizeof(T));

                //
                delete[] Ptr;
            }

            Ptr = ptr;
            Length = length;
        }

        ~BufferT() {
            delete[] Ptr;
        }

        operator T *() const { return Ptr; }
        operator void *() const { return Ptr; }
    };

    struct Data {
        uint8_t *Ptr{nullptr};
        size_t Length{0};

        bool Valid() const { return Ptr != nullptr && Length > 0; }

        Data() = default;

        explicit Data(const size_t length) {
            Resize(length);
        }

        explicit Data(const PhotonCommon::CharType* ptr, const size_t length) {
            if (ptr != nullptr) {
                Ptr = new uint8_t[length]{};
                Length = length;
                memcpy(Ptr, ptr, Length);
            } else {
                assert(length == 0);
                Ptr = nullptr;
                Length = 0;
            }
        }

        explicit Data(uint8_t *ptr, const size_t length) {
            Ptr = ptr;
            Length = length;
        }

        operator std::span<const uint8_t>() const {
            return {Ptr, Length};
        }

        operator std::span<uint8_t>() const {
            return {Ptr, Length};
        }

        uint8_t* data() const noexcept { return Ptr; }
        size_t size() const noexcept { return Length; }

        Data Clone() const {
            Data copy{};
            copy.Length = Length;
            copy.Ptr = new uint8_t[Length];

            memcpy(copy.Ptr, Ptr, Length);

            return copy;
        }

        void Free() {
            delete[] Ptr;
            Ptr = nullptr;
            Length = 0;
        }

        void Resize(const size_t length) {
            const auto ptr = new uint8_t[length]{};

            if (Ptr != nullptr) {
                assert(Length > 0);

                //
                memcpy(ptr, Ptr, Length);

                //
                delete[] Ptr;
            }

            Ptr = ptr;
            Length = length;
        }

        Data Slice(const size_t offset) const {
            Data slice = *this;
            slice.Ptr += offset;
            slice.Length -= offset;
            assert(slice.Length <= this->Length);
            return slice;
        }

        Data CloneSlice(const size_t offset) const {
            Data slice = *this;
            slice.Ptr += offset;
            slice.Length -= offset;
            assert(slice.Length <= this->Length);
            return slice.Clone();
        }

        operator bool() const {
            return Ptr != nullptr && Length > 0;
        }
    };

    class TimerDelta {
        steady_clock::time_point _start{};

    public:
        void Start();

        bool Running() const;

        double Peek() const;

        double Consume();

        static TimerDelta StartNew();
    };

    class Timer {
        steady_clock::time_point _start;

    public:
        void Start();

        bool Running() const;

        double ElapsedSeconds() const;
    };

    template<typename T>
    struct LinkList {
        T *Head{nullptr};
        T *Tail{nullptr};
        int Count{0};

        void AddFirst(T *item) {
            if (Head != nullptr) {
                item->Next = Head;
                Head->Prev = item;
            } else {
                Tail = item;
            }

            Head = item;
            ++Count;
        }

        void AddLast(T *item) {
            if (Tail != nullptr) {
                item->Prev = Tail;
                Tail->Next = item;
            } else {
                Head = item;
            }

            Tail = item;
            ++Count;
        }

        void AddBefore(T *item, T *before) {
            if (before == Head) {
                AddFirst(item);
            } else {
                item->Next = before;
                item->Prev = before->Prev;

                before->Prev->Next = item;
                before->Prev = item;

                ++Count;
            }
        }


        void AddAfter(T *item, T *after) {
            if (after == Tail) {
                AddLast(item);
            } else {
                item->Next = after->Next;
                item->Prev = after;

                after->Next->Prev = item;
                after->Next = item;
                ++Count;
            }
        }


        bool TryRemoveFirst(T *&result) {
            if (Head == nullptr) {
                result = nullptr;
                return false;
            }

            result = RemoveFirst();
            return true;
        }

        bool TryRemoveLast(T *&result) {
            if (Tail == nullptr) {
                result = nullptr;
                return false;
            }

            result = RemoveLast();
            return true;
        }

        bool TryPeekFirst(T *&result) {
            result = Head;
            return Head != nullptr;
        }


        T *RemoveFirst() {
            T *head = Head;
            Remove(head);
            return head;
        }

        T *RemoveLast() {
            T *tail = Tail;
            Remove(tail);
            return tail;
        }


        bool Remove(T *item) {
            if (item == nullptr) {
                return false;
            }

            if (item->Prev == nullptr) {
                Head = item->Next;
            } else {
                item->Prev->Next = item->Next;
            }

            if (item->Next == nullptr) {
                Tail = item->Prev;
            } else {
                item->Next->Prev = item->Prev;
            }

            item->Prev = nullptr;
            item->Next = nullptr;

            --Count;

            return true;
        }
    };

    
    // Quaternion compression: 4 floats/doubles (16/32 bytes) -> 1 uint32 (4 bytes)
    // Uses smallest-three encoding with 10 bits per component + 2-bit largest axis index.
    // Matches the C# server implementation in Maths.cs exactly.

    namespace detail {
        constexpr int32_t pow10table[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};
        constexpr int32_t pow10(int n) { return pow10table[n]; }
    }

    template<typename T>
    inline int32_t FloatQuantize(T value, int decimals) {
        static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>, "FloatQuantize requires float or double");
        const auto scale = detail::pow10(decimals);
        return static_cast<int32_t>(static_cast<double>(value) * scale + (value >= 0 ? 0.5 : -0.5));
    }

    template<typename T>
    inline T FloatDequantize(int32_t value, int decimals) {
        static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>, "FloatDequantize requires float or double");
        return static_cast<T>(static_cast<double>(value) / detail::pow10(decimals));
    }

template<typename T>
    inline uint32_t QuaternionCompress(T x, T y, T z, T w) {
        static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>, "QuaternionCompress requires float or double");
        constexpr float UNRANGE = 0.70710678118654752440084436210485f + 0.0000001f;
        constexpr float ENRANGE = 1.0f / UNRANGE;
        constexpr uint32_t HALF_ENCODED = (1 << 10) / 2;
        constexpr float ENCODER = ENRANGE * HALF_ENCODED;

        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float fz = static_cast<float>(z);
        const float fw = static_cast<float>(w);

        const float absx = fx < 0 ? -fx : fx;
        const float absy = fy < 0 ? -fy : fy;
        const float absz = fz < 0 ? -fz : fz;
        const float absw = fw < 0 ? -fw : fw;

        const int biggestOfXY = (absx > absy) ? 0 : 1;
        const int biggestOfZW = (absz > absw) ? 2 : 3;
        const int biggestAxis = ((biggestOfXY == 0) ? absx : absy) > ((biggestOfZW == 2) ? absz : absw) ? biggestOfXY : biggestOfZW;

        float a, b, c, d;
        switch (biggestAxis) {
            case 0: a = fy; b = fz; c = fw; d = fx; break;
            case 1: a = fx; b = fz; c = fw; d = fy; break;
            case 2: a = fx; b = fy; c = fw; d = fz; break;
            default: a = fx; b = fy; c = fz; d = fw; break;
        }

        if (d < 0) { a = -a; b = -b; c = -c; }

        return
            static_cast<uint32_t>(a * ENCODER + HALF_ENCODED) |
            static_cast<uint32_t>(b * ENCODER + HALF_ENCODED) << 10 |
            static_cast<uint32_t>(c * ENCODER + HALF_ENCODED) << 20 |
            static_cast<uint32_t>(biggestAxis) << 30;
    }

    template<typename T>
    inline void QuaternionDecompress(uint32_t buffer, T &outX, T &outY, T &outZ, T &outW) {
        static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>, "QuaternionDecompress requires float or double");
        constexpr float UNRANGE = 0.70710678118654752440084436210485f + 0.0000001f;
        constexpr uint32_t HALF_ENCODED = (1 << 10) / 2;
        constexpr uint32_t MASK10BITS = 0x3FF;
        constexpr float DECODER = (1.0f / HALF_ENCODED) * UNRANGE;

        const int acomp = static_cast<int>(buffer & MASK10BITS);
        const int bcomp = static_cast<int>((buffer >> 10) & MASK10BITS);
        const int ccomp = static_cast<int>((buffer >> 20) & MASK10BITS);
        const int biggestAxis = static_cast<int>(buffer >> 30);

        const float a = (acomp - static_cast<int>(HALF_ENCODED)) * DECODER;
        const float b = (bcomp - static_cast<int>(HALF_ENCODED)) * DECODER;
        const float c = (ccomp - static_cast<int>(HALF_ENCODED)) * DECODER;
        const float d = static_cast<float>(std::sqrt(1.0 - (a * a + b * b + c * c)));

        switch (biggestAxis) {
            case 0: outX = static_cast<T>(d); outY = static_cast<T>(a); outZ = static_cast<T>(b); outW = static_cast<T>(c); break;
            case 1: outX = static_cast<T>(a); outY = static_cast<T>(d); outZ = static_cast<T>(b); outW = static_cast<T>(c); break;
            case 2: outX = static_cast<T>(a); outY = static_cast<T>(b); outZ = static_cast<T>(d); outW = static_cast<T>(c); break;
            default: outX = static_cast<T>(a); outY = static_cast<T>(b); outZ = static_cast<T>(c); outW = static_cast<T>(d); break;
        }
    }
}

#include "SharedModeCompat.h"
