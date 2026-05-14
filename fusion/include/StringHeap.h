// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <set>
#include "Buffers.h"
#include "StringType.h"

namespace FusionCore
{

    enum class StringMessage {
        Valid = 0,
        NotALiveEntry = 1,
        WrongGeneration = 2,
        OutOfRange = 3,
        WrongSize = 4,
        EmptyString = 5,
        InvalidHandle = 6,
        EmptyHeap = 7,
    };

    static constexpr uint32_t HEAP_BUFFER_PADDING = 256;

    struct StringHandle {
        uint32_t id;
        uint32_t generation;
    };

    struct Entry {
        uint32_t offset = 0;
        uint32_t size = 0;
        uint32_t generation = 0;
        bool alive = false;

        //Local state sync data.
        bool IsDirty = false;
        Tick ChangedTick;
    };

    struct FreeSeg {
        uint32_t offset;
        uint32_t size;

        bool operator<(FreeSeg const& o) const { return offset < o.offset; }
    };

    struct SegmentInfo {
        bool alive;
        uint32_t offset;
        uint32_t size;
    };

    class NetworkedStringHeap {

    public:
        std::vector<Entry> entries;
        uint32_t entryCount = 0;

        std::vector<FreeSeg> free_by_offset; //Sorted based on lowest offset
        uint32_t freeSegmentCount = 0;

        std::set<uint32_t, std::greater<uint32_t>> free_ids;  // Sorted based on index, lowest index at the end (the one we grab)

        BufferT<PhotonCommon::CharType> StringData{};
        BufferT<PhotonCommon::CharType> Shadow{};
        BufferT<Tick> Ticks{};

        uint32_t HeapSize = 0;

        std::vector<SegmentInfo> SegmentInfos;

        NetworkedStringHeap(uint32_t size = 0)
        {
            SegmentInfos.resize(256);
            entries.resize(128);
            free_by_offset.resize(128);

            if (size == 0) {
                return;
            }

            free_by_offset[0] = {0, size};
            freeSegmentCount = 1;

            Resize(size);
        }

        void Resize(uint32_t size);

        StringHandle allocate_string(const PhotonCommon::CharType* str);

        const PhotonCommon::CharType* resolve_string(const StringHandle& h, StringMessage& OutStatus);

        StringHandle free_handle(const StringHandle& h);

        bool is_valid_handle(const StringHandle& handle);

        uint32_t GetStringLength(const StringHandle& h);

        void LogStringData(const StringHandle& h);

        void compact_heap();

    private:
        uint64_t find_free_or_append(uint32_t size);

        void coalesce_free();

        void ensure_free_capacity();
    };
}

#include "SharedModeCompat.h"
