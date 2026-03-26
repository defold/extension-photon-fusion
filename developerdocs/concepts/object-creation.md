# Object Creation

Fusion provides three distinct paths for creating networked objects, plus a two-step process for sub-objects. All paths produce objects backed by a [Words buffer](objects.md) and replicated by the SDK, but they differ in ID assignment, lifetime, and creation flow.

| Path | Method | ID Assignment | Use Case |
|------|--------|---------------|----------|
| Dynamic | `CreateObject` | Auto-incremented per client | Player avatars, projectiles, pickups |
| Scene | `CreateSceneObject` | Deterministic (scene + id) | Level geometry, doors, switches |
| Global Instance | `CreateGlobalInstanceObject` | Deterministic (singleton) | Game managers, scoreboards |
| Sub-object | `CreateSubObject` + `AddSubObject` | Explicit | Inventory items, attachments |

## Dynamic Objects (CreateObject)

Dynamic objects are the most common path. The creating client provides a type descriptor and header blob; the SDK assigns a unique `ObjectId` and replicates the object to all interested clients.

```cpp
ObjectRoot* Client::CreateObject(
    size_t              words,       // Word count (user data + ExtraTailWords)
    const TypeRef&      type,        // { Hash, WordCount } for type identification
    const CharType*     header,      // Opaque header blob (scene path, config, etc.)
    size_t              headerLength,
    uint32_t            scene,       // Scene sequence this object belongs to
    ObjectOwnerModes    ownerMode,   // Ownership behavior
    int32_t             requiredObjectsCount = 0  // Dependencies before ready
);
```

### ObjectId Assignment

```cpp
ObjectId Client::GetNewObjectId();
```

Every object receives an `ObjectId` composed of two fields:

```cpp
struct ObjectId {
    PlayerId Origin;    // Player who created the object
    uint32_t Counter;   // Monotonically increasing per-player counter
};
```

The SDK auto-assigns IDs via `GetNewObjectId()`. The `Origin` field enables conflict-free creation across clients without coordination.

### Header

The `header` parameter is an opaque byte blob stored alongside the object. Remote clients receive it in the `OnObjectReady` callback and use it to determine which scene to instantiate. Typical contents include a scene resource path or a type hash. The SDK does not interpret the header -- it passes it through as-is.

### Remote Object Ready

When a remote client's object becomes fully synchronized, the SDK fires:

```cpp
Broadcaster<void(ObjectRoot*)> OnObjectReady;
```

The callback receives the fully initialized `ObjectRoot` with `Header`, `Type`, and `OwnerMode` populated. The `ObjectReady` flag on the root is set to `true`. The integration layer reads the header, instantiates the appropriate scene, and begins synchronization.

An object becomes "ready" when it reaches `OBJECT_STATUS_CREATED` and all of its required objects (specified by `requiredObjectsCount`) are also created and present.

### Destruction

The owner destroys a dynamic object by calling:

```cpp
bool Client::DestroyObjectLocal(ObjectRoot* obj, bool engineObjectAlreadyDestroyed);
```

Set `engineObjectAlreadyDestroyed` to `true` if the engine-side entity has already been freed. The SDK broadcasts the destruction to all clients:

```cpp
Broadcaster<void(const ObjectRoot*, DestroyModes)> OnObjectDestroyed;
```

### DestroyModes

```cpp
enum class DestroyModes {
    Local           = 0,   // Owner explicitly destroyed
    Remote          = 1,   // Destruction replicated from owner
    SceneChange     = 2,   // Destroyed due to scene transition
    Shutdown        = 3,   // Client shutting down
    RejectedNotOwner = 4,  // Server rejected: not the owner
    ForceDestroy    = 5,   // Server forced destruction
};
```

## Scene Objects (CreateSceneObject)

Scene objects represent entities that exist in the level itself -- not spawned dynamically but part of the loaded scene. Every client creates the same scene objects locally; the SDK reconciles them using deterministic IDs.

```cpp
ObjectRoot* Client::CreateSceneObject(
    bool&               alreadyPopulated,  // [out] true if network data exists
    size_t              words,
    const TypeRef&      type,
    const CharType*     header,
    size_t              headerLength,
    uint32_t            scene,             // Scene sequence
    uint32_t            id,                // Deterministic object ID
    ObjectOwnerModes    ownerMode,
    int32_t             requiredObjectsCount = 0
);
```

### The alreadyPopulated Pattern

The critical difference from `CreateObject` is the `alreadyPopulated` output parameter:

- **`false`** -- This client is the first to create the object (typically the master client). Engine defaults should be serialized into the Words buffer.
- **`true`** -- Another client already populated the object. The Words buffer contains network data that should be deserialized into the engine state.

This bidirectional flow eliminates the need for separate "spawn data" exchange for scene objects.

### Deterministic IDs

Scene objects use a deterministic `id` parameter instead of auto-assigned IDs. All clients loading the same scene must produce the same ID for the same object. A common approach is hashing the object's name or path:

```cpp
uint32_t id = CRC64(rootNodeName);
```

## Global Instance Objects (CreateGlobalInstanceObject)

A variant for singleton objects that persist across scene changes:

```cpp
ObjectRoot* Client::CreateGlobalInstanceObject(
    bool&               alreadyPopulated,
    size_t              words,
    const TypeRef&      type,
    const CharType*     header,
    size_t              headerLength,
    uint32_t            scene,
    uint32_t            id,
    ObjectOwnerModes    ownerMode,
    int32_t             requiredObjectsCount = 0
);
```

Global instance objects survive scene transitions and maintain their state across `ChangeScene()` calls. They follow the same `alreadyPopulated` pattern as scene objects.

## Sub-Objects (CreateSubObject + AddSubObject)

Sub-objects are child objects attached to an existing root object. They share the root's authority but have their own `ObjectId`, Words buffer, and synchronization state.

### Two-Step Creation

```cpp
// Step 1: Create the child
ObjectChild* Client::CreateSubObject(
    ObjectId            parent,            // Parent root's ObjectId
    size_t              words,
    const TypeRef&      type,
    const CharType*     header,
    size_t              headerLength,
    uint32_t            targetObjectHash,  // Type identifier for remote matching
    ObjectId            id,                // Explicit child ID
    ObjectSpecialFlags  SpecialFlags
);

// Step 2: Attach to parent
bool Client::AddSubObject(ObjectRoot* ParentObject, ObjectChild* SubObject);
```

### Creation Flow

```cpp
// 1. Create the child
ObjectChild* child = client->CreateSubObject(
    parentId, words, type,
    header, headerLength,
    typeHash, childId, ObjectSpecialFlags::None
);

// 2. Write initial state into child Words
child->SetSendUpdates(false);
write_initial_state(child);

// 3. Attach to parent
client->AddSubObject(parentRoot, child);

// 4. Activate
child->SetSendUpdates(true);
child->SetHasValidData(true);
```

`AddSubObject` must be called after the child's Words buffer is populated. Once added, the SDK replicates the child to remote clients.

### Remote Sub-Object Callback

```cpp
Broadcaster<void(ObjectChild*)> OnSubObjectCreated;
```

Remote clients receive the `ObjectChild` with its `TargetObjectHash` and `Parent` ID. The integration layer matches `TargetObjectHash` to determine the child's type.

### Sub-Object Destruction

```cpp
bool Client::DestroySubObjectLocal(ObjectChild* obj);

Broadcaster<void(ObjectChild*, DestroyModes)> OnSubObjectDestroyed;
```

### Querying Sub-Objects

```cpp
bool Client::HasSubObjects(const Object* Root);
const std::vector<ObjectId>& Client::GetSubObject(const Object* Root);
Object* Client::FindSubObjectWithHash(ObjectRoot* Root, uint32_t subObjectHash);
```

## Word Count Calculation

The `words` parameter in all creation methods must include both user data words and the SDK-reserved tail:

```cpp
size_t total_words = user_property_words + Object::ExtraTailWords;  // ExtraTailWords = 6
```

The `ExtraTailWords` account for the [ObjectTail](objects.md#objecttail) structure (RequiredObjectsCount, InterestKey, Destroyed, SendRate, Dummy).

## TypeRef Convention

```cpp
struct TypeRef {
    uint64_t Hash;       // CRC64 or engine-specific hash
    uint32_t WordCount;  // Total words including tail
};
```

The `Hash` must match between creator and consumer for remote instantiation to succeed. The `WordCount` should equal the `words` parameter passed to the creation method.

## Activation Pattern

After creating an object and writing initial state:

```cpp
// Pause sending during initial population
obj->SetSendUpdates(false);

// Write all initial properties via memcpy into Words buffer
populate_words(obj);

// Resume sending and mark data as valid
obj->SetSendUpdates(true);
obj->SetHasValidData(true);
```

This prevents the SDK from sending a partially-populated object. The `SetSendUpdates(false)` call ensures no outgoing packet includes this object until all properties are written.

## Required Objects

The `requiredObjectsCount` parameter specifies how many other objects must be present before the `OnObjectReady` callback fires. Required object IDs are stored in the [ObjectTail](objects.md#objecttail) at the end of the Words buffer, just before the standard tail fields.

```cpp
// Query required objects
int32_t count = root->RequiredObjectsCount();
ObjectId* ids = root->RequiredObjects();
bool isReq = root->IsRequired(someId);
```

The total word count must account for required objects:

```cpp
size_t total = user_words + Object::ExtraTailWords;
// requiredObjectsCount is passed separately, not added to word count
```

## Related

- [Objects](objects.md) -- Object hierarchy, Words buffer layout, ObjectTail
- [Ownership](ownership.md) -- ObjectOwnerModes and authority transfer
- [Serialization](serialization.md) -- Words buffer and type-to-word mapping
- [Scene Management](scene-management.md) -- Scene sequences and `ChangeScene()`
- [String Heap](string-heap.md) -- Spawn data exception for strings
