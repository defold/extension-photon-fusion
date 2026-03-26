# Objects

Networked objects are the core data model in Fusion. Each object owns a fixed-size buffer of [Words](architecture.md#word) that is automatically replicated to other clients. The SDK provides a three-level class hierarchy for different object roles.

## Class Hierarchy

```
Object (abstract base)
  |
  +-- ObjectRoot    (top-level networked entity)
  |
  +-- ObjectChild   (sub-object attached to a root)
```

All three classes live in the `SharedMode` namespace.

## Object (Base)

The common base class. Holds the Words buffer, shadow state, type information, and SDK bookkeeping.

```cpp
class Object {
public:
    ObjectId Id;                       // Globally unique identifier
    void* Engine;                      // Opaque pointer for engine integration
    ObjectType ObjectType;             // Base, Root, or Child
    bool HasValidData;                 // Words buffer contains meaningful data
    Data Header;                       // Immutable spawn data
    TypeRef Type;                      // Type hash + word count

    BufferT<Word> Shadow;              // Last-acked state (for dirty detection)
    BufferT<Word> Words;               // Current replicated state

    ObjectSpecialFlags SpecialFlags;   // IsRootTransform, etc.
    uint8_t SendFlags;                 // Current packet flags

    Tick RemoteTickSent;               // Last tick we sent
    Tick RemoteTickAcked;              // Last tick remote acked

    int32_t Status;                    // NEW / PENDING / CREATED

    NetworkedStringHeap StringHeap;    // Per-object string pool (1024 bytes)

    static constexpr size_t ExtraTailWords = sizeof(ObjectTail) / 4;  // = 6
    static constexpr double DynamicOwnerCooldownTime = 1.0 / 3;      // ~333ms

    void SetHasValidData(bool hasValidData);
    void SetSendUpdates(bool sendUpdates);

    virtual ObjectRoot* Root() = 0;

    // String heap operations
    StringHandle AddString(const PhotonCommon::CharType* str);
    const PhotonCommon::CharType* ResolveString(const StringHandle& handle, StringMessage& outStatus);
    StringHandle FreeString(const StringHandle& handle);
    bool IsValidStringHandle(const StringHandle& handle);
    uint32_t GetStringLength(const StringHandle& handle);

    // Bandwidth tracking
    uint32_t GetBytesSendLastTick() const;
    uint32_t GetBytesReceivedLastTick() const;
    uint32_t ConsumeBytesSendLastTick();        // Read and reset
    uint32_t ConsumeBytesReceivedLastTick();     // Read and reset
    void ResetReceivedBytes();
};
```

### Key Fields

| Field | Type | Purpose |
|-------|------|---------|
| `Id` | `ObjectId` | Globally unique identifier (origin player + counter) |
| `Engine` | `void*` | Opaque pointer for engine integration to store its own data |
| `Words` | `BufferT<Word>` | Current replicated state buffer |
| `Shadow` | `BufferT<Word>` | Previous acked state for change detection |
| `Header` | `Data` | Immutable spawn data sent at creation time |
| `Type` | `TypeRef` | Type hash and total word count (including tail) |
| `HasValidData` | `bool` | Whether the Words buffer contains meaningful data |
| `Status` | `int32_t` | Lifecycle status (NEW, PENDING, CREATED) |
| `StringHeap` | `NetworkedStringHeap` | Per-object string pool |

## ObjectRoot

A top-level networked entity. Tracks ownership, timing, scene membership, and sub-objects.

```cpp
class ObjectRoot final : public Object {
public:
    int LocalSendRate;                 // Per-object send rate multiplier
    double Time;                       // Network-synchronized time
    PlayerId Owner;                    // Current owner's PlayerId
    ObjectOwnerModes OwnerMode;        // Transaction, Dynamic, MasterClient, etc.
    uint32_t Scene;                    // Scene sequence this object belongs to

    ObjectOwnerIntent OwnerIntent;     // WantOwner / DontWantOwner
    double OwnerIntentCooldown;        // Time remaining before next transfer

    int32_t UpdatesReceived;           // Count of received state updates
    bool SentThisFrame;                // Whether state was sent this frame
    bool ObjectReady;                  // All required objects are present

    int32_t PluginVersion;             // Server plugin protocol version
    int32_t ClientVersion;             // Client protocol version
    int32_t ClientBaseVersion;         // Base version for delta encoding

    std::vector<ObjectId> SubObjects;  // Attached child object IDs

    static bool Is(const Object* obj);
    static ObjectRoot* Cast(Object* obj);
    static const ObjectRoot* Cast(const Object* obj);

    bool IsRequired(ObjectId id) const;
    int32_t RequiredObjectsCount() const;
    ObjectId* RequiredObjects() const;

    ObjectRoot* Root() override;       // Returns this
};
```

## ObjectChild

A sub-object attached to a root. Shares the root's authority but has its own independent Words buffer and ObjectId.

```cpp
class ObjectChild final : public Object {
public:
    ObjectId Parent;                   // Root object's ObjectId
    uint32_t TargetObjectHash;         // Hash to match child type on remote

    static ObjectId GetParent(const Object* obj);
    static bool Is(const Object* obj);
    static ObjectChild* Cast(Object* obj);
    static const ObjectChild* Cast(const Object* obj);

    ObjectRoot* Root() override;       // Navigates to parent root
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

Use the static `Is()` and `Cast()` methods on `ObjectRoot` and `ObjectChild` for safe type checks rather than comparing `ObjectType` directly:

```cpp
if (ObjectRoot::Is(obj)) {
    ObjectRoot* root = ObjectRoot::Cast(obj);
    // ...
}

if (ObjectChild::Is(obj)) {
    ObjectChild* child = ObjectChild::Cast(obj);
    ObjectId parentId = child->Parent;
}
```

## Words Buffer

The Words buffer is a flat array of `int32_t` values that holds all replicated property data for an object. Properties are serialized sequentially without type markers -- the integration layer and all clients must agree on the same layout.

```
+--------+--------+--------+-----+--------+-----------+
| Prop 0 | Prop 1 | Prop 2 | ... | Prop N | Tail (6w) |
+--------+--------+--------+-----+--------+-----------+
```

- Each property occupies a fixed number of words (e.g., `float` = 1, `Vector3` = 3, `StringHandle` = 2).
- The last 6 words are reserved for [ObjectTail](#objecttail) -- **never write to them**.

The total buffer size is: `sum(property_words) + ExtraTailWords`.

### Writing and Reading

The authority client writes property values into the Words buffer during the outbound sync phase. The SDK transmits only changed words (detected by comparing Words against Shadow). Remote clients read from their local copy of the Words buffer during the inbound sync phase.

```cpp
// Authority: write a float at word offset 0
memcpy(&obj->Words.Ptr[0], &my_float, sizeof(float));

// Remote: read it back
float value;
memcpy(&value, &obj->Words.Ptr[0], sizeof(float));
```

## Shadow Buffer

The Shadow buffer is a parallel array with the same size as Words. After the SDK transmits an update, it compares Words against Shadow to detect which words changed and need retransmission. After acknowledgment, Shadow is updated.

The engine integration should not modify Shadow directly.

## ObjectTail

The last 6 words of every Words buffer are reserved for the SDK's internal use:

```cpp
#pragma pack(push, 4)
struct ObjectTail {
    int32_t RequiredObjectsCount;   // Number of required object dependencies
    uint64_t InterestKey;           // AOI interest key (8 bytes, 2 words)
    int32_t Destroyed;              // Destruction flag
    int32_t SendRate;               // Server-controlled send rate
    int32_t Dummy;                  // Reserved padding
};
#pragma pack(pop)

static_assert(sizeof(ObjectTail) == 24);  // 6 words * 4 bytes
```

**Critical**: Writing to the tail area corrupts SDK state. The `Destroyed` field is especially dangerous -- overwriting it prevents proper cleanup. The `InterestKey` is managed by the [AOI system](aoi.md) via `SetGlobalInterestKey()`, `SetAreaInterestKey()`, and `SetUserInterestKey()`.

When computing buffer sizes, always add `Object::ExtraTailWords` (6):

```cpp
size_t total_words = sum_of_property_words + Object::ExtraTailWords;
```

## Status Lifecycle

```
NEW (0) --> PENDING (1) --> CREATED (2)
```

| Constant | Value | Meaning |
|----------|-------|---------|
| `OBJECT_STATUS_NEW` | 0 | Just allocated, not yet sent to server |
| `OBJECT_STATUS_PENDING` | 1 | Sent to server, awaiting acknowledgment |
| `OBJECT_STATUS_CREATED` | 2 | Fully synchronized and acknowledged |

Objects transition from NEW to PENDING when first included in an outgoing packet, and from PENDING to CREATED when the server acknowledges receipt.

## Bandwidth Tracking

Each object tracks bytes sent and received per frame:

```cpp
uint32_t sent = obj->ConsumeBytesSendLastTick();      // Read + reset
uint32_t recv = obj->ConsumeBytesReceivedLastTick();   // Read + reset
```

These values are useful for network debugging, performance monitors, and bandwidth allocation. The "Consume" variants read the value and reset it to zero atomically.

## Object Lookup

The `Client` maintains two maps for object lookup:

```cpp
// All objects (roots + children)
std::unordered_map<ObjectId, Object*>& client->AllObjects();

// Root objects only
std::unordered_map<ObjectId, ObjectRoot*>& client->AllRootObjects();

// Single-object lookup
Object* client->FindObject(ObjectId id);
ObjectRoot* client->FindObjectRoot(ObjectId id);
Object* client->FindSubObjectWithHash(ObjectRoot* root, uint32_t subObjectHash);
```

## Object Lifecycle Summary

```
1. Allocate   -- Client creates object, assigns ObjectId, allocates Words/Shadow
2. Populate   -- Integration writes initial state into Words buffer
3. Activate   -- SetHasValidData(true) + SetSendUpdates(true)
4. Replicate  -- Each frame: Words vs Shadow delta detection, dirty words sent
5. Ready      -- OnObjectReady fires when object + required objects are all CREATED
6. Destroy    -- DestroyObjectLocal() marks for removal; OnObjectDestroyed on remotes
```

See [Object Creation](object-creation.md) for the three creation paths.

## Related

- [Architecture](architecture.md) -- Fundamental types and memory model
- [Object Creation](object-creation.md) -- CreateObject, CreateSceneObject, CreateSubObject
- [Serialization](serialization.md) -- Words buffer layout and encoding
- [String Heap](string-heap.md) -- NetworkedStringHeap for string replication
- [Ownership](ownership.md) -- Owner modes and authority
- [AOI](aoi.md) -- InterestKey in ObjectTail
