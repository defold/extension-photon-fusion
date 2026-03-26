# Pitfalls

Critical gotchas that every Fusion 3 SDK integration must handle. Each pitfall has caused real bugs in production integrations.

## 1. Writing into the ObjectTail

The last 6 words (`Object::ExtraTailWords = sizeof(ObjectTail) / 4`) of every object's Words buffer are reserved for internal use. The `ObjectTail` contains the interest key, destruction flag, send rate, and required objects count. Writing user data into this region corrupts the object's internal state.

```cpp
// ObjectTail layout (24 bytes = 6 words at the end of Words)
#pragma pack(push, 4)
struct ObjectTail {
    int32_t RequiredObjectsCount;  // offset +0
    uint64_t InterestKey;          // offset +4  (8 bytes = 2 words)
    int32_t Destroyed;             // offset +12
    int32_t SendRate;              // offset +16
    int32_t Dummy;                 // offset +20
};
#pragma pack(pop)
static_assert(sizeof(ObjectTail) == 24);
```

```cpp
// WRONG: user data overflows into tail
size_t totalWords = userWordCount; // forgot tail!
auto* obj = client->CreateObject(totalWords, type, header, headerLen, scene, ownerMode);
obj->Words.Ptr[userWordCount - 1] = myValue; // may overlap tail

// CORRECT: always add ExtraTailWords
size_t totalWords = userWordCount + SharedMode::Object::ExtraTailWords;
auto* obj = client->CreateObject(totalWords, type, header, headerLen, scene, ownerMode);
// Safe range: obj->Words.Ptr[0] through obj->Words.Ptr[userWordCount - 1]
// DANGER:    obj->Words.Ptr[userWordCount] onwards is ObjectTail
```

**Rule**: When allocating an object, request `user_words + Object::ExtraTailWords` total words. Only write to indices `0` through `user_words - 1`.

## 2. Words Buffer Iteration Order Mismatch

Properties in the Words buffer are serialized sequentially at fixed offsets with no type markers or length prefixes. The iteration order during `sync_outbound()` (writing) must exactly match `sync_inbound()` (reading) and spawn data serialization.

If you add, remove, or reorder properties and the sender/receiver disagree on layout, every property after the change point reads garbage data silently -- there are no runtime checks.

```cpp
// WRONG: different order in write vs read
void SyncOutbound(int32_t* words) {
    words[0] = health;                        // int32
    words[1] = float_to_word(position_x);     // float
}
void SyncInbound(const int32_t* words) {
    float px = word_to_float(words[0]);       // reads health as float!
    int hp = words[1];                        // reads position as int!
}

// CORRECT: same order in both
void SyncOutbound(int32_t* words) {
    words[0] = float_to_word(position_x);
    words[1] = health;
}
void SyncInbound(const int32_t* words) {
    float px = word_to_float(words[0]);
    int hp = words[1];
}
```

**Rule**: Define the property layout once (e.g., in a configuration resource) and iterate it identically in all serialization paths: sync_outbound, sync_inbound, spawn data write, spawn data read, and word count computation.

## 3. SDK Header Inclusion Order

The Fusion SDK pulls in Photon headers that define a `Dictionary` type. This collides with engine types (e.g., `godot::Dictionary`, `TMap` typedefs). SDK headers must be included **before** engine headers in every translation unit.

```cpp
// CORRECT: SDK headers first
#include "Client.h"     // Fusion SDK
#include "Types.h"      // Fusion SDK
#include <my_engine/node.hpp>  // Engine

// WRONG: Engine headers first -- Dictionary name collision
#include <my_engine/node.hpp>  // Engine
#include "Client.h"     // Fusion SDK -- COMPILE ERROR
```

**Rule**: Every `.cpp` file that uses both Fusion SDK and engine types must include SDK headers before engine headers. Consider using a precompiled header or a single include-order header to enforce this.

## 4. String Handle Leaks

`NetworkedStringHeap` is a fixed-size heap. Every call to `Object::AddString()` allocates space. If you update a string property without first calling `Object::FreeString()` on the old handle, the old allocation is never reclaimed.

```cpp
// WRONG: leaks the previous string
SharedMode::StringHandle handle = obj->AddString(toU8(new_value));
words[offset] = handle.id;
words[offset + 1] = handle.generation;

// CORRECT: free old handle before allocating new
SharedMode::StringHandle old_handle;
old_handle.id = static_cast<uint32_t>(words[offset]);
old_handle.generation = static_cast<uint32_t>(words[offset + 1]);
if (old_handle.id != 0) {
    obj->FreeString(old_handle);
}
SharedMode::StringHandle new_handle = obj->AddString(toU8(new_value));
words[offset] = new_handle.id;
words[offset + 1] = new_handle.generation;
```

**Rule**: Always free the old string handle before allocating a new one. Use `id == 0` to detect uninitialized/empty handles.

## 5. Spawn Data Cannot Use StringHeap

When an object is first created, its `NetworkedStringHeap` does not exist yet on remote clients. Spawn data (the `Header` field passed to `CreateObject()` / `CreateSceneObject()`) is serialized into a raw byte buffer, not the Words buffer.

If you write `StringHandle` values into spawn data, remote clients will receive handle IDs that reference a heap that has not been populated yet, resulting in `StringMessage::NotALiveEntry` or `StringMessage::OutOfRange` errors.

```cpp
// WRONG: StringHandle in spawn data
SharedMode::StringHandle h = obj->AddString(toU8("player_name"));
spawnWords[0] = h.id;        // Remote sees invalid handle
spawnWords[1] = h.generation;

// CORRECT: write empty handle at spawn, sync real string next frame
spawnWords[0] = 0;  // empty handle
spawnWords[1] = 0;
// In sync_outbound() on the next frame, AddString() works because
// the heap is now established on both sides.
```

**Rule**: For initial string values, serialize them directly into the header byte buffer as raw bytes (length-prefixed or null-terminated). Only use `AddString()` / `StringHandle` for ongoing state sync after the object is fully created.

## 6. Frame Loop Order

The frame loop must execute in this exact order every frame:

```
realtimeClient.Service()  ->  client.UpdateFrameEnd()  ->  client.UpdateFrameBegin(dt)
```

- `Service()` -- processes network I/O (send queued packets, receive incoming packets)
- `UpdateFrameEnd()` -- finishes the current frame (queues outbound state/RPCs)
- `UpdateFrameBegin(dt)` -- starts the next frame (applies received state, fires callbacks)

```cpp
// WRONG: Begin before End
g_realtime->Service();
g_client->UpdateFrameBegin(dt);  // processes nothing, callbacks fire with stale data
g_client->UpdateFrameEnd();      // SDK assertion or _expectingEnd flag error

// WRONG: Service after End
g_client->UpdateFrameEnd();      // queues packets but nothing to send yet
g_realtime->Service();           // sends packets one frame late
g_client->UpdateFrameBegin(dt);

// CORRECT
g_realtime->Service();
SyncOutbound();
g_client->UpdateFrameEnd();
g_client->UpdateFrameBegin(dt);
SyncInbound();
```

**Rule**: Call all three methods every frame in the exact order: `Service()`, `UpdateFrameEnd()`, `UpdateFrameBegin(dt)`. Insert `sync_outbound` before End and `sync_inbound` after Begin.

## 7. Single-Threaded Constraint

The entire Fusion SDK is single-threaded. All API calls -- `Client` methods, `Object` field access, `RealtimeClient` operations, string heap operations -- must happen on the same thread that runs the frame loop.

There are no mutexes or thread-safety guarantees. Calling SDK methods from a background thread causes data races, corrupted state, and crashes.

```cpp
// WRONG: accessing from a worker thread
std::thread worker([&]() {
    auto* obj = client->FindObject(id);  // data race!
    memcpy(&obj->Words.Ptr[0], &value, 4);  // corrupted state!
});

// CORRECT: all SDK access on the main/frame thread
void OnMainThread() {
    auto* obj = client->FindObject(id);
    memcpy(&obj->Words.Ptr[0], &value, 4);
}
```

**Rule**: Run the frame loop and all SDK interactions on the engine's main thread (or a single dedicated networking thread if your engine supports it).

## 8. CRT Linkage Mismatch (Windows)

The Fusion SDK libraries are compiled with `/MD` (dynamic C runtime). If your integration links with `/MT` (static C runtime), you get heap corruption -- objects allocated in one CRT are freed in another.

Symptoms include random crashes in `delete[]`, `free()`, or `std::vector` destructors, often in `Data::Free()` or `BufferT::~BufferT()`.

```cmake
# WRONG: static CRT
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")         # /MT

# CORRECT: dynamic CRT to match the SDK
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")       # /MD
```

**Rule**: On Windows, always link with `/MD` (dynamic CRT) to match the SDK. In CMake, set `CMAKE_MSVC_RUNTIME_LIBRARY` to `MultiThreadedDLL` (Release) or `MultiThreadedDebugDLL` (Debug).

## 9. `alreadyPopulated` Flag for Scene Objects

`CreateSceneObject()` returns a `bool& alreadyPopulated` out-parameter that determines the initial data flow direction:

```cpp
// WRONG: ignoring alreadyPopulated
bool alreadyPopulated = false;
auto* obj = client->CreateSceneObject(alreadyPopulated, words, type,
                                       nullptr, 0, seq, id, ownerMode);
SerializeToWords(obj);  // Always writes -- overwrites network state if populated!

// CORRECT: handle both directions
bool alreadyPopulated = false;
auto* obj = client->CreateSceneObject(alreadyPopulated, words, type,
                                       nullptr, 0, seq, id, ownerMode);
if (!alreadyPopulated) {
    // First time: write engine defaults INTO the Words buffer
    SerializeToWords(obj);
    obj->SetSendUpdates(true);
    obj->SetHasValidData(true);
} else {
    // Rejoining: Words buffer has network state -- apply to engine
    DeserializeFromWords(obj);
    obj->SetHasValidData(true);
}
```

Getting this backwards means:
- `!alreadyPopulated` but you read from Words: engine gets zeroed-out defaults
- `alreadyPopulated` but you write to Words: you overwrite network state with stale engine defaults

**Rule**: Always check `alreadyPopulated` and handle both directions. The first client to create the scene object sees `false`; late-joining clients see `true`.

## 10. Array Capacity Is Immutable After Creation

Array properties reserve a fixed number of words at object creation time: `1 + (max_capacity * element_words)`. This word count is baked into the object's Words buffer size and cannot change after creation.

```cpp
// WRONG: writing beyond allocated capacity
int maxCapacity = 4;
int totalWords = 1 + maxCapacity; // 5 words reserved for array

// Later, trying to store 10 elements:
words[0] = 10;  // count = 10
for (int i = 0; i < 10; i++) {
    words[1 + i] = values[i];  // elements 4-9 overflow into other properties!
}

// CORRECT: clamp to capacity
words[0] = std::min(elementCount, maxCapacity);
int count = words[0];
for (int i = 0; i < count; i++) {
    words[1 + i] = values[i];
}
```

**Rule**: Determine the maximum array capacity at design time. Set it in the replication configuration before spawning. The runtime array can be smaller than capacity (tracked by the count word), but never larger.

## 11. Sub-Object StringHeap Delegation

`ObjectChild` does not have its own `NetworkedStringHeap`. String operations on a child object delegate to the root's heap:

```cpp
// ObjectChild::Root() returns the parent ObjectRoot
// Object::AddString() internally calls Root()->StringHeap.allocate_string()
```

This means:
- All children share the root's string heap capacity
- String handles are root-scoped, not child-scoped
- **Freeing a child does NOT automatically free its strings from the root's heap**

```cpp
// WRONG: destroying child without freeing strings
client->DestroySubObjectLocal(child);  // strings leaked in root heap!

// CORRECT: free child strings before destruction
for (int stringOffset : childStringOffsets) {
    SharedMode::StringHandle h;
    h.id = static_cast<uint32_t>(child->Words.Ptr[stringOffset]);
    h.generation = static_cast<uint32_t>(child->Words.Ptr[stringOffset + 1]);
    if (h.id != 0) {
        child->FreeString(h);
    }
}
client->DestroySubObjectLocal(child);
```

**Rule**: When destroying a sub-object, explicitly free all its string handles from the root's heap first. Track which handles belong to which child.

## 12. CreateSubObject + AddSubObject Two-Step

Creating a sub-object requires two separate API calls:

```cpp
// WRONG: AddSubObject before writing data
auto* child = client->CreateSubObject(parentId, words, type, header, headerLen,
                                       targetHash, childId, specialFlags);
client->AddSubObject(parent, child);  // sends empty Words to remotes!
memcpy(child->Words.Ptr, data, size);  // too late, already replicated

// WRONG: forgetting AddSubObject
auto* child = client->CreateSubObject(parentId, words, type, header, headerLen,
                                       targetHash, childId, specialFlags);
memcpy(child->Words.Ptr, data, size);
// child exists locally but is never replicated!

// CORRECT: create -> write data -> register
auto* child = client->CreateSubObject(parentId, words, type, header, headerLen,
                                       targetHash, childId, specialFlags);
memcpy(child->Words.Ptr, data, userWordCount * sizeof(int32_t));
child->SetHasValidData(true);
client->AddSubObject(parent, child);
```

**Rule**: Always follow the sequence: `CreateSubObject` -> write spawn data -> `AddSubObject`. The child's Words buffer must be populated before `AddSubObject` triggers replication.

## 13. SubscriptionBag Lifetime

The `SubscriptionBag` holds `ScopedSubscription` objects that automatically unsubscribe when destroyed. If the bag is destroyed while the `Client` (and its broadcasters) is still alive, the subscriptions are cleanly removed. But if the bag outlives the broadcaster (or the broadcaster is destroyed first), you get a use-after-free.

```cpp
// WRONG: bag destroyed after client
{
    SharedMode::Client* client = new SharedMode::Client(*realtime);
    PhotonCommon::SubscriptionBag subs;
    subs += client->OnObjectReady.Subscribe([](auto*) { /*...*/ });

    delete client;  // Broadcaster destroyed
}
// ~SubscriptionBag tries to unsubscribe from dead broadcaster: UAF!

// CORRECT: unsubscribe before destroying client
{
    SharedMode::Client* client = new SharedMode::Client(*realtime);
    PhotonCommon::SubscriptionBag subs;
    subs += client->OnObjectReady.Subscribe([](auto*) { /*...*/ });

    subs.UnsubscribeAll();  // Clean unsubscribe while broadcaster alive
    delete client;
}

// ALSO CORRECT: bag destroyed before client (RAII order)
{
    SharedMode::Client client(*realtime);  // constructed first
    PhotonCommon::SubscriptionBag subs;    // constructed second
    subs += client.OnObjectReady.Subscribe([](auto*) { /*...*/ });
    // ~subs runs first (reverse construction order), client still alive
    // ~client runs second
}
```

**Rule**: Either call `UnsubscribeAll()` explicitly before destroying the client, or ensure the `SubscriptionBag` is destroyed before the client via RAII ordering (declare the bag after the client so it is destroyed first).

## 14. Task\<Result\<T\>\> Error Handling

The `Task<Result<T>>` pattern used by `RealtimeClient` async operations requires checking `IsOk()` before accessing the value. Calling `GetValue()` on an error result triggers an assertion failure (debug) or undefined behavior (release).

```cpp
// WRONG: unchecked GetValue
auto result = co_await realtime->JoinOrCreateRoom(u8"room");
auto room = result.GetValue();  // assertion failure if join failed!

// WRONG: checking result as bool then ignoring error path
auto result = co_await realtime->JoinOrCreateRoom(u8"room");
if (result) {
    auto room = result.GetValue();
}
// ...but what happens on failure? Silently continues with no room.

// CORRECT: check IsOk/IsErr, handle both paths
auto result = co_await realtime->JoinOrCreateRoom(u8"room");
if (result.IsErr()) {
    auto& error = result.GetError();
    printf("Join failed: code=%d msg=%s\n",
           static_cast<int>(error.code),
           reinterpret_cast<const char*>(error.message.c_str()));
    co_return Result<void>::Err(error);
}
auto room = result.GetValue();  // safe: IsOk() is true
```

For the polling pattern, always check `IsReady()` before `Get()`, and check the result:

```cpp
// CORRECT: polling pattern
if (task.IsReady()) {
    auto result = task.Get();
    if (result.IsOk()) {
        // proceed
    } else {
        auto& error = result.GetError();
        printf("Error: %d\n", static_cast<int>(error.code));
    }
}
```

**Rule**: Always check `IsOk()` or `IsErr()` on a `Result<T>` before calling `GetValue()` or `GetError()`. Handle both success and failure paths explicitly. Use `ValueOr(defaultValue)` when a default is acceptable.

## Summary Table

| # | Pitfall | Consequence | Prevention |
|---|---------|-------------|------------|
| 1 | Writing into ObjectTail | Corrupted interest/destroyed/send-rate flags | Allocate `user_words + ExtraTailWords`, write only to `[0, user_words)` |
| 2 | Iteration order mismatch | Silent data corruption | Single property layout, identical iteration everywhere |
| 3 | Header inclusion order | Compile error (Dictionary collision) | SDK headers before engine headers |
| 4 | String handle leaks | Heap exhaustion | Free old handle before allocating new |
| 5 | StringHeap in spawn data | Invalid handle references on remote | Raw bytes for initial strings, StringHeap for ongoing sync |
| 6 | Frame loop order | Stale data, extra latency, assertion failure | `Service()` -> `UpdateFrameEnd()` -> `UpdateFrameBegin(dt)` |
| 7 | Multi-threaded access | Data races, crashes | All SDK calls on one thread |
| 8 | CRT linkage mismatch | Heap corruption on Windows | Link with `/MD` (dynamic CRT) |
| 9 | `alreadyPopulated` mishandled | Zeroed or stale state | Check flag, handle both directions |
| 10 | Array capacity overflow | Data corruption | Fixed capacity at creation time, clamp writes |
| 11 | Sub-object string leaks | Root heap exhaustion | Explicitly free child strings on destroy |
| 12 | Missing AddSubObject | Child never replicated | `CreateSubObject` -> write data -> `AddSubObject` |
| 13 | SubscriptionBag lifetime | Use-after-free | Unsubscribe before destroying client, or RAII order |
| 14 | Unchecked Task\<Result\<T\>\> | Assertion failure / UB | Always check `IsOk()` before `GetValue()` |

See also: [Getting Started](integration/getting-started.md), [Object Sync Patterns](integration/object-sync-patterns.md), [Sub-Objects](integration/sub-objects.md), [Engine Binding](integration/engine-binding.md).
