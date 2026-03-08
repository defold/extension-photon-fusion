# Object Sync Patterns

This guide covers the practical patterns for synchronizing object state through Fusion's Words buffer: computing word offsets, mapping types to words, writing and reading properties, spawn data serialization, array replication, and string property sync.

See [Serialization](../concepts/serialization.md) for the conceptual foundation and [Getting Started](getting-started.md) for the minimal integration skeleton.

## The Sync Loop

Every frame, the integration runs a two-phase sync cycle:

```
Authority client                          Non-authority client
     |                                            |
     |  sync_outbound()                           |
     |  ┌──────────────┐                          |
     |  │ Read engine  │                          |
     |  │ properties   │                          |
     |  │ Write to     │                          |
     |  │ Words buffer │                          |
     |  └──────┬───────┘                          |
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
┌─────────────────────────────────────────────────────┐
│  User data area                     │  Tail (6 words) │
│  [prop0] [prop1] [prop2] ...        │  [AOI][Destroyed]│
└─────────────────────────────────────────────────────┘
  ^                                    ^
  Words.Ptr[0]                         Words.Ptr[Words.Length - 6]
```

**Tail area (6 words = `sizeof(ObjectTail)`):**

```cpp
struct ObjectTail {
    int32_t AOI_X;       // Area of Interest X
    int32_t AOI_Y;       // Area of Interest Y
    int32_t AOI_Z;       // Area of Interest Z
    int32_t AOI_SET;     // AOI has been set
    int32_t Destroyed;   // Object destruction flag
    int32_t Dummy;       // Padding
};
static_assert(sizeof(ObjectTail) == 24); // 6 * sizeof(int32_t)
```

**Never write to the tail area.** Overwriting the `Destroyed` field silently kills the object.

The usable word count is:

```cpp
constexpr int tailWords = sizeof(SharedMode::ObjectTail) / sizeof(int32_t); // 6
int usableWords = static_cast<int>(obj->Words.Length) - tailWords;
```

## Type-to-Word Mapping

Each property type occupies a fixed number of words. The integration must agree on this mapping between writer and reader.

| Type | Words | Encoding |
|------|-------|----------|
| `bool` | 1 | `0` or `1` |
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
| `String` | 2 | StringHandle: `[id, generation]` (see [String Sync](#string-property-sync)) |

### Float-to-Word Bit-Cast

Floats are stored via `memcpy`, not casting, to avoid undefined behavior:

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

### 64-bit Value Splitting

`int64` and `double` values occupy 2 words:

```cpp
void int64_to_words(int64_t value, int32_t& low, int32_t& high) {
    low  = static_cast<int32_t>(value & 0xFFFFFFFF);
    high = static_cast<int32_t>((value >> 32) & 0xFFFFFFFF);
}

int64_t words_to_int64(int32_t low, int32_t high) {
    return (static_cast<int64_t>(static_cast<uint32_t>(high)) << 32) |
            static_cast<uint32_t>(low);
}
```

## Word Offset Computation

Properties are laid out sequentially. The offset of each property depends on the total word count of all preceding properties.

```cpp
// Example: sync position (Vector3 = 3 words) + rotation (float = 1 word) + health (int32 = 1 word)
//
// Offset 0: position.x  (float, 1 word)
// Offset 1: position.y  (float, 1 word)
// Offset 2: position.z  (float, 1 word)
// Offset 3: rotation    (float, 1 word)
// Offset 4: health      (int32, 1 word)
// Total: 5 user words

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

**Iteration order must be identical in:**
1. `sync_outbound()` -- authority writes
2. `sync_inbound()` -- non-authority reads
3. `serialize_spawn_data()` -- initial state at creation
4. `deserialize_spawn_data()` -- initial state on remote
5. `compute_word_count()` -- buffer size calculation

If the order differs, data will be read at the wrong offsets and silently corrupted.

## Writing Properties (sync_outbound)

The authority client writes to the Words buffer each frame:

```cpp
void SyncOutbound(SharedMode::Object* obj) {
    if (!obj || !obj->Words.IsValid()) return;

    int32_t* words = obj->Words.Ptr;
    constexpr int tailWords = sizeof(SharedMode::ObjectTail) / sizeof(int32_t);
    int maxWords = static_cast<int>(obj->Words.Length) - tailWords;
    int offset = 0;

    // Write each property at its computed offset
    // (Must match the order in SyncInbound and SpawnData)

    // Position (Vector3 = 3 words)
    words[offset + 0] = float_to_word(myPosition.x);
    words[offset + 1] = float_to_word(myPosition.y);
    words[offset + 2] = float_to_word(myPosition.z);
    offset += 3;

    // Rotation (float = 1 word)
    words[offset] = float_to_word(myRotation);
    offset += 1;

    // Health (int = 1 word)
    words[offset] = myHealth;
    offset += 1;

    // Enable sending (disable initially after CreateObject, enable on first outbound)
    obj->SetSendUpdates(true);
}
```

**Godot integration pattern**: The Godot integration iterates auto-sync paths (position, rotation) first, then custom properties from a `FusionReplicationConfig` resource. This two-pass approach keeps the serialization order deterministic.

## Reading Properties (sync_inbound)

Non-authority clients read from the Words buffer and apply to engine state:

```cpp
void SyncInbound(SharedMode::Object* obj) {
    if (!obj || !obj->Words.IsValid()) return;

    // Skip if we own this object (we're the authority)
    if (g_client->IsOwner(obj)) return;

    // Optionally skip if no new data from network
    if (!g_client->HasBeenUpdatedByPlugin(obj)) return;

    const int32_t* words = obj->Words.Ptr;
    constexpr int tailWords = sizeof(SharedMode::ObjectTail) / sizeof(int32_t);
    int maxWords = static_cast<int>(obj->Words.Length) - tailWords;
    int offset = 0;

    // Read position (Vector3 = 3 words)
    float px = word_to_float(words[offset + 0]);
    float py = word_to_float(words[offset + 1]);
    float pz = word_to_float(words[offset + 2]);
    offset += 3;

    // Read rotation (float = 1 word)
    float rot = word_to_float(words[offset]);
    offset += 1;

    // Read health (int = 1 word)
    int health = words[offset];
    offset += 1;

    // Apply to engine representation
    SetPosition(obj->Engine, px, py, pz);
    SetRotation(obj->Engine, rot);
    SetHealth(obj->Engine, health);
}
```

**Dirty checking**: The SDK tracks which words changed via the Shadow buffer (`Object::Shadow`). `HasBeenUpdatedByPlugin()` tells you if new data arrived this frame. This avoids unnecessary property writes.

## Spawn Data Serialization

When an object is created, its initial state must be delivered to remote clients. There are two mechanisms:

### Header Data (Spawn Payload)

Passed as the `header`/`headerLength` parameters to `CreateObject()`. This data is delivered to remote clients in the `OnObjectCreated` callback via `ObjectRoot::Header`.

```cpp
// Authority side: create with spawn data
PackedByteArray spawnData = SerializeSpawnState(instance);
SharedMode::ObjectRoot* obj = g_client->CreateObject(
    totalWords, typeRef,
    reinterpret_cast<const SharedMode::CharType*>(spawnData.data()),
    spawnData.size(),
    sceneIndex, flags
);
```

```cpp
// Remote side: read in OnObjectCreated
g_client->OnObjectCreated = [](SharedMode::ObjectRoot* obj) {
    if (obj->Header.Valid()) {
        DeserializeSpawnState(obj->Header.Ptr, obj->Header.Length);
    }
};
```

### Words Buffer Copy (Immediate State)

After creating the object, copy serialized properties directly to the Words buffer:

```cpp
// Authority side: after CreateObject, copy initial state to Words
obj->SetSendUpdates(false);  // Prevent sending before data is ready

// Serialize properties to a temporary buffer
int32_t tempWords[MAX_WORDS];
int wordCount = SerializeProperties(tempWords);

// Copy to the object's Words buffer (skip tail area)
constexpr int tailWords = sizeof(SharedMode::ObjectTail) / sizeof(int32_t);
int usable = static_cast<int>(obj->Words.Length) - tailWords;
int toCopy = std::min(wordCount, usable);
memcpy(obj->Words.Ptr, tempWords, toCopy * sizeof(int32_t));

obj->SetSendUpdates(true);
obj->SetHasValidData(true);
```

The Godot integration uses this `memcpy` approach for both spawned objects and scene objects.

### Scene Objects: Bidirectional Initial Data

Scene objects use `CreateSceneObject()` which has an `alreadyPopulated` out parameter:

```cpp
bool alreadyPopulated = false;
SharedMode::ObjectRoot* obj = g_client->CreateSceneObject(
    alreadyPopulated, totalWords, typeRef,
    nullptr, 0,              // no header for scene objects
    sceneSequence, objectId, flags
);

if (alreadyPopulated) {
    // Another client already set up this object -- read Words to engine
    DeserializeFromWords(obj);
} else {
    // We're first -- write engine defaults to Words
    SerializeToWords(obj);
}
```

This eliminates the need for a pending queue since each client creates its own scene objects locally and the SDK tells them which direction data should flow.

## Array Replication

Arrays occupy a fixed region in the Words buffer. The capacity is set at creation time and cannot change.

### Memory Layout

```
┌───────────┬───────────────────────────────────────────┐
│ count (1) │ element[0] ... element[max_capacity - 1]  │
└───────────┴───────────────────────────────────────────┘
```

Total words: `1 + (max_capacity * element_words)`

### Element Word Counts

| Array Type | Element Words |
|------------|---------------|
| `PackedFloat32Array` | 1 |
| `PackedFloat64Array` | 2 |
| `PackedInt32Array` | 1 |
| `PackedInt64Array` | 2 |
| `PackedVector2Array` | 2 |
| `PackedVector3Array` | 3 |
| `PackedVector4Array` | 4 |
| `PackedColorArray` | 4 |

### Writing Arrays

```cpp
int WriteArrayToWords(const float* elements, int elementCount,
                      int maxCapacity, int32_t* words, int offset) {
    int totalWords = 1 + maxCapacity; // For float32: 1 word per element

    // Word 0: actual count
    words[offset] = std::min(elementCount, maxCapacity);
    int count = words[offset];

    // Words 1..count: element data
    for (int i = 0; i < count; i++) {
        words[offset + 1 + i] = float_to_word(elements[i]);
    }

    // Zero remaining slots (prevents stale data)
    for (int i = count; i < maxCapacity; i++) {
        words[offset + 1 + i] = 0;
    }

    return totalWords;
}
```

### Reading Arrays

```cpp
int ReadArrayFromWords(float* outElements, int& outCount,
                       int maxCapacity, const int32_t* words, int offset) {
    int totalWords = 1 + maxCapacity;

    outCount = words[offset];
    outCount = std::clamp(outCount, 0, maxCapacity);

    for (int i = 0; i < outCount; i++) {
        outElements[i] = word_to_float(words[offset + 1 + i]);
    }

    return totalWords;
}
```

**Key constraints:**
- Max capacity must be set before object creation (it determines buffer size)
- Arrays exceeding capacity are truncated silently
- The full `1 + max_capacity * element_words` region is always reserved, even if the array is empty

## String Property Sync

Strings use the `NetworkedStringHeap`, which is a separate data structure on each `ObjectRoot`. Strings are **not** stored inline in the Words buffer. Instead, the Words buffer stores a 2-word `StringHandle` (id + generation), and the actual string data lives in the heap.

### StringHandle Layout

```
Words[offset + 0] = handle.id         // uint32_t, 0 = invalid/empty
Words[offset + 1] = handle.generation  // uint32_t
```

### Writing a String

```cpp
void WriteString(SharedMode::Object* obj, int offset,
                 const char* str, StringHandle& prevHandle) {
    // 1. Free the old handle to prevent leaks
    if (prevHandle.id != 0) {
        obj->FreeString(prevHandle);
    }

    int32_t* words = obj->Words.Ptr;

    if (str == nullptr || str[0] == '\0') {
        // Empty string: write invalid handle
        words[offset + 0] = 0;
        words[offset + 1] = 0;
        prevHandle = {0, 0};
        return;
    }

    // 2. Allocate new string in the heap
    SharedMode::StringHandle handle = obj->AddString(
        reinterpret_cast<const SharedMode::CharType*>(str)
    );

    // 3. Write handle to Words buffer
    words[offset + 0] = static_cast<int32_t>(handle.id);
    words[offset + 1] = static_cast<int32_t>(handle.generation);

    // 4. Track for future cleanup
    prevHandle = handle;
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
    const SharedMode::CharType* resolved = obj->ResolveString(handle, status);

    if (status != SharedMode::StringMessage::Valid || !resolved) {
        return ""; // Handle expired or error
    }

    return reinterpret_cast<const char*>(resolved);
}
```

### Handle Lifecycle Management

String handles must be actively tracked and freed to avoid leaking heap memory:

1. **Before writing a new string**, free the old handle via `Object::FreeString()`
2. **Track active handles** per property path so you can free them on change
3. **On object destruction**, free all active handles
4. **Spawn data** does NOT use the StringHeap (the object doesn't exist yet). Write invalid handles (`{0, 0}`) at spawn time, and let `sync_outbound()` allocate the first real handle.

**Godot integration pattern**: `FusionSynchronizer` maintains a `std::unordered_map<NodePath, std::pair<uint32_t, uint32_t>>` called `active_string_handles` that tracks the current handle for each string property. Before each outbound sync, it frees the old handle, allocates a new one, and updates the tracking map.

### StringHandle API

| Method | Signature | Notes |
|--------|-----------|-------|
| `AddString` | `StringHandle Object::AddString(const CharType* str)` | Allocate string in heap |
| `ResolveString` | `const CharType* Object::ResolveString(const StringHandle&, StringMessage&)` | Look up string data |
| `FreeString` | `StringHandle Object::FreeString(const StringHandle&)` | Release handle |
| `GetStringLength` | `uint32_t Object::GetStringLength(const StringHandle&)` | Get string byte length |

String operations on an `ObjectChild` delegate to `Root()->StringHeap` -- the heap is shared across the entire root object tree.

See [String Heap](../concepts/string-heap.md) for the conceptual model and [StringHeap Reference](../reference/stringheap-api.md) for the full API.

## Shadow Buffer and Dirty Detection

Each `Object` has a `Shadow` buffer that mirrors the last-acknowledged state of `Words`. The SDK uses this internally to compute deltas -- only changed words are transmitted.

Your integration does not need to manage the Shadow buffer directly. The key APIs:

| Method | Signature | Purpose |
|--------|-----------|---------|
| `HasBeenUpdatedByPlugin` | `bool Client::HasBeenUpdatedByPlugin(Object* obj)` | True if new data arrived this frame |
| `SetHasValidData` | `void Object::SetHasValidData(bool)` | Mark object as having usable data |
| `SetSendUpdates` | `void Object::SetSendUpdates(bool)` | Enable/disable outbound transmission |

**`SetSendUpdates(false)`** should be set immediately after `CreateObject()` to prevent sending zeroed Words before spawn data is written. Enable it after the initial `memcpy`.

## Putting It All Together

A typical integration defines a property list and iterates it for all three operations:

```cpp
struct PropertyDef {
    const char* name;
    int wordCount;
    // ... type info, getter/setter
};

static PropertyDef g_properties[] = {
    {"position", 3},  // Vector3
    {"rotation", 1},  // float
    {"health",   1},  // int32
    {"name",     2},  // String (StringHandle)
};

int ComputeWordCount() {
    int total = 0;
    for (auto& p : g_properties) total += p.wordCount;
    return total;
}

void SyncOutbound(Object* obj) {
    int offset = 0;
    for (auto& p : g_properties) {
        WriteProperty(obj, p, offset);
        offset += p.wordCount;
    }
}

void SyncInbound(Object* obj) {
    int offset = 0;
    for (auto& p : g_properties) {
        ReadProperty(obj, p, offset);
        offset += p.wordCount;
    }
}
```

This ensures offset consistency across all code paths.

## Next Steps

- [Sub-Objects](sub-objects.md) -- Child objects with their own Words buffers
- [Engine Binding](engine-binding.md) -- Connecting SDK objects to engine representations
- [String Heap](../concepts/string-heap.md) -- Deep dive into heap internals
- [Serialization](../concepts/serialization.md) -- Conceptual model
