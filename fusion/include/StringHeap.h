//
// Created by Linus on 2025-11-21.
//

#ifndef STRINGHEAP_H
#define STRINGHEAP_H

#include <cstdint>
#include <vector>
#include <optional>
#include <set>
#include "Buffers.h"

namespace SharedMode
{
    static constexpr const wchar_t* ERR_NOT_ALIVE_ENTRY   = L"\xFFFF ERR_NOT_ALIVE_ENTRY";
    static constexpr const wchar_t* ERR_WRONG_GENERATION   = L"\xFFFE ERR_WRONG_GENERATION";
    static constexpr const wchar_t* ERR_OUT_OF_RANGE = L"\xFFFD ERR_OUT_OF_RANGE";
    static constexpr const wchar_t* ERR_WRONG_SIZE = L"\xFFFC ERR_WRONG_SIZE";

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

        BufferT<uint16_t> StringData{};
        BufferT<uint16_t> Shadow{};
        BufferT<Tick> Ticks{};

        uint32_t HeapSize = 0;

        std::vector<SegmentInfo> SegmentInfos;

        NetworkedStringHeap(uint32_t size)
        {
            SegmentInfos.resize(256);

            entries.resize(128);
            free_by_offset.resize(128);

            free_by_offset[0] = {0, size};
            freeSegmentCount = 1;

            Resize(size);
        }

        void Resize(uint32_t size);

        StringHandle allocate_string(const wchar_t* str);

        const wchar_t* resolve_string(const StringHandle &h);

        StringHandle free_handle(const StringHandle &h);

        void compact_heap();

    private:
        uint64_t find_free_or_append(uint32_t size);

        void coalesce_free();

        std::vector<wchar_t> conversionBuffer; // Used to hold conversions between utf16 and utf32 (wchar_t on none windows platforms)
    };
}


#endif //STRINGHEAP_H
