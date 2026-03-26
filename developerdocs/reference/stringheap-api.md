# StringHeap API

Per-object networked string pool for efficient replication of string properties. All types live in the `SharedMode` namespace.

Header: `StringHeap.h`

---

## NetworkedStringHeap

Manages a heap of networked strings with allocation, deallocation, compaction, and dirty tracking for delta synchronization.

### Constructor

```cpp
NetworkedStringHeap(uint32_t size);
```

Create a heap with an initial capacity of `size` bytes. Pre-allocates entry and free-segment storage.

### Public Fields

| Field | Type | Description |
|-------|------|-------------|
| `entries` | `std::vector<Entry>` | Slot table for string entries. |
| `entryCount` | `uint32_t` | Number of live entries. |
| `free_by_offset` | `std::vector<FreeSeg>` | Free segments sorted by offset. |
| `freeSegmentCount` | `uint32_t` | Number of free segments. |
| `free_ids` | `std::set<uint32_t, std::greater<uint32_t>>` | Recycled entry IDs (lowest index at back for fast reuse). |
| `StringData` | `BufferT<PhotonCommon::CharType>` | The heap data buffer (current state). |
| `Shadow` | `BufferT<PhotonCommon::CharType>` | Previous-tick shadow for dirty detection. |
| `Ticks` | `BufferT<Tick>` | Per-byte tick stamps for delta encoding. |
| `HeapSize` | `uint32_t` | Current allocated heap capacity in bytes. |
| `SegmentInfos` | `std::vector<SegmentInfo>` | Debug segment map. |

### Public Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Resize` | `void Resize(uint32_t size)` | Grow the heap buffers (StringData, Shadow, Ticks) to the new size. |
| `allocate_string` | `StringHandle allocate_string(const PhotonCommon::CharType *str)` | Allocate a string in the heap and return its handle. Grows the heap if necessary. |
| `resolve_string` | `const PhotonCommon::CharType *resolve_string(const StringHandle &h, StringMessage &OutStatus)` | Look up a string by handle. Sets `OutStatus` to indicate validity. Returns a pointer to the string data or `nullptr`. |
| `free_handle` | `StringHandle free_handle(const StringHandle &h)` | Free the string at the given handle. Returns an invalidated handle. The freed space is added to the free list. |
| `is_valid_handle` | `bool is_valid_handle(const StringHandle &handle)` | Returns `true` if the handle points to a live entry with a matching generation. |
| `GetStringLength` | `uint32_t GetStringLength(const StringHandle &h)` | Returns the byte length of the string at the given handle. |
| `LogStringData` | `void LogStringData(const StringHandle &h)` | Print debug information about the entry and its data to the log. |
| `compact_heap` | `void compact_heap()` | Defragment the heap by moving live strings to fill gaps. |

---

## Entry

Slot descriptor for a single string in the heap.

```cpp
struct Entry {
    uint32_t offset = 0;       // Byte offset into StringData
    uint32_t size = 0;         // String length in bytes
    uint32_t generation = 0;   // Generation counter (incremented on reuse)
    bool alive = false;        // True if the entry is in use

    bool IsDirty = false;      // True if modified since last sync
    Tick ChangedTick;           // Tick when the entry was last modified
};
```

---

## FreeSeg

Describes a contiguous free region in the heap buffer.

```cpp
struct FreeSeg {
    uint32_t offset;   // Start offset in StringData
    uint32_t size;     // Size in bytes

    bool operator<(FreeSeg const &o) const;  // Compare by offset (for sorted storage)
};
```

---

## StringHandle

Opaque handle referencing a string in the heap. Validated by checking the generation against the entry.

```cpp
struct StringHandle {
    uint32_t id;          // Entry index in the entries table
    uint32_t generation;  // Expected generation (must match entry to be valid)
};
```

---

## StringMessage

Status codes returned by `resolve_string` via the `OutStatus` parameter.

```cpp
enum class StringMessage {
    Valid = 0,            // Handle is valid, string data returned
    NotALiveEntry = 1,    // Entry exists but is not alive
    WrongGeneration = 2,  // Handle generation does not match entry
    OutOfRange = 3,       // Handle ID is out of range
    WrongSize = 4,        // Entry size is inconsistent
    EmptyString = 5,      // String has zero length
    InvalidHandle = 6     // Handle is structurally invalid
};
```

---

## Constants

```cpp
static constexpr uint32_t HEAP_BUFFER_PADDING = 256;
```

Extra padding bytes added when growing the heap buffer to reduce frequent reallocations.
