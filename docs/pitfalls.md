# Pitfalls

Critical gotchas that every Fusion 3 SDK integration must handle. Each pitfall has caused real bugs in production integrations.

## 1. Writing into the ObjectTail

The last 6 words (`sizeof(ObjectTail) / 4`) of every object's Words buffer are reserved for internal use. The `ObjectTail` contains AOI coordinates and the `Destroyed` flag. Writing user data into this region corrupts the object's internal state.

```cpp
// ObjectTail layout (24 bytes = 6 words at the end of Words)
struct ObjectTail {
    int32_t AOI_X;
    int32_t AOI_Y;
    int32_t AOI_Z;
    int32_t AOI_SET;
    int32_t Destroyed;   // SDK sets this to signal destruction
    int32_t Dummy;
};
```

**Rule**: When allocating an object, request `user_words + Object::ExtraTailWords` total words. Only write to indices `0` through `user_words - 1`.

```cpp
size_t total_words = user_word_count + SharedMode::Object::ExtraTailWords;
auto* obj = client->CreateObject(total_words, type, header, headerLen, scene, flags);
// Safe range: obj->Words[0] to obj->Words[user_word_count - 1]
// DANGER:     obj->Words[user_word_count] onwards is ObjectTail
```

## 2. Words Buffer Iteration Order Mismatch

Properties in the Words buffer are serialized sequentially at fixed offsets with no type markers or length prefixes. The iteration order during `sync_outbound()` (writing) must exactly match `sync_inbound()` (reading) and `serialize_spawn_data()`.

If you add, remove, or reorder properties and the sender/receiver disagree on layout, every property after the change point reads garbage data silently — there are no runtime checks.

**Rule**: Define the property layout once (e.g., in a configuration resource) and iterate it identically in all serialization paths.

## 3. SDK Header Inclusion Order

The Fusion SDK defines a `Dictionary` type that collides with engine types (e.g., `godot::Dictionary`). SDK headers must be included **before** engine headers in every translation unit.

```cpp
// CORRECT: SDK headers first
#include "Client.h"     // Fusion SDK
#include "Types.h"      // Fusion SDK
#include "my_class.h"   // Engine code
#include <godot_cpp/classes/node.hpp>  // Engine

// WRONG: Engine headers first — Dictionary name collision
#include <godot_cpp/classes/node.hpp>  // Engine
#include "Client.h"     // Fusion SDK — COMPILE ERROR
```

**Rule**: Every `.cpp` file that uses both Fusion SDK and engine types must include SDK headers before engine headers.

## 4. String Handle Leaks

`NetworkedStringHeap` is a fixed-size heap. Every call to `Object::AddString()` allocates space. If you update a string property without first calling `Object::FreeString()` on the old handle, the old allocation is never reclaimed.

```cpp
// WRONG: Leaks the previous string
StringHandle handle = obj->AddString(new_value);
words[offset] = handle.id;
words[offset + 1] = handle.generation;

// CORRECT: Free old handle before allocating new
StringHandle old_handle{words[offset], words[offset + 1]};
if (old_handle.id != 0) {
    obj->FreeString(old_handle);
}
StringHandle new_handle = obj->AddString(new_value);
words[offset] = new_handle.id;
words[offset + 1] = new_handle.generation;
```

**Rule**: Track the active `StringHandle` for each string property. Free the old handle before allocating a new one. Use `id == 0` to detect uninitialized/empty handles.

## 5. Spawn Data Cannot Use StringHeap

When an object is first created, its `NetworkedStringHeap` does not exist yet on remote clients. Spawn data (the `Header` field passed to `CreateObject` / `CreateSceneObject`) is serialized into a raw byte buffer, not into the Words buffer.

If you write `StringHandle` values into spawn data, remote clients will receive handle IDs that reference a heap that hasn't been populated yet, resulting in `StringMessage::NotALiveEntry` or `StringMessage::OutOfRange` errors.

**Rule**: For initial string values, serialize them directly into the spawn data header as raw bytes (length-prefixed or null-terminated). Only use `AddString()` / `StringHandle` for ongoing state sync after the object is fully created.

## 6. Frame Loop Order

The frame loop must execute in this exact order every frame:

```
Photon.Service()  →  Client.UpdateFrameEnd()  →  Client.UpdateFrameBegin(dt)
```

- `Service()` — processes network I/O (send queued packets, receive incoming packets)
- `UpdateFrameEnd()` — finishes the current frame (queues outbound state/RPCs)
- `UpdateFrameBegin(dt)` — starts the next frame (applies received state, fires callbacks)

Calling these out of order causes:
- **`UpdateFrameBegin` before `UpdateFrameEnd`**: Outbound state never gets queued. Remote clients see stale data.
- **`Service` after `UpdateFrameEnd`**: Adds a full frame of latency — outbound packets aren't sent until next frame.
- **Missing `UpdateFrameEnd`/`UpdateFrameBegin` pair**: The SDK requires matched begin/end calls. The `_expectingEnd` flag tracks this.

**Rule**: Call all three methods every frame, in the exact order above. Use `UpdateSocketOnly()` as a lightweight alternative when the full frame loop isn't needed (e.g., during connection before joining a room).

## 7. Single-Threaded Constraint

The entire Fusion SDK is single-threaded. All API calls — `Client` methods, `Object` field access, `Photon` operations, string heap operations — must happen on the same thread that runs the frame loop.

There are no mutexes or thread-safety guarantees. Calling SDK methods from a background thread causes data races, corrupted state, and crashes.

**Rule**: Run the frame loop and all SDK interactions on the engine's main thread (or a single dedicated networking thread, if your engine supports it).

## 8. CRT Linkage Mismatch (Windows)

The Fusion SDK libraries are compiled with `/MD` (dynamic C runtime). If your integration links with `/MT` (static C runtime), you get heap corruption — objects allocated in one CRT are freed in another.

Symptoms include random crashes in `delete[]`, `free()`, or `std::vector` destructors, often in `Data::Free()` or `BufferT::~BufferT()`.

**Rule**: On Windows, always link with `/MD` (dynamic CRT) to match the SDK. In build systems, this is typically `use_static_cpp=no` or `/MD` compiler flag.

## 9. `alreadyPopulated` Flag for Scene Objects

`CreateSceneObject()` returns a `bool& alreadyPopulated` out-parameter that determines the initial data flow direction:

```cpp
bool alreadyPopulated = false;
auto* obj = client->CreateSceneObject(alreadyPopulated, words, type, header, headerLen, scene, id, flags);

if (!alreadyPopulated) {
    // First time: serialize engine defaults INTO the Words buffer
    serialize_current_state_to_words(obj);
} else {
    // Rejoining: Words buffer already has network state — apply it to the engine
    apply_words_to_engine_state(obj);
}
```

Getting this backwards means:
- `!alreadyPopulated` but you read from Words → engine gets zeroed-out defaults
- `alreadyPopulated` but you write to Words → you overwrite network state with stale engine defaults

**Rule**: Always check `alreadyPopulated` and handle both directions. The authority client typically sees `false` (first creation), while late-joining clients see `true` (object already exists on the network).

## 10. Array Capacity Is Immutable After Creation

Array properties reserve a fixed number of words at object creation time: `1 + (max_capacity * element_words)`. This word count is baked into the object's Words buffer size and cannot change after creation.

If you need a larger array, you must destroy and recreate the object with a larger word allocation. Attempting to write beyond the allocated capacity corrupts adjacent property data or the ObjectTail.

**Rule**: Determine the maximum array capacity at design time. Set it in the replication configuration before spawning. The runtime array can be smaller than the capacity (tracked by the count word at the start), but never larger.

## 11. Sub-Object StringHeap Delegation

`ObjectChild` does not have its own `NetworkedStringHeap`. String operations on a child object delegate to the root's heap:

```cpp
// ObjectChild::Root() returns the parent ObjectRoot
// Object::AddString() calls Root()->StringHeap.allocate_string()
```

This means:
- All children share the root's string heap capacity
- String handles are root-scoped, not child-scoped
- Freeing a child does NOT automatically free its strings from the root's heap

**Rule**: When destroying a sub-object, explicitly free all its string handles from the root's heap first. Track which handles belong to which child to avoid leaks.

## 12. CreateSubObject + AddSubObject Two-Step

Creating a sub-object requires two separate calls:

```cpp
// Step 1: Create the child object (allocates it, assigns ID)
auto* child = client->CreateSubObject(parentId, words, type, header, headerLen, targetHash, childId, specialFlags);

// Step 2: Copy spawn data into the child's Words buffer
memcpy(child->Words.Ptr, spawn_data, word_count * sizeof(int32_t));

// Step 3: Register the child with its parent (triggers replication)
bool success = client->AddSubObject(parentRoot, child);
```

Calling `AddSubObject` before writing spawn data sends empty/zeroed Words to remote clients. Forgetting `AddSubObject` entirely means the child exists locally but is never replicated.

**Rule**: Always follow the sequence: `CreateSubObject` → write spawn data → `AddSubObject`. The child's Words buffer must be populated before `AddSubObject` triggers replication.

## Summary Table

| # | Pitfall | Consequence | Prevention |
|---|---------|-------------|------------|
| 1 | Writing into ObjectTail | Corrupted AOI/Destroyed flags | Allocate `user_words + ExtraTailWords`, only write to `[0, user_words)` |
| 2 | Iteration order mismatch | Silent data corruption | Single property layout definition, identical iteration everywhere |
| 3 | Header inclusion order | Compile error (`Dictionary` collision) | SDK headers before engine headers |
| 4 | String handle leaks | Heap exhaustion | Free old handle before allocating new |
| 5 | StringHeap in spawn data | Invalid handle references | Raw bytes for initial strings, StringHeap for ongoing sync |
| 6 | Frame loop order | Stale data, extra latency | `Service()` → `UpdateFrameEnd()` → `UpdateFrameBegin(dt)` |
| 7 | Multi-threaded access | Data races, crashes | All SDK calls on one thread |
| 8 | CRT linkage mismatch | Heap corruption on Windows | Link with `/MD` (dynamic CRT) |
| 9 | `alreadyPopulated` mishandled | Zeroed or stale state | Check flag, handle both directions |
| 10 | Array capacity overflow | Data corruption | Fixed capacity at creation time |
| 11 | Sub-object string leaks | Root heap exhaustion | Explicitly free child strings on destroy |
| 12 | Missing AddSubObject | Child never replicated | `CreateSubObject` → write data → `AddSubObject` |

See also: [Architecture](concepts/architecture.md), [Objects](concepts/objects.md), [Object Creation](concepts/object-creation.md), [String Heap](concepts/string-heap.md), [Getting Started](integration/getting-started.md).
