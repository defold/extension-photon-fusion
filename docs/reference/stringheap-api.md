# StringHeap API Reference

Networked string storage and replication for the Fusion SDK. All types live in the `SharedMode` namespace and are defined in `StringHeap.h`.

See also: [String Heap Concepts](../concepts/string-heap.md)

---

## Overview

Strings cannot be stored directly in the fixed-size Words buffer. Instead, each `ObjectRoot` owns a `NetworkedStringHeap` -- a variable-size arena that stores UTF-8 strings and replicates them alongside the object's state data.

Properties that need strings store a `StringHandle` (2 words: id + generation) in the Words buffer. The handle is an index into the heap. On the receiving side, the handle is resolved back to a string pointer via `resolve_string`.

String operations are accessed through `Object` (the base class shared by `ObjectRoot` and `ObjectChild`). Child objects delegate to their root's heap.

---

## StringHandle

A lightweight reference to a string stored in a `NetworkedStringHeap`. Occupies 2 words (8 bytes) in the Words buffer.

```cpp
struct StringHandle {
    uint32_t id;
    uint32_t generation;
};
```

### Fields

| Field | Type | Description |
|---|---|---|
| `id` | `uint32_t` | Slot index in the heap's entry table. `0` is reserved as the invalid/empty handle. |
| `generation` | `uint32_t` | Generation counter for the slot. Prevents stale handles from resolving after a slot is freed and reused. |

### Conventions

- A handle with `id == 0` represents an empty or unset string. It should not be passed to `resolve_string` or `free_handle`.
- Handles are stored as two consecutive `int32_t` words in the Words buffer (reinterpret-cast between `uint32_t` and `int32_t`).
- Handles are valid only within the scope of the `ObjectRoot` that owns the heap.

---

## StringMessage

Status codes returned by `resolve_string` to indicate success or the reason for failure.

```cpp
enum class StringMessage {
    Valid            = 0,
    NotALiveEntry    = 1,
    WrongGeneration  = 2,
    OutOfRange       = 3,
    WrongSize        = 4,
    EmptyString      = 5,
    InvalidHandle    = 6,
};
```

| Value | Description |
|---|---|
| `Valid` | The handle resolved successfully. The returned pointer is valid. |
| `NotALiveEntry` | The slot exists but has been freed (`alive == false`). |
| `WrongGeneration` | The slot is alive but the generation does not match. The handle is stale. |
| `OutOfRange` | The `id` exceeds the current entry table size. |
| `WrongSize` | Internal consistency error -- the recorded size is invalid. |
| `EmptyString` | The slot exists and is alive but contains a zero-length string. |
| `InvalidHandle` | The handle's `id` is `0` (the reserved invalid index). |

---

## Entry

Internal bookkeeping record for a single string slot. One `Entry` exists per allocated handle.

```cpp
struct Entry {
    uint32_t offset;
    uint32_t size;
    uint32_t generation;
    bool alive;
    bool IsDirty;
    Tick ChangedTick;
};
```

| Field | Type | Description |
|---|---|---|
| `offset` | `uint32_t` | Byte offset into `StringData` where this string begins. |
| `size` | `uint32_t` | Length of the string in bytes (excluding any null terminator). |
| `generation` | `uint32_t` | Current generation of this slot. Incremented on each free. |
| `alive` | `bool` | Whether this slot currently holds a valid string. |
| `IsDirty` | `bool` | Whether this entry has been modified since the last network sync. |
| `ChangedTick` | `Tick` | The tick at which this entry was last modified. Used for delta sync. |

---

## FreeSeg

Describes a contiguous free region within the string data buffer. Used by the heap's internal free-list allocator.

```cpp
struct FreeSeg {
    uint32_t offset;
    uint32_t size;

    bool operator<(FreeSeg const& o) const;
};
```

| Field | Type | Description |
|---|---|---|
| `offset` | `uint32_t` | Byte offset of the free region's start. |
| `size` | `uint32_t` | Size of the free region in bytes. |

Sorted by `offset` (ascending) for efficient coalescing of adjacent free segments.

---

## SegmentInfo

Diagnostic snapshot of a heap segment, used for debugging and inspection.

```cpp
struct SegmentInfo {
    bool alive;
    uint32_t offset;
    uint32_t size;
};
```

| Field | Type | Description |
|---|---|---|
| `alive` | `bool` | Whether this segment is allocated (`true`) or free (`false`). |
| `offset` | `uint32_t` | Byte offset in the string data buffer. |
| `size` | `uint32_t` | Size of the segment in bytes. |

---

## NetworkedStringHeap

The main string storage class. Each `ObjectRoot` owns one instance. Manages allocation, resolution, freeing, compaction, and network synchronization of strings.

### Constructor

```cpp
NetworkedStringHeap(uint32_t size);
```

Creates a heap with the given initial capacity in bytes. Initializes the entry table (128 slots), free segment list (128 slots), segment info array (256 slots), and a single free segment spanning the entire buffer.

**Parameter**: `size` -- Initial heap capacity in bytes. The `ObjectRoot` default is `1024`.

### Public Fields

| Field | Type | Description |
|---|---|---|
| `entries` | `std::vector<Entry>` | Slot table indexed by `StringHandle::id`. |
| `entryCount` | `uint32_t` | Number of slots that have ever been used (high-water mark). |
| `free_by_offset` | `std::vector<FreeSeg>` | Free segments sorted by offset (ascending). |
| `freeSegmentCount` | `uint32_t` | Number of active free segments. |
| `free_ids` | `std::set<uint32_t, std::greater<>>` | Pool of freed slot indices, ordered so the lowest index is grabbed first. |
| `StringData` | `BufferT<CharType>` | The main string data buffer. Contains all live and dead string bytes. |
| `Shadow` | `BufferT<CharType>` | Shadow copy for change detection during network sync. |
| `Ticks` | `BufferT<Tick>` | Per-byte tick stamps for delta synchronization. |
| `HeapSize` | `uint32_t` | Current allocated size of the data buffers. |
| `SegmentInfos` | `std::vector<SegmentInfo>` | Diagnostic segment map (pre-allocated to 256). |

### Constants

```cpp
static constexpr uint32_t HEAP_BUFFER_PADDING = 256;
```

Extra bytes added when resizing the heap to reduce allocation frequency.

### Methods

#### `Resize`

```cpp
void Resize(uint32_t size);
```

Resizes the `StringData`, `Shadow`, and `Ticks` buffers to the given size, preserving existing content. Updates `HeapSize`.

#### `allocate_string`

```cpp
StringHandle allocate_string(const CharType* str);
```

Allocates a new string in the heap.

1. Computes the byte length of `str`.
2. Finds or appends a free region large enough to hold the string.
3. Claims a slot (from the free ID pool, or allocates a new one).
4. Copies the string bytes into `StringData` at the allocated offset.
5. Marks the entry as alive and dirty.
6. Returns a `StringHandle` with the slot's `id` and current `generation`.

If the heap is too small, it is automatically resized (with `HEAP_BUFFER_PADDING` extra bytes).

#### `resolve_string`

```cpp
const CharType* resolve_string(const StringHandle& h, StringMessage& OutStatus);
```

Resolves a handle back to a string pointer.

- Sets `OutStatus` to indicate success or failure reason.
- On success (`StringMessage::Valid`), returns a pointer into `StringData` at the entry's offset.
- On failure, returns `nullptr`.

**Thread safety**: The returned pointer is valid only as long as no further allocations or compactions occur on the heap.

#### `free_handle`

```cpp
StringHandle free_handle(const StringHandle& h);
```

Frees the string associated with `h`.

1. Marks the entry as not alive.
2. Increments the entry's generation counter.
3. Returns the freed handle back to the free ID pool.
4. The string's data region is added to the free list.

Returns a `StringHandle` with `id = 0` as a convenience for clearing the caller's stored handle.

**Important**: Always free the old handle before allocating a new one when updating a string property. Failing to do so leaks heap space.

#### `GetStringLength`

```cpp
uint32_t GetStringLength(const StringHandle& h);
```

Returns the byte length of the string referenced by `h`. Does not validate the handle -- caller should check that the handle is valid first.

#### `LogStringData`

```cpp
void LogStringData(const StringHandle& h);
```

Writes diagnostic information about the string entry to the log output. Useful for debugging heap corruption or unexpected resolution failures.

#### `compact_heap`

```cpp
void compact_heap();
```

Defragments the heap by moving all live strings to the front of `StringData`, eliminating gaps left by freed strings. Updates all entry offsets accordingly. Coalesces the resulting free space into a single trailing segment.

This is an expensive operation and should be called infrequently (e.g., when the heap is nearly full and allocation fails).

---

## Object String Methods

The `Object` base class (shared by `ObjectRoot` and `ObjectChild`) provides convenience methods that delegate to the root's `NetworkedStringHeap`. For child objects, these methods traverse to the parent root automatically.

### `AddString`

```cpp
StringHandle Object::AddString(const CharType* str);
```

Allocates a string in the root's heap. Equivalent to `Root()->StringHeap.allocate_string(str)`.

### `ResolveString`

```cpp
const CharType* Object::ResolveString(const StringHandle& handle, StringMessage& OutStatus);
```

Resolves a handle from the root's heap. Equivalent to `Root()->StringHeap.resolve_string(handle, OutStatus)`.

### `FreeString`

```cpp
StringHandle Object::FreeString(const StringHandle& handle);
```

Frees a string in the root's heap. Equivalent to `Root()->StringHeap.free_handle(handle)`.

### `GetStringLength`

```cpp
uint32_t Object::GetStringLength(const StringHandle& handle);
```

Returns the byte length of a string. Equivalent to `Root()->StringHeap.GetStringLength(handle)`.

### `LogStringData`

```cpp
void Object::LogStringData(const StringHandle& handle);
```

Logs diagnostic info for a string. Equivalent to `Root()->StringHeap.LogStringData(handle)`.

---

## Lifecycle Example

```
1. Authority writes a string:
   handle = obj->AddString(u8"player_name");
   words[offset+0] = handle.id;
   words[offset+1] = handle.generation;

2. Fusion replicates Words + StringHeap data over the network.

3. Remote reads the string:
   StringHandle h = { words[offset+0], words[offset+1] };
   StringMessage status;
   const char8_t* str = obj->ResolveString(h, status);

4. Authority updates the string:
   obj->FreeString(old_handle);           // Free old string first
   new_handle = obj->AddString(u8"new_name");
   words[offset+0] = new_handle.id;
   words[offset+1] = new_handle.generation;
```
