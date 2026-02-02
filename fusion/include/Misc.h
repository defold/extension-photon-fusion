// Copyright Exit Games GmbH. All Rights Reserved.

#ifndef SHAREDCLIENT_MISC_C
#define SHAREDCLIENT_MISC_C

#include "json.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#include <cstdarg>
#include <memory>

using namespace std::chrono;

namespace SharedMode {
    using json = nlohmann::json;


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

        explicit Data(const char *ptr, const size_t length) {
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

    class Configuration {
        json _json{};

    public:
        void SetDouble(const std::string &name, double value);

        void SetString(const std::string &name, std::string value);

        void SetBool(const std::string &name, bool value);

        void SetObject(const std::string &name, const Configuration &config);

        double GetDouble(std::string name, double orDefault = 0) const;

        std::string GetString(std::string name, const std::string &orDefault) const;

        bool GetBool(std::string name, bool orDefault = false) const;

        void GetObject(const std::string &name, Configuration &config);
    };

    class TimerDelta {
        steady_clock::time_point _start;

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
        T *Head;
        T *Tail;
        int Count;

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
}

#endif
