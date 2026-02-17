# Object Creation

Fusion provides three distinct paths for creating networked objects, each suited to a different scenario. All three produce objects backed by a [Words buffer](objects.md) and replicated by the SDK, but they differ in ID assignment, lifetime, and creation flow.

| Path | Method | ID Assignment | Use Case |
|------|--------|---------------|----------|
| Spawned | `CreateObject` | Auto-incremented per client | Player avatars, projectiles, pickups |
| Scene | `CreateSceneObject` | Deterministic (hash of name) | Level geometry, doors, switches |
| Sub-object | `CreateSubObject` + `AddSubObject` | Explicit or auto | Inventory items, attachments, weapon mods |

## Spawned Objects (CreateObject)

Spawned objects are the most common path. The creating client provides a type descriptor and header blob; the SDK assigns a unique `ObjectId` and replicates the object to all interested clients.

```cpp
ObjectRoot* Client::CreateObject(
    size_t          words,          // Word count (user data + ExtraTailWords)
    const TypeRef&  type,           // { Hash, WordCount } for type identification
    const CharType* header,         // Opaque header blob (scene path, config, etc.)
    size_t          headerLength,
    uint32_t        scene,          // Scene sequence this object belongs to
    ObjectFlags     objectFlags     // Owner mode, interest mode, settings
);
```

### ObjectId

Every object receives an `ObjectId` composed of two fields:

```cpp
struct ObjectId {
    PlayerId Origin;    // Player who created the object
    uint32_t Counter;   // Monotonically increasing per-player counter
};
```

The SDK auto-assigns IDs via `GetNewObjectId()`. The `Origin` field enables conflict-free creation across clients without coordination.

### Header

The `header` parameter is an opaque byte blob stored alongside the object. Remote clients receive it in the `OnObjectCreated` callback and use it to determine which scene to instantiate. Typical contents include a scene resource path or a type hash. The SDK does not interpret the header -- it is passed through as-is.

### Remote Creation Callback

When a remote client creates a spawned object, the SDK fires:

```cpp
std::function<void(ObjectRoot*)> OnObjectCreated;
```

The callback receives the fully initialized `ObjectRoot` with `Header`, `Type`, and `Flags` populated. The integration layer reads the header, instantiates the appropriate scene, and begins synchronization.

### Destruction

The owner destroys a spawned object by calling:

```cpp
bool Client::DestroyObjectLocal(ObjectRoot* obj, bool engineObjectAlreadyDestroyed);
```

Set `engineObjectAlreadyDestroyed` to `true` if the engine-side node has already been freed (e.g., `queue_free()` was called first). The SDK broadcasts the destruction to all clients. Remote clients receive:

```cpp
std::function<void(const ObjectRoot*, DestroyModes)> OnObjectDestroyed;
```

### DestroyModes

```cpp
enum class DestroyModes {
    Local      = 0,   // Owner explicitly destroyed the object
    Remote     = 1,   // Destruction replicated from owner
    SceneChange = 2,  // Object destroyed due to scene transition
    Shutdown   = 3,   // Client shutting down
};
```

## Scene Objects (CreateSceneObject)

Scene objects represent entities that exist in the level itself -- they are not spawned dynamically but are part of the loaded scene. Every client creates the same scene objects locally; the SDK reconciles them using deterministic IDs.

```cpp
ObjectRoot* Client::CreateSceneObject(
    bool&           alreadyPopulated,   // [out] true if network data exists
    size_t          words,
    const TypeRef&  type,
    const CharType* header,
    size_t          headerLength,
    uint32_t        scene,              // Scene sequence
    uint32_t        id,                 // Deterministic object ID (e.g., hash of node name)
    ObjectFlags     objectFlags
);
```

### The alreadyPopulated Pattern

The critical difference from `CreateObject` is the `alreadyPopulated` output parameter:

- **`false`** -- This client is the first to create the object (typically the master client). Engine defaults should be serialized into the Words buffer.
- **`true`** -- Another client already populated the object. The Words buffer contains network data that should be deserialized into the engine state.

This eliminates the need for a separate "spawn data" exchange for scene objects.

### Deterministic IDs

Scene objects use a deterministic `id` parameter instead of auto-assigned IDs. All clients loading the same scene must produce the same ID for the same object. A common approach is hashing the object's name or path:

```cpp
uint32_t id = hash(rootNodeName);
```

### Global Instance Objects

A variant for singleton objects that persist across scenes:

```cpp
ObjectRoot* Client::CreateGlobalInstanceObject(
    bool&           alreadyPopulated,
    size_t          words,
    const TypeRef&  type,
    const CharType* header,
    size_t          headerLength,
    uint32_t        scene,
    uint32_t        id,
    ObjectFlags     objectFlags
);
```

Global instance objects use the `ObjectSettingsFlags::IsGlobalInstance` flag and survive scene transitions.

## Sub-Objects (CreateSubObject + AddSubObject)

Sub-objects are child objects attached to an existing root object. They share the root's `NetworkedStringHeap` and authority, but have their own `ObjectId`, Words buffer, and synchronization state.

```cpp
ObjectChild* Client::CreateSubObject(
    ObjectId            parent,             // Parent root's ObjectId
    size_t              words,
    const TypeRef&      type,
    const CharType*     header,
    size_t              headerLength,
    uint32_t            targetObjectHash,   // Type identifier for remote matching
    ObjectId            id,                 // Explicit child ID
    ObjectSpecialFlags  SpecialFlags
);

bool Client::AddSubObject(ObjectRoot* ParentObject, ObjectChild* SubObject);
```

### Creation Flow

1. Create the child: `CreateSubObject(parentId, words, type, header, headerLength, typeHash, childId, flags)`
2. Write initial state into `SubObject->Words`
3. Attach to parent: `AddSubObject(parent, child)`

`AddSubObject` must be called after the child's Words buffer is populated. Once added, the SDK replicates the child to remote clients.

### Remote Sub-Object Callback

```cpp
std::function<void(ObjectChild*)> OnSubObjectCreated;
```

Remote clients receive the `ObjectChild` with its `TargetObjectHash` and `Parent` ID. The integration layer matches `TargetObjectHash` to determine the child's type, instantiates the scene, and deserializes the Words data.

### Querying Sub-Objects

```cpp
bool Client::HasSubObjects(const Object* Root);
const std::vector<ObjectId>& Client::GetSubObject(const Object* Root);
Object* Client::FindSubObjectWithHash(ObjectRoot* Root, uint32_t subObjectHash) const;
```

### ObjectChild Class

```cpp
class ObjectChild final : public Object {
public:
    ObjectId Parent{0, 0};
    uint32_t TargetObjectHash{0};

    static ObjectId GetParent(const Object* obj);
    static bool Is(const Object* obj);
    static ObjectChild* Cast(Object* obj);

    ObjectRoot* Root() override;  // Navigates to parent root
};
```

Sub-objects delegate string operations to `Root()->StringHeap`, so all string handles within a hierarchy share the same heap.

## ObjectFlags

All creation paths accept `ObjectFlags` to configure behavior:

```cpp
struct ObjectFlags {
    ObjectSettingsFlags SettingsFlags;   // OwnerLeavesOwnerToNone, IsGlobalInstance
    ObjectOwnerModes    OwnerMode;      // Transaction, Dynamic, MasterClient
    ObjectInterestModes InterestMode;   // All, Area, Assigned
};
```

See [Ownership](ownership.md) for owner modes and [Area of Interest](aoi.md) for interest modes.

## ObjectSpecialFlags

Sub-objects support additional flags:

```cpp
enum class ObjectSpecialFlags : uint8_t {
    None                        = 0,
    IsRootTransform             = 1 << 1,
    IgnoreRootTransformProperties = 1 << 2,
};
```

## Finding Objects

```cpp
Object*     Client::FindObject(ObjectId id) const;       // Any object (root or child)
ObjectRoot* Client::FindObjectRoot(ObjectId id) const;    // Root objects only

// All objects (including children)
std::unordered_map<ObjectId, Object*>& Client::AllObjects();

// Root objects only
std::unordered_map<ObjectId, ObjectRoot*>& Client::AllRootObjects();
```

## TypeRef

Every object carries a `TypeRef` used for type identification and buffer sizing:

```cpp
struct TypeRef {
    uint64_t Hash;       // CRC64 or engine-specific hash
    uint32_t WordCount;  // Total words including tail
};
```

The `WordCount` must include `Object::ExtraTailWords` (6 words for `ObjectTail`). The `Hash` must match between creator and consumer for remote instantiation to succeed.

## See Also

- [Objects](objects.md) -- Object hierarchy and Words buffer layout
- [Ownership](ownership.md) -- Owner modes and authority transfer
- [Serialization](serialization.md) -- Words buffer and type-to-word mapping
- [Scene Management](scene-management.md) -- Scene sequences and `ChangeScene()`
- [Client API Reference](../reference/client-api.md) -- Full method reference
