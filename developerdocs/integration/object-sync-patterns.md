# Object Sync Patterns

This guide covers the practical patterns for synchronizing object state through Fusion's Words buffer: computing word offsets, mapping types to words, writing and reading properties, spawn data serialization, array replication, and string property sync.

See [Getting Started](getting-started.md) for the minimal integration skeleton.

## The Sync Loop

Every frame, the integration runs a two-phase sync cycle:

```
Authority client                          Non-authority client
     |                                           |
     |  sync_outbound()                          |
     |  ┌──────────────┐                         |
     |  │ Read engine   │                         |
     |  │ properties    │                         |
     |  │ Write to      │                         |
     |  │ Words buffer  │                         |
     |  └──────┬───────┘                         |
     |         v                                  |
     |  UpdateFrameEnd()  ──── network ────>  UpdateFrameBegin()
     |                                            |
     |                                     sync_inbound()
     |                                     ┌──────────────┐
     |                                     │ Read Words    │
     |                                     │ buffer        │
     |                                     │ Write to      │
     |                                     │ engine props  │
     |                                     └──────────────┘
```

The authority client is the source of truth. It writes engine-side property values into `Object::Words`. Fusion replicates the Words buffer over the network. Non-authority clients read from `Object::Words` and apply values to their engine-side representation.

## Words Buffer Layout

The Words buffer is a flat `int32_t` array on each `Object`. Properties are serialized sequentially at fixed offsets with no type markers, padding, or delimiters.

```
┌─────────────────────────────────────────────────────────────────────────┐
│  User data area                                │  Tail (6 words)        │
│  [prop0] [prop1] [prop2] ...                   │  [RqObj][IntKey][D][SR]│
└─────────────────────────────────────────────────────────────────────────┘
  ^                                               ^
  Words.Ptr[0]                                    Words.Ptr[userWords]
```

**Tail area (6 words = `Object::ExtraTailWords` = `sizeof(ObjectTail) / 4`):**

```cpp
#pragma pack(push, 4)
struct ObjectTail {
    int32_t RequiredObjectsCount;  // Number of required sub-objects
    uint64_t InterestKey;          // Interest/AOI key (8 bytes = 2 words)
    int32_t Destroyed;             // Object destruction flag
    int32_t SendRate;              // Per-object send rate override
    int32_t Dummy;                 // Padding
};
#pragma pack(pop)
static_assert(sizeof(ObjectTail) == 24); // 6 * sizeof(int32_t)
```

**Never write user data into the tail area.** Overwriting the `Destroyed` field silently kills the object. Overwriting `InterestKey` corrupts AOI filtering. Overwriting `SendRate` changes the object's network update rate.

The usable word count for user data is:

```cpp
int usableWords = static_cast<int>(obj->Words.Length) - SharedMode::Object::ExtraTailWords;
```

## Type-to-Word Mapping

Each property type occupies a fixed number of words. The integration must agree on this mapping between writer and reader.

| Type | Words | Encoding |
|------|-------|----------|
| `bool` | 1 | `0` or `1` as `int32_t` |
| `int32` | 1 | Direct `int32_t` |
| `float` | 1 | `memcpy` bit-cast from `float` |
| `int64` | 2 | Low word = bits [0:31], high word = bits [32:63] |
| `double` | 2 | `memcpy` to `int64_t`, then split to 2 words |
| `Vector2` (2x float) | 2 | `[x, y]` each as float word |
| `Vector2i` (2x int32) | 2 | `[x, y]` direct |
| `Vector3` (3x float) | 3 | `[x, y, z]` |
| `Vector3i` (3x int32) | 3 | `[x, y, z]` |
| `Vector4` / `Quaternion` (4x float) | 4 | `[x, y, z, w]` |
| `Vector4i` (4x int32) | 4 | `[x, y, z, w]` |
| `Color` (4x float) | 4 | `[r, g, b, a]` |
| `Rect2` (4x float) | 4 | `[pos.x, pos.y, size.x, size.y]` |
| `AABB` (6x float) | 6 | `[pos.x, pos.y, pos.z, size.x, size.y, size.z]` |
| `Plane` (4x float) | 4 | `[normal.x, normal.y, normal.z, d]` |
| `Basis` (9x float) | 9 | Row-major: `[r0c0, r0c1, r0c2, r1c0, ...]` |
| `Transform2D` (6x float) | 6 | Column-major: `[col0.x, col0.y, col1.x, col1.y, col2.x, col2.y]` |
| `Transform3D` (12x float) | 12 | `[Basis(9)] [origin.x, origin.y, origin.z]` |
| `CompressedQuat` (uint32) | 1 | Smallest-three encoding via `QuaternionCompress()` |
| `String` | 2 | StringHandle: `[id, generation]` (see [String Property Sync](#string-property-sync)) |

### Float-to-Word Bit-Cast

Floats are stored via `memcpy`, not pointer casting, to avoid undefined behavior:

```cpp
int32_t float_to_word(float value) {
    int32_t word;
    static_assert(sizeof(float) == sizeof(int32_t));
    memcpy(&word, &value, sizeof(float));
    return word;
}

float word_to_float(int32_t word) {
    float value;
    memcpy(&value, &word, sizeof(float));
    return value;
}
```

**Never use pointer casts** (`*reinterpret_cast<int32_t*>(&value)`) -- this violates strict aliasing rules and causes undefined behavior that can manifest as incorrect values under optimization.

### 64-bit Value Splitting

`int64` and `double` values occupy 2 words. Split into low and high 32-bit halves:

```cpp
void int64_to_words(int64_t value, int32_t& low, int32_t& high) {
    low  = static_cast<int32_t>(value & 0xFFFFFFFF);
    high = static_cast<int32_t>((value >> 32) & 0xFFFFFFFF);
}

int64_t words_to_int64(int32_t low, int32_t high) {
    return (static_cast<int64_t>(static_cast<uint32_t>(high)) << 32) |
            static_cast<uint32_t>(low);
}

void double_to_words(double value, int32_t& low, int32_t& high) {
    int64_t bits;
    memcpy(&bits, &value, sizeof(double));
    int64_to_words(bits, low, high);
}

double words_to_double(int32_t low, int32_t high) {
    int64_t bits = words_to_int64(low, high);
    double value;
    memcpy(&value, &bits, sizeof(double));
    return value;
}
```

## Word Offset Computation

Properties are laid out sequentially. The offset of each property depends on the total word count of all preceding properties.

```cpp
// Example layout:
//   Offset 0: position.x  (float, 1 word)
//   Offset 1: position.y  (float, 1 word)
//   Offset 2: position.z  (float, 1 word)
//   Offset 3: rotation    (float, 1 word)
//   Offset 4: health      (int32, 1 word)
//   Total: 5 user words

int word_offset = 0;

// Write position (3 words)
words[word_offset + 0] = float_to_word(position.x);
words[word_offset + 1] = float_to_word(position.y);
words[word_offset + 2] = float_to_word(position.z);
word_offset += 3;

// Write rotation (1 word)
words[word_offset] = float_to_word(rotation);
word_offset += 1;

// Write health (1 word)
words[word_offset] = health;
word_offset += 1;
```

**Iteration order must be identical in all of these functions:**

1. `sync_outbound()` -- authority writes engine state to Words
2. `sync_inbound()` -- non-authority reads Words to engine state
3. Spawn data serialization -- initial state at creation time
4. Spawn data deserialization -- initial state on the remote side
5. `compute_word_count()` -- buffer size calculation

If the order differs between any of these, properties will be read at wrong offsets and silently corrupted. There are no runtime checks.

## Writing Properties (sync_outbound)

The authority client writes to the Words buffer each frame:

```cpp
void SyncOutbound(SharedMode::Client* client, SharedMode::Object* obj) {
    if (!obj || !obj->Words.IsValid()) return;
    if (!client->IsOwner(obj)) return;

    int32_t* words = obj->Words.Ptr;
    int offset = 0;

    // Position (Vector3 = 3 words)
    words[offset + 0] = float_to_word(myPosition.x);
    words[offset + 1] = float_to_word(myPosition.y);
    words[offset + 2] = float_to_word(myPosition.z);
    offset += 3;

    // Rotation (float = 1 word)
    words[offset] = float_to_word(myRotation);
    offset += 1;

    // Health (int32 = 1 word)
    words[offset] = myHealth;
    offset += 1;

    // Ensure the object sends updates
    obj->SetSendUpdates(true);
}
```

## Reading Properties (sync_inbound)

Non-authority clients read from the Words buffer and apply to engine state:

```cpp
void SyncInbound(SharedMode::Client* client, SharedMode::Object* obj) {
    if (!obj || !obj->Words.IsValid()) return;
    if (client->IsOwner(obj)) return;

    // Optionally skip if no new data arrived this frame
    if (!client->HasBeenUpdatedByPlugin(obj, false)) return;

    const int32_t* words = obj->Words.Ptr;
    int offset = 0;

    // Position (Vector3 = 3 words)
    float px = word_to_float(words[offset + 0]);
    float py = word_to_float(words[offset + 1]);
    float pz = word_to_float(words[offset + 2]);
    offset += 3;

    // Rotation (float = 1 word)
    float rot = word_to_float(words[offset]);
    offset += 1;

    // Health (int32 = 1 word)
    int health = words[offset];
    offset += 1;

    // Apply to engine representation
    SetPosition(obj->Engine, px, py, pz);
    SetRotation(obj->Engine, rot);
    SetHealth(obj->Engine, health);
}
```

**`HasBeenUpdatedByPlugin` signature:**

```cpp
bool HasBeenUpdatedByPlugin(Object* obj, bool reset);
```

When `reset` is `true`, the internal flag is cleared after reading. When `false`, the flag persists until the SDK clears it. Use `true` if you only want to apply once per network update.

## Spawn Data Serialization

When an object is created, its initial state must be delivered to remote clients. There are two mechanisms:

### Header Data (Spawn Payload)

Passed as the `header`/`headerLength` parameters to `CreateObject()`. This data is delivered to remote clients as `ObjectRoot::Header` when the object arrives.

```cpp
// Authority side: serialize spawn data into a byte buffer
std::vector<uint8_t> spawnData = SerializeSpawnState(instance);

SharedMode::ObjectRoot* obj = client->CreateObject(
    totalWords, typeRef,
    reinterpret_cast<const PhotonCommon::CharType*>(spawnData.data()),
    spawnData.size(),
    sceneIndex, ownerMode
);
```

```cpp
// Remote side: read in OnObjectReady callback
subs += client->OnObjectReady.Subscribe([](SharedMode::ObjectRoot* obj) {
    if (obj->Header.Valid()) {
        DeserializeSpawnState(obj->Header.Ptr, obj->Header.Length);
    }
});
```

### Words Buffer Copy (Immediate State)

After creating the object, copy serialized properties directly to the Words buffer:

```cpp
// Authority side: after CreateObject
obj->SetSendUpdates(false);  // Prevent sending before data is ready

// Serialize properties to a temporary buffer
int32_t tempWords[MAX_WORDS];
int wordCount = SerializeProperties(tempWords);

// Copy to the object's Words buffer (user area only)
int usable = static_cast<int>(obj->Words.Length) - SharedMode::Object::ExtraTailWords;
int toCopy = std::min(wordCount, usable);
memcpy(obj->Words.Ptr, tempWords, toCopy * sizeof(int32_t));

obj->SetSendUpdates(true);
obj->SetHasValidData(true);
```

### Scene Objects: Bidirectional Initial Data

Scene objects use `CreateSceneObject()` which has an `alreadyPopulated` out-parameter:

```cpp
bool alreadyPopulated = false;
SharedMode::ObjectRoot* obj = client->CreateSceneObject(
    alreadyPopulated,     // [out] was this object already on the network?
    totalWords,           // total word count including tail
    typeRef,              // type reference
    nullptr, 0,           // header (typically empty for scene objects)
    sceneSequence,        // scene sequence number
    objectId,             // deterministic scene object ID
    ownerMode             // ownership mode
);

if (alreadyPopulated) {
    // Another client already set up this object.
    // The Words buffer contains network state -- apply to engine.
    DeserializeFromWords(obj);
} else {
    // We are first. Write engine defaults INTO the Words buffer.
    SerializeToWords(obj);
    obj->SetSendUpdates(true);
    obj->SetHasValidData(true);
}
```

**Getting `alreadyPopulated` backwards:**
- Reading from an unpopulated Words buffer gives zeroed-out defaults
- Writing to an already-populated Words buffer overwrites network state with stale engine defaults

## Array Replication

Arrays occupy a fixed region in the Words buffer. The capacity is set at creation time and cannot change.

### Memory Layout

```
┌───────────┬──────────┬──────────┬─────┬───────────────────────────────┐
│ count (1) │ elem[0]  │ elem[1]  │ ... │ elem[max_capacity - 1]        │
└───────────┴──────────┴──────────┴─────┴───────────────────────────────┘
```

Total words: `1 + (max_capacity * element_words)`

### Element Word Counts

| Array Element Type | Words per Element |
|--------------------|-------------------|
| `float` | 1 |
| `double` | 2 |
| `int32` | 1 |
| `int64` | 2 |
| `Vector2` (2x float) | 2 |
| `Vector3` (3x float) | 3 |
| `Vector4` (4x float) | 4 |
| `Color` (4x float) | 4 |

### Writing Arrays

```cpp
int WriteFloat32Array(const float* elements, int elementCount,
                      int maxCapacity, int32_t* words, int offset)
{
    int totalWords = 1 + maxCapacity; // float32: 1 word per element

    // Word 0: actual element count (clamped to capacity)
    int count = (elementCount < maxCapacity) ? elementCount : maxCapacity;
    words[offset] = count;

    // Words 1..count: element data
    for (int i = 0; i < count; i++) {
        words[offset + 1 + i] = float_to_word(elements[i]);
    }

    // Zero remaining slots to prevent stale data
    for (int i = count; i < maxCapacity; i++) {
        words[offset + 1 + i] = 0;
    }

    return totalWords; // advance offset by this amount
}
```

### Reading Arrays

```cpp
int ReadFloat32Array(float* outElements, int& outCount,
                     int maxCapacity, const int32_t* words, int offset)
{
    int totalWords = 1 + maxCapacity;

    outCount = words[offset];
    if (outCount < 0) outCount = 0;
    if (outCount > maxCapacity) outCount = maxCapacity;

    for (int i = 0; i < outCount; i++) {
        outElements[i] = word_to_float(words[offset + 1 + i]);
    }

    return totalWords; // advance offset by this amount
}
```

### Multi-Word Element Arrays (e.g., Vector3)

```cpp
int WriteVector3Array(const float* xyz, int elementCount,
                      int maxCapacity, int32_t* words, int offset)
{
    int totalWords = 1 + (maxCapacity * 3); // Vector3: 3 words per element

    int count = (elementCount < maxCapacity) ? elementCount : maxCapacity;
    words[offset] = count;

    for (int i = 0; i < count; i++) {
        words[offset + 1 + i * 3 + 0] = float_to_word(xyz[i * 3 + 0]);
        words[offset + 1 + i * 3 + 1] = float_to_word(xyz[i * 3 + 1]);
        words[offset + 1 + i * 3 + 2] = float_to_word(xyz[i * 3 + 2]);
    }

    // Zero remaining
    for (int i = count * 3; i < maxCapacity * 3; i++) {
        words[offset + 1 + i] = 0;
    }

    return totalWords;
}
```

**Key constraints:**
- Max capacity must be determined before object creation (it sets the buffer size)
- Arrays exceeding capacity are silently truncated
- The full `1 + max_capacity * element_words` region is always reserved, even if the array is empty
- Capacity cannot change after object creation. To resize, destroy and recreate the object.

## String Property Sync

Strings use the `NetworkedStringHeap` on each `ObjectRoot`. Strings are **not** stored inline in the Words buffer. Instead, the Words buffer stores a 2-word `StringHandle` (id + generation), and the actual string data lives in the heap.

### StringHandle Layout

```
Words[offset + 0] = handle.id         // uint32_t, 0 = invalid/empty
Words[offset + 1] = handle.generation  // uint32_t
```

### Writing a String

```cpp
void WriteString(SharedMode::Object* obj, int offset,
                 const char* newValue)
{
    int32_t* words = obj->Words.Ptr;

    // 1. Read the existing handle from Words
    SharedMode::StringHandle oldHandle;
    oldHandle.id = static_cast<uint32_t>(words[offset + 0]);
    oldHandle.generation = static_cast<uint32_t>(words[offset + 1]);

    // 2. Free the old handle to prevent heap leaks
    if (oldHandle.id != 0) {
        obj->FreeString(oldHandle);
    }

    // 3. Handle empty/null case
    if (newValue == nullptr || newValue[0] == '\0') {
        words[offset + 0] = 0;
        words[offset + 1] = 0;
        return;
    }

    // 4. Allocate new string in the heap
    SharedMode::StringHandle newHandle = obj->AddString(
        reinterpret_cast<const PhotonCommon::CharType*>(newValue)
    );

    // 5. Write handle to Words buffer
    words[offset + 0] = static_cast<int32_t>(newHandle.id);
    words[offset + 1] = static_cast<int32_t>(newHandle.generation);
}
```

### Reading a String

```cpp
const char* ReadString(SharedMode::Object* obj, int offset) {
    const int32_t* words = obj->Words.Ptr;

    SharedMode::StringHandle handle;
    handle.id = static_cast<uint32_t>(words[offset + 0]);
    handle.generation = static_cast<uint32_t>(words[offset + 1]);

    if (handle.id == 0) {
        return ""; // Empty string
    }

    SharedMode::StringMessage status;
    const PhotonCommon::CharType* resolved = obj->ResolveString(handle, status);

    if (status != SharedMode::StringMessage::Valid || !resolved) {
        return ""; // Handle expired or error
    }

    return reinterpret_cast<const char*>(resolved);
}
```

### Handle Lifecycle Rules

1. **Free before allocate.** Before writing a new string value, free the old handle via `FreeString()`. The heap is fixed-size; leaking handles exhausts it.
2. **Track handles per property.** Either read the current handle from the Words buffer each frame, or maintain a side map of active handles.
3. **On object destruction**, free all active string handles to reclaim heap space.
4. **Spawn data does NOT use the StringHeap.** At creation time, the heap does not exist on remote clients yet. Write `{0, 0}` (empty handle) in the initial Words buffer, then let `sync_outbound()` allocate the first real handle on the next frame.
5. **Sub-objects share the root's heap.** All string operations on an `ObjectChild` delegate to `Root()->StringHeap`. See [Sub-Objects](sub-objects.md#string-heap-delegation).

### StringHandle API

| Method | Signature | Notes |
|--------|-----------|-------|
| `AddString` | `StringHandle Object::AddString(const CharType* str)` | Allocate string in heap |
| `ResolveString` | `const CharType* Object::ResolveString(const StringHandle&, StringMessage&)` | Look up string data |
| `FreeString` | `StringHandle Object::FreeString(const StringHandle&)` | Release handle, returns zeroed handle |
| `IsValidStringHandle` | `bool Object::IsValidStringHandle(const StringHandle&)` | Check if handle is live |
| `GetStringLength` | `uint32_t Object::GetStringLength(const StringHandle&)` | Get string byte length |

### StringMessage Status Codes

| Value | Meaning |
|-------|---------|
| `Valid` | String resolved successfully |
| `NotALiveEntry` | Handle points to a freed entry |
| `WrongGeneration` | Handle generation does not match (stale handle) |
| `OutOfRange` | Handle ID exceeds entry array |
| `WrongSize` | Internal size mismatch |
| `EmptyString` | Entry exists but string is empty |
| `InvalidHandle` | Handle ID is 0 |

## Shadow Buffer and Dirty Detection

Each `Object` has a `Shadow` buffer that mirrors the last-acknowledged state of `Words`. The SDK uses this internally to compute deltas -- only changed words are transmitted over the network.

Your integration does not need to manage the Shadow buffer directly. The key APIs:

| Method | Signature | Purpose |
|--------|-----------|---------|
| `HasBeenUpdatedByPlugin` | `bool Client::HasBeenUpdatedByPlugin(Object* obj, bool reset)` | True if new data arrived from network |
| `SetHasValidData` | `void Object::SetHasValidData(bool)` | Mark object as having usable data |
| `SetSendUpdates` | `void Object::SetSendUpdates(bool)` | Enable/disable outbound transmission |

**`SetSendUpdates(false)`** should be set immediately after `CreateObject()` to prevent sending zeroed Words before spawn data is written. Enable it after the initial `memcpy`.

**`SetHasValidData(true)`** tells the SDK that the Words buffer contains meaningful data. Set it after writing initial state.

## Property List Pattern

A typical integration defines a property list and iterates it for all serialization operations. This ensures offset consistency:

```cpp
struct PropertyDef {
    const char* name;
    int wordCount;
    // type info, getter/setter function pointers, etc.
};

static PropertyDef g_properties[] = {
    {"position", 3},  // Vector3
    {"rotation", 1},  // float
    {"health",   1},  // int32
    {"name",     2},  // String (StringHandle)
};

int ComputeWordCount() {
    int total = 0;
    for (const auto& p : g_properties) {
        total += p.wordCount;
    }
    return total;
}

void SyncOutbound(SharedMode::Client* client, SharedMode::Object* obj) {
    if (!client->IsOwner(obj)) return;
    int offset = 0;
    for (const auto& p : g_properties) {
        WriteProperty(obj, p, offset);
        offset += p.wordCount;
    }
    obj->SetSendUpdates(true);
}

void SyncInbound(SharedMode::Client* client, SharedMode::Object* obj) {
    if (client->IsOwner(obj)) return;
    int offset = 0;
    for (const auto& p : g_properties) {
        ReadProperty(obj, p, offset);
        offset += p.wordCount;
    }
}

void SerializeSpawnData(SharedMode::Object* obj) {
    int offset = 0;
    for (const auto& p : g_properties) {
        WriteProperty(obj, p, offset);
        offset += p.wordCount;
    }
}
```

By iterating the same `g_properties` array in every function, you guarantee that offsets never drift out of sync.

## Next Steps

- [Sub-Objects](sub-objects.md) -- Child objects with their own Words buffers
- [Engine Binding](engine-binding.md) -- Connecting SDK objects to engine representations
- [Pitfalls](../pitfalls.md) -- Critical gotchas (tail writes, iteration order, string leaks)
