# String Heap

Strings cannot be stored directly in the [Words buffer](objects.md) because they are variable-length. Instead, Fusion provides `NetworkedStringHeap`, a per-root-object heap that stores string data and exposes fixed-size handles for use in the Words buffer.

## Overview

Each `ObjectRoot` contains a `NetworkedStringHeap` initialized with 1024 bytes of storage:

```cpp
class ObjectRoot final : public Object {
    NetworkedStringHeap StringHeap{1024};
    // ...
};
```

Sub-objects (`ObjectChild`) do not have their own heap. String operations on a child delegate to its root's heap via `Root()->StringHeap`.

## StringHandle

A `StringHandle` is a fixed-size reference (2 words / 8 bytes) stored in the Words buffer:

```cpp
struct StringHandle {
    uint32_t id;          // Slot index in the heap
    uint32_t generation;  // Generation counter for use-after-free detection
};
```

| Field | Description |
|-------|-------------|
| `id` | Index into the heap's entry table. `0` is reserved as the invalid/empty handle. |
| `generation` | Incremented each time a slot is reused. Stale handles (wrong generation) are rejected. |

In the Words buffer, a StringHandle occupies exactly 2 words at the property's offset:

```
words[offset]     = handle.id
words[offset + 1] = handle.generation
```

## Lifecycle

### Allocate

```cpp
StringHandle Object::AddString(const CharType* str);
```

Allocates heap storage, copies the string data, and returns a handle. The handle's `id` and `generation` are written into the Words buffer by the integration layer.

For empty strings, do not call `AddString`. Instead, write `id = 0` directly. The SDK treats `id == 0` as an invalid handle that resolves to an empty string.

### Resolve

```cpp
const CharType* Object::ResolveString(const StringHandle& handle, StringMessage& OutStatus);
```

Looks up the string data for the given handle. Returns a pointer to the stored UTF-8 data and sets `OutStatus` to indicate success or failure.

### Free

```cpp
StringHandle Object::FreeString(const StringHandle& handle);
```

Releases the heap storage associated with the handle. Returns an invalidated handle (id = 0). The freed slot's generation is incremented, so any stale handles pointing to it will fail validation.

**You must call `FreeString` before allocating a replacement string for the same property.** Failing to do so leaks heap space.

### Helper

```cpp
uint32_t Object::GetStringLength(const StringHandle& handle);
void Object::LogStringData(const StringHandle& handle);
```

## StringMessage Status Codes

`ResolveString` returns a status code indicating the result:

```cpp
enum class StringMessage {
    Valid          = 0,   // String resolved successfully
    NotALiveEntry  = 1,   // Slot has been freed
    WrongGeneration = 2,  // Handle is stale (slot was reused)
    OutOfRange     = 3,   // Handle id exceeds entry table bounds
    WrongSize      = 4,   // Internal size mismatch
    EmptyString    = 5,   // Slot exists but contains empty data
    InvalidHandle  = 6,   // Handle id is 0 (null handle)
};
```

The only status that returns valid string data is `Valid`. All other statuses should be treated as "no string available" and the property should resolve to an empty string.

## Heap Internals

### Entry Table

The heap maintains a table of `Entry` structs:

```cpp
struct Entry {
    uint32_t offset;       // Byte offset into StringData buffer
    uint32_t size;         // String length in bytes (excluding null terminator)
    uint32_t generation;   // Current generation for this slot
    bool     alive;        // Whether this slot is in use

    bool     IsDirty;      // Needs replication
    Tick     ChangedTick;  // When the entry was last modified
};
```

### Free List

Freed slots are tracked in `free_ids` (sorted set, lowest index preferred for reuse). Freed heap segments are tracked in `free_by_offset` and coalesced to reduce fragmentation:

```cpp
struct FreeSeg {
    uint32_t offset;
    uint32_t size;
};
```

### Compaction

`compact_heap()` defragments the string data buffer by moving live entries to fill gaps left by freed strings. This is called internally when fragmentation reaches a threshold.

### Auto-Resize

The heap starts at 1024 bytes and grows automatically when allocation cannot find a contiguous free segment. Growth is managed via `Resize()`. The constant `HEAP_BUFFER_PADDING` (256 bytes) provides a growth margin.

### Replication

The heap has its own replication channel separate from the Words buffer. The SDK tracks dirty entries via `IsDirty` and `ChangedTick`, and includes string heap data in state packets only when entries or data have changed. The packet flags indicate what changed:

```cpp
constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_ENTRIES_CHANGE = 2;
constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_DATA_CHANGE    = 4;
```

The heap also maintains its own `Shadow` buffer for change detection, analogous to the Words buffer's shadow.

## Spawn Data Exception

During object creation (spawn data), the `NetworkedStringHeap` does not yet exist because the `ObjectRoot` has not been created. Strings in spawn data must be serialized as raw bytes in the header blob rather than as StringHandle references. Once the object is created and the heap is available, subsequent string updates use the normal handle mechanism.

## Usage Pattern

A typical string property update cycle:

```cpp
// Read the current handle from Words
StringHandle oldHandle;
oldHandle.id         = words[offset];
oldHandle.generation = words[offset + 1];

// Free the old string (if it was valid)
if (oldHandle.id != 0) {
    obj->FreeString(oldHandle);
}

// Allocate new string
StringHandle newHandle = obj->AddString(u8"Hello, world!");

// Write new handle to Words
words[offset]     = newHandle.id;
words[offset + 1] = newHandle.generation;
```

On the receiving side:

```cpp
// Read handle from Words
StringHandle handle;
handle.id         = words[offset];
handle.generation = words[offset + 1];

if (handle.id == 0) {
    // Empty string
} else {
    StringMessage status;
    const CharType* str = obj->ResolveString(handle, status);
    if (status == StringMessage::Valid && str) {
        // Use str (UTF-8 encoded)
    }
}
```

## See Also

- [Serialization](serialization.md) -- Words buffer layout and type mapping
- [Objects](objects.md) -- Object hierarchy (root vs. child heap delegation)
- [Object Creation](object-creation.md) -- Spawn data exception
- [StringHeap API Reference](../reference/stringheap-api.md) -- Complete API reference
