# String Heap

Strings cannot be stored directly in the [Words buffer](objects.md) because they are variable-length. Instead, Fusion provides `NetworkedStringHeap`, a per-object heap that stores string data and exposes fixed-size handles for use in the Words buffer.

## Overview

Each `Object` (both `ObjectRoot` and `ObjectChild`) contains a `NetworkedStringHeap` initialized with 1024 bytes of storage:

```cpp
class Object {
    NetworkedStringHeap StringHeap{1024};
    // ...
};
```

Sub-objects (`ObjectChild`) delegate string operations to their root's heap via `Root()->StringHeap` when the sub-object's own heap is not sufficient. The root heap is the authoritative storage for the object hierarchy.

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
| `id` | Index into the heap's entry table. `0` is the invalid/empty handle. |
| `generation` | Incremented each time a slot is reused. Stale handles (wrong generation) are rejected. |

In the Words buffer, a StringHandle occupies exactly 2 words at the property's offset:

```
words[offset]     = handle.id
words[offset + 1] = handle.generation
```

## Lifecycle

### Allocate

```cpp
StringHandle Object::AddString(const PhotonCommon::CharType* str);
```

Allocates heap storage, copies the UTF-8 string data, and returns a handle. The handle's `id` and `generation` are then written into the Words buffer by the integration layer.

For empty strings, do not call `AddString`. Instead, write `id = 0` directly. The SDK treats `id == 0` as an invalid handle that resolves to an empty string.

### Resolve

```cpp
const PhotonCommon::CharType* Object::ResolveString(
    const StringHandle& handle,
    StringMessage& OutStatus
);
```

Looks up the string data for the given handle. Returns a pointer to the stored UTF-8 data and sets `OutStatus` to indicate success or failure.

### Free

```cpp
StringHandle Object::FreeString(const StringHandle& handle);
```

Releases the heap storage associated with the handle. Returns an invalidated handle (id = 0). The freed slot's generation is incremented, so any stale handles pointing to it will fail validation.

**You must call `FreeString` before allocating a replacement string for the same property.** Failing to do so leaks heap space.

### Validation

```cpp
bool Object::IsValidStringHandle(const StringHandle& handle);
```

Returns `true` if the handle points to a live entry with a matching generation.

### Helpers

```cpp
uint32_t Object::GetStringLength(const StringHandle& handle);
void Object::LogStringData(const StringHandle& handle);
```

## StringMessage Status Codes

`ResolveString` reports a status code indicating the result:

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

Only `Valid` returns usable string data. All other statuses should be treated as "no string available."

## Heap Internals

### Entry Table

The heap maintains a table of `Entry` structs:

```cpp
struct Entry {
    uint32_t offset;       // Byte offset into StringData buffer
    uint32_t size;         // String length in bytes
    uint32_t generation;   // Current generation for this slot
    bool     alive;        // Whether this slot is in use

    bool     IsDirty;      // Needs replication
    Tick     ChangedTick;  // When the entry was last modified
};
```

### Memory Layout

```
StringData buffer:
+--------+--------+-----------+--------+--------+
| str_0  | str_1  |  (freed)  | str_3  | (free) |
+--------+--------+-----------+--------+--------+
                   ^-- tracked by free_by_offset
```

### Free List

Freed slots are tracked in `free_ids` (sorted set with lowest index preferred for reuse). Freed heap segments are tracked in `free_by_offset` and coalesced to reduce fragmentation:

```cpp
struct FreeSeg {
    uint32_t offset;
    uint32_t size;
};
```

### Compaction

`compact_heap()` defragments the string data buffer by moving live entries to fill gaps left by freed strings. Called internally when fragmentation reaches a threshold.

### Auto-Resize

The heap starts at 1024 bytes and grows automatically when allocation cannot find a contiguous free segment. The constant `HEAP_BUFFER_PADDING` (256 bytes) provides a growth margin.

### Replication

The heap has its own replication path separate from the Words buffer. The SDK tracks dirty entries via `IsDirty` and `ChangedTick`, and includes string heap data in state packets only when entries or data have changed:

```cpp
constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_ENTRIES_CHANGE = 2;
constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_DATA_CHANGE    = 4;
```

The heap also maintains its own `Shadow` and `Ticks` buffers for change detection, analogous to the Words buffer's shadow.

## Spawn Data Exception

During object creation, strings in spawn data (the `header` blob) must be serialized as raw bytes rather than as StringHandle references. The heap is available immediately after object allocation, but any strings written to Words during the initial population (before `SetHasValidData(true)`) follow the normal handle flow.

The distinction: the `Header` field is opaque bytes that the SDK passes through without interpreting. If your spawn data includes strings, encode them as length-prefixed UTF-8 in the header, not as StringHandles.

## Sub-Object Delegation

Sub-objects (`ObjectChild`) have their own `StringHeap` instance, but string operations called on an `ObjectChild` may delegate to the root's heap depending on the operation. The key behavior:

- `AddString` / `FreeString` / `ResolveString` operate on the object's own heap
- The root heap is the primary heap for replication purposes
- All string handles within a hierarchy must track which object's heap they belong to

## Handle Tracking for Leak Prevention

Every `AddString` call must eventually be paired with a `FreeString` call, either when the property value changes or when the object is destroyed. Leaked handles waste heap space and can eventually exhaust the heap.

A recommended pattern:

```cpp
// Track handles per property
struct StringProperty {
    StringHandle handle{0, 0};
    int32_t wordOffset;

    void Set(Object* obj, const PhotonCommon::CharType* str) {
        // Free old handle
        if (handle.id != 0) {
            obj->FreeString(handle);
        }

        // Allocate new
        if (str && str[0] != 0) {
            handle = obj->AddString(str);
        } else {
            handle = {0, 0};
        }

        // Write to Words
        obj->Words.Ptr[wordOffset]     = handle.id;
        obj->Words.Ptr[wordOffset + 1] = handle.generation;
    }

    void Free(Object* obj) {
        if (handle.id != 0) {
            obj->FreeString(handle);
            handle = {0, 0};
        }
    }
};
```

## Usage Pattern

### Authority Side (Writing)

```cpp
// Read the current handle from Words
StringHandle oldHandle;
oldHandle.id         = words[offset];
oldHandle.generation = words[offset + 1];

// Free the old string (if valid)
if (oldHandle.id != 0) {
    obj->FreeString(oldHandle);
}

// Allocate new string
StringHandle newHandle = obj->AddString(PHOTON_STR("Hello, world!"));

// Write new handle to Words
words[offset]     = newHandle.id;
words[offset + 1] = newHandle.generation;
```

### Remote Side (Reading)

```cpp
// Read handle from Words
StringHandle handle;
handle.id         = words[offset];
handle.generation = words[offset + 1];

if (handle.id == 0) {
    // Empty string
} else {
    StringMessage status;
    const PhotonCommon::CharType* str = obj->ResolveString(handle, status);
    if (status == StringMessage::Valid && str) {
        // Use str (UTF-8 encoded)
    }
}
```

## Related

- [Serialization](serialization.md) -- Words buffer layout and type mapping
- [Objects](objects.md) -- Object hierarchy (root vs. child)
- [Object Creation](object-creation.md) -- Spawn data exception
