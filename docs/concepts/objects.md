# Objects

Networked objects are the core data model in Fusion. Each object owns a fixed-size buffer of [Words](architecture.md#word) that is automatically replicated to other clients. The SDK provides a three-level class hierarchy for different object roles.

## Class Hierarchy

```
Object (abstract base)
  +-- ObjectRoot    (top-level networked entity)
  +-- ObjectChild   (sub-object attached to a root)
```

All three classes live in the `SharedMode` namespace.

### Object (Base)

The common base class. Holds the Words buffer, shadow state, type information, and SDK bookkeeping.

```cpp
class Object {
public:
    ObjectId Id;
    void* Engine;              // Opaque pointer for engine integration
    ObjectType ObjectType;     // Base, Root, or Child
    bool HasValidData;
    Data Header;               // Spawn data (serialized at creation time)
    TypeRef Type;              // Type hash + word count

    BufferT<Word> Shadow;      // Last-acked state (for dirty detection)
    BufferT<Word> Words;       // Current replicated state

    ObjectSpecialFlags SpecialFlags;

    static constexpr size_t ExtraTailWords = sizeof(ObjectTail) / 4;  // = 6

    void SetHasValidData(bool hasValidData);
    void SetSendUpdates(bool sendUpdates);

    virtual ObjectRoot* Root() = 0;

    // String heap operations (delegate to Root()->StringHeap)
    StringHandle AddString(const CharType* str);
    const CharType* ResolveString(const StringHandle& handle, StringMessage& outStatus);
    StringHandle FreeString(const StringHandle& handle);
    uint32_t GetStringLength(const StringHandle& handle);
};
```

**Key fields:**

| Field | Type | Purpose |
|-------|------|---------|
| `Id` | `ObjectId` | Globally unique identifier (origin player + counter) |
| `Engine` | `void*` | Opaque pointer for engine integration to store its own data |
| `Words` | `BufferT<Word>` | Current replicated state buffer |
| `Shadow` | `BufferT<Word>` | Previous acked state for change detection |
| `Header` | `Data` | Immutable spawn data sent at creation time |
| `Type` | `TypeRef` | Type hash and total word count (including tail) |
| `HasValidData` | `bool` | Whether the Words buffer contains meaningful data |

### ObjectRoot

A top-level networked entity. Owns the [StringHeap](string-heap.md) and tracks ownership, timing, scene membership, and sub-objects.

```cpp
class ObjectRoot final : public Object {
public:
    double Time;
    PlayerId Owner;
    ObjectFlags Flags;           // Settings + owner mode + interest mode
    uint32_t Scene;              // Scene sequence this object belongs to

    ObjectOwnerIntent OwnerIntent;
    double OwnerIntentCooldown;

    Tick RemoteTickSent;
    Tick RemoteTickAcked;

    int32_t Status;              // OBJECT_STATUS_NEW / PENDING / CREATED
    int32_t UpdatesReceived;
    int32_t UpdatesInFlight;

    int32_t PluginVersion;
    int32_t ClientVersion;
    int32_t ClientBaseVersion;

    std::vector<ObjectId> SubObjects;
    NetworkedStringHeap StringHeap;

    static bool Is(const Object* obj);
    static ObjectRoot* Cast(Object* obj);
    ObjectRoot* Root() override;   // Returns this
};
```

**Lifecycle status constants:**

| Constant | Value | Meaning |
|----------|-------|---------|
| `OBJECT_STATUS_NEW` | 0 | Just allocated, not yet sent to server |
| `OBJECT_STATUS_PENDING` | 1 | Sent to server, waiting for ack |
| `OBJECT_STATUS_CREATED` | 2 | Fully synchronized |

### ObjectChild

A sub-object attached to a root. Shares the root's StringHeap and authority, but has its own independent Words buffer and ObjectId.

```cpp
class ObjectChild final : public Object {
public:
    ObjectId Parent;             // Root object's ObjectId
    uint32_t TargetObjectHash;   // Hash to match child type on remote
    int32_t SubObjectStatus;

    static ObjectId GetParent(const Object* obj);
    static bool Is(const Object* obj);
    static ObjectChild* Cast(Object* obj);

    ObjectRoot* Root() override;  // Returns parent root
};
```

`Root()` traverses up to the parent `ObjectRoot`, which the SDK uses for authority checks and StringHeap access.

## ObjectType Enum

```cpp
enum class ObjectType : uint8_t {
    Base  = 1,
    Child = 2,
    Root  = 3
};
```

Use the static `Is()` and `Cast()` methods on `ObjectRoot` and `ObjectChild` for safe type checks rather than comparing `ObjectType` directly.

## Words Buffer

The Words buffer is a flat array of `int32_t` values that holds all replicated property data for an object. Properties are serialized sequentially without type markers -- the integration layer and all clients must agree on the same layout.

```
+--------+--------+--------+-----+--------+-----------+
| Prop 0 | Prop 1 | Prop 2 | ... | Prop N | Tail (6w) |
+--------+--------+--------+-----+--------+-----------+
```

- Each property occupies a fixed number of words (e.g., `float` = 1, `Vector3` = 3, `StringHandle` = 2).
- Array properties use the layout `[count: 1 word] [elements...]`.
- The last 6 words are reserved for [ObjectTail](#objecttail) -- **never write to them**.

The total buffer size is: `sum(property_words) + 6 tail words`.

### Writing and Reading

The authority client writes property values into the Words buffer during the outbound sync phase. The SDK transmits only changed words (detected by comparing Words against Shadow). Remote clients read from their local copy of the Words buffer during the inbound sync phase.

```cpp
// Authority: write a float at word offset 0
obj->Words.Ptr[0] = *reinterpret_cast<int32_t*>(&my_float);

// Remote: read it back
float value = *reinterpret_cast<float*>(&obj->Words.Ptr[0]);
```

## Shadow Buffer

The Shadow buffer is a parallel array with the same size as Words. After the SDK transmits an update, it copies Words into Shadow. On the next frame, the SDK compares Words against Shadow to detect which words changed and need retransmission.

The engine integration should not modify Shadow directly.

## ObjectTail

The last 6 words of every Words buffer are reserved for the SDK's internal use:

```cpp
struct ObjectTail {
    int32_t AOI_X;        // Area-of-interest X coordinate
    int32_t AOI_Y;        // Area-of-interest Y coordinate
    int32_t AOI_Z;        // Area-of-interest Z coordinate
    int32_t AOI_SET;      // Whether AOI position has been set
    int32_t Destroyed;    // Destruction flag
    int32_t Dummy;        // Padding
};

static_assert(sizeof(ObjectTail) == 24);  // 6 words * 4 bytes
```

**Critical**: Writing to the tail area corrupts SDK state. The `Destroyed` field at word offset `-2` from the end is especially dangerous -- overwriting it prevents the object from being properly cleaned up.

When computing buffer sizes, always add `Object::ExtraTailWords` (6) to the sum of your property word counts:

```cpp
size_t total_words = sum_of_property_words + Object::ExtraTailWords;
```

## ObjectFlags

Combines three configuration values into a packed `uint32_t`:

```cpp
struct ObjectFlags {
    ObjectSettingsFlags SettingsFlags;
    ObjectOwnerModes OwnerMode;
    ObjectInterestModes InterestMode;
};
```

See [Ownership](ownership.md) and [Area of Interest](aoi.md) for details on the individual fields.

### ObjectSettingsFlags

```cpp
enum class ObjectSettingsFlags : uint8_t {
    None                    = 0,
    OwnerLeavesOwnerToNone  = 1 << 0,  // Reset owner when current owner disconnects
    IsGlobalInstance        = 1 << 1,  // Singleton object shared across scenes
};
```

### ObjectSpecialFlags

```cpp
enum class ObjectSpecialFlags : uint8_t {
    None                        = 0,
    IsRootTransform             = 1 << 1,
    IgnoreRootTransformProperties = 1 << 2,
};
```

Used internally for physics replication optimization.

## Object Lookup

The `Client` maintains two maps for object lookup:

```cpp
// All objects (roots + children)
std::unordered_map<ObjectId, Object*>& AllObjects();

// Root objects only
std::unordered_map<ObjectId, ObjectRoot*>& AllRootObjects();

// Single-object lookup
Object* FindObject(ObjectId id) const;
ObjectRoot* FindObjectRoot(ObjectId id) const;
Object* FindSubObjectWithHash(ObjectRoot* root, uint32_t subObjectHash) const;
```

## Object Lifecycle Summary

1. **Allocation** -- `Client` creates the object internally, assigns an `ObjectId`, allocates Words/Shadow buffers.
2. **Population** -- Integration layer writes initial state (spawn data) into the Words buffer.
3. **Activation** -- `SetHasValidData(true)` and `SetSendUpdates(true)` mark the object as ready for replication.
4. **Replication** -- Each frame, the SDK detects changes (Words vs Shadow) and transmits dirty words to other clients.
5. **Destruction** -- `DestroyObjectLocal()` marks the object for removal; `OnObjectDestroyed` fires on remote clients.

See [Object Creation](object-creation.md) for the three creation paths (spawned, scene, sub-object).

## Related

- [Architecture](architecture.md) -- Fundamental types and memory model
- [Object Creation](object-creation.md) -- CreateObject, CreateSceneObject, CreateSubObject
- [Serialization](serialization.md) -- Words buffer layout and encoding
- [String Heap](string-heap.md) -- NetworkedStringHeap for string replication
- [Ownership](ownership.md) -- Owner modes and authority
