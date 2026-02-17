# Engine Binding

This guide covers the patterns for connecting Fusion SDK objects to engine-side representations: the `Object::Engine` pointer, bidirectional registry design, type descriptor and spawnable type registration, and the spawner/factory pattern.

These patterns are engine-agnostic. We show the generic C++ approach, then reference how the Godot and Unreal integrations implement each pattern.

## Object::Engine Pointer

Every `Object` (both `ObjectRoot` and `ObjectChild`) has a `void* Engine` field:

```cpp
class Object {
public:
    void* Engine{nullptr};  // Your engine-side pointer
    // ...
};
```

This is the SDK's designated place to store a back-reference to your engine representation (node, actor, entity, etc.). The SDK never reads or writes `Engine` -- it is entirely for your use.

```cpp
// After creating a Fusion object, store the engine pointer
SharedMode::ObjectRoot* fusionObj = client->CreateObject(/*...*/);
fusionObj->Engine = myEngineNode;

// Later, retrieve it in callbacks
client->OnObjectOwnerChanged = [](SharedMode::ObjectRoot* obj) {
    auto* node = static_cast<MyEngineNode*>(obj->Engine);
    node->OnAuthorityChanged();
};
```

**Lifetime note:** You must null out `Engine` when the engine-side object is destroyed, and check for null before dereferencing. The Fusion object may outlive the engine representation (e.g., during scene transitions).

## Bidirectional Registry Pattern

The `Engine` pointer gives you Fusion-to-engine lookup. But you also need engine-to-Fusion lookup: given an engine object, find its Fusion counterpart.

The solution is a bidirectional registry that maps between engine object IDs and Fusion `ObjectId`s.

### Design

```cpp
class ObjectRegistry {
    // Engine ID -> packed Fusion ID
    std::unordered_map<uint64_t, uint64_t> engineToFusion;
    // Packed Fusion ID -> Engine ID
    std::unordered_map<uint64_t, uint64_t> fusionToEngine;

public:
    void Register(uint64_t engineId, uint32_t origin, uint32_t counter);
    void UnregisterByEngineId(uint64_t engineId);
    void UnregisterByFusionId(uint32_t origin, uint32_t counter);

    bool HasEngineId(uint64_t engineId) const;
    bool HasFusionId(uint32_t origin, uint32_t counter) const;

    uint64_t GetEngineId(uint32_t origin, uint32_t counter) const;
    void GetFusionId(uint64_t engineId, uint32_t& origin, uint32_t& counter) const;

    void Clear();
};
```

### Packing ObjectId

Fusion's `ObjectId` has two components: `Origin` (PlayerId, uint32) and `Counter` (uint32). Pack them into a single `uint64_t` for use as a map key:

```cpp
struct PackedObjectId {
    uint64_t value = 0;

    PackedObjectId() = default;

    PackedObjectId(uint32_t origin, uint32_t counter) {
        // Low 32 bits = Origin, High 32 bits = Counter
        value = (static_cast<uint64_t>(counter) << 32) |
                 static_cast<uint32_t>(origin);
    }

    void Unpack(uint32_t& origin, uint32_t& counter) const {
        origin = static_cast<uint32_t>(value & 0xFFFFFFFF);
        counter = static_cast<uint32_t>(value >> 32);
    }
};
```

### Registration Points

Register objects at these points in the lifecycle:

| Event | Action |
|-------|--------|
| After `CreateObject()` + scene instantiation | `Register(engineId, obj->Id.Origin, obj->Id.Counter)` |
| In `OnObjectCreated` callback | `Register(engineId, obj->Id.Origin, obj->Id.Counter)` |
| After `CreateSceneObject()` | `Register(engineId, obj->Id.Origin, obj->Id.Counter)` |
| After `CreateSubObject()` + `AddSubObject()` | `Register(engineId, child->Id.Origin, child->Id.Counter)` |
| In `OnSubObjectCreated` callback | `Register(engineId, child->Id.Origin, child->Id.Counter)` |
| In `OnObjectDestroyed` callback | `UnregisterByFusionId(obj->Id.Origin, obj->Id.Counter)` |
| When engine object is freed | `UnregisterByEngineId(engineId)` |

### Usage: RPC Routing

The registry enables routing incoming RPCs to the correct engine object:

```cpp
client->OnRpc = [&](SharedMode::Rpc& rpc) {
    if (rpc.TargetObject.IsSome()) {
        // Object-targeted RPC
        uint64_t engineId = registry.GetEngineId(
            rpc.TargetObject.Origin, rpc.TargetObject.Counter);
        auto* node = GetEngineObject(engineId);
        if (node) {
            DispatchRpc(node, rpc);
        }
    } else {
        // Broadcast RPC (TargetObject is {0, 0})
        DispatchBroadcastRpc(rpc);
    }
};
```

### Usage: Sub-Object Parent Lookup

When a sub-object arrives, find its parent's engine representation:

```cpp
client->OnSubObjectCreated = [&](SharedMode::ObjectChild* child) {
    uint64_t parentEngineId = registry.GetEngineId(
        child->Parent.Origin, child->Parent.Counter);
    auto* parentNode = GetEngineObject(parentEngineId);
    // ...
};
```

### Metadata Extension

The registry can store additional metadata per object for spawn/despawn:

```cpp
struct ObjectMetadata {
    uint32_t sceneIndex = 0;
    uint64_t typeHash = 0;
    std::string scenePath;
};

// Extended registration
std::unordered_map<uint64_t, ObjectMetadata> metadata; // keyed by engine ID

void RegisterWithMetadata(uint64_t engineId, uint32_t origin, uint32_t counter,
                          uint64_t typeHash, const std::string& scenePath) {
    Register(engineId, origin, counter);
    metadata[engineId] = {0, typeHash, scenePath};
}
```

**Godot integration**: `FusionObjectRegistry` implements this exact pattern with `register_object_with_metadata()` storing `scene_index`, `type_hash`, and `scene_path`.

## Type Descriptor / Spawnable Type Registration

When a remote client creates an object, the local client needs to know which scene/prefab to instantiate. This is solved by a **spawnable type registry** that maps type hashes to scene resources.

### Type Hash Convention

A type hash is a `uint64_t` that uniquely identifies a scene/prefab type. The convention is to hash the resource path:

```cpp
// Godot: use String::hash() of the .tscn path
uint64_t typeHash = scenePath.hash();

// Generic: use CRC64 from the SDK
uint64_t typeHash = SharedMode::CRC64(path.c_str(), path.length());
```

The type hash is stored in `TypeRef::Hash` and set at object creation time. Remote clients receive it via `ObjectRoot::Type.Hash` in the `OnObjectCreated` callback.

### Registration

Each spawner maintains a list of spawnable scenes:

```cpp
struct SpawnableType {
    std::string scenePath;
    uint64_t typeHash;
    // Cached scene resource (lazy-loaded)
    void* cachedScene = nullptr;
};

std::vector<SpawnableType> spawnableTypes;

void RegisterSpawnableScene(const std::string& path) {
    SpawnableType type;
    type.scenePath = path;
    type.typeHash = HashPath(path);
    spawnableTypes.push_back(type);
}
```

### Matching on Remote Creation

When `OnObjectCreated` fires, find the matching spawnable type:

```cpp
SpawnableType* FindByTypeHash(uint64_t hash) {
    for (auto& type : spawnableTypes) {
        if (type.typeHash == hash) {
            return &type;
        }
    }
    return nullptr;
}

client->OnObjectCreated = [&](SharedMode::ObjectRoot* obj) {
    SpawnableType* type = FindByTypeHash(obj->Type.Hash);
    if (!type) {
        printf("Unknown object type: %llu\n", obj->Type.Hash);
        return;
    }

    // Load and instantiate the scene
    auto* instance = InstantiateScene(type->scenePath);

    // Deserialize spawn data from Header
    DeserializeSpawnData(instance, obj->Header);

    // Register in the bidirectional registry
    registry.Register(GetEngineId(instance), obj->Id.Origin, obj->Id.Counter);

    // Store engine pointer on Fusion object
    obj->Engine = instance;
};
```

### TypeRef Structure

```cpp
struct TypeRef {
    uint64_t Hash;        // Type identifier (scene path hash)
    uint32_t WordCount;   // Total Words buffer size (including tail)
};
```

`TypeRef` is passed to `CreateObject()`, `CreateSceneObject()`, and `CreateSubObject()`. It is available on remote objects via `Object::Type`.

## Spawner / Factory Pattern

A spawner is the engine-side component that manages the lifecycle of networked objects: registration, local spawning, remote instantiation, and despawning.

### Spawner Responsibilities

| Responsibility | When |
|----------------|------|
| Register spawnable scenes | At initialization / enter tree |
| Local spawn: instantiate + `CreateObject()` + serialize + register | When authority wants to create an object |
| Remote spawn: handle `OnObjectCreated` + instantiate + deserialize + register | When notified of remote creation |
| Despawn: `DestroyObjectLocal()` + free engine object + unregister | When authority wants to remove an object |
| Remote despawn: handle `OnObjectDestroyed` + free engine object + unregister | When notified of remote destruction |

### Local Spawn Flow

```
┌──────────────────────────────────────────────────────────────────┐
│ 1. Load scene, instantiate off-tree                              │
│ 2. Apply initial data (position, properties, etc.)               │
│ 3. Find FusionSynchronizer, compute word count                   │
│ 4. Serialize spawn-time properties to header (PackedByteArray)   │
│ 5. Call CreateObject(totalWords, typeRef, header, ...)           │
│ 6. SetSendUpdates(false) on the new object                      │
│ 7. Copy serialized data to Words buffer via memcpy               │
│ 8. Bind synchronizer to Fusion object                            │
│ 9. Register in object registry                                   │
│ 10. Add instance to scene tree                                    │
│ 11. SetSendUpdates(true), SetHasValidData(true)                  │
└──────────────────────────────────────────────────────────────────┘
```

```cpp
void* SpawnLocal(const std::string& scenePath, /* initial data */) {
    // 1-2. Instantiate and configure
    auto* instance = InstantiateScene(scenePath);
    ApplyInitialData(instance, /* ... */);

    // 3. Compute word count
    int userWords = ComputeWordCount(instance);

    // 4. Serialize spawn data
    std::vector<int32_t> spawnData = SerializeSpawnData(instance);

    // 5. Create Fusion object
    constexpr size_t tailWords = sizeof(SharedMode::ObjectTail) / sizeof(int32_t);
    size_t totalWords = userWords + tailWords;

    SharedMode::TypeRef typeRef{HashPath(scenePath), static_cast<uint32_t>(totalWords)};
    SharedMode::ObjectFlags flags(
        SharedMode::ObjectSettingsFlags::None,
        SharedMode::ObjectOwnerModes::Transaction,
        SharedMode::ObjectInterestModes::All);

    SharedMode::ObjectRoot* obj = client->CreateObject(
        totalWords, typeRef,
        reinterpret_cast<const SharedMode::CharType*>(spawnData.data()),
        spawnData.size() * sizeof(int32_t),
        0, flags);

    // 6. Disable sending until ready
    obj->SetSendUpdates(false);

    // 7. Copy spawn data to Words buffer
    int usable = static_cast<int>(obj->Words.Length) - tailWords;
    int toCopy = std::min(static_cast<int>(spawnData.size()), usable);
    memcpy(obj->Words.Ptr, spawnData.data(), toCopy * sizeof(int32_t));

    // 8-9. Bind and register
    BindSynchronizer(instance, obj);
    registry.Register(GetEngineId(instance), obj->Id.Origin, obj->Id.Counter);
    obj->Engine = instance;

    // 10. Add to scene tree
    AddToSceneTree(instance);

    // 11. Enable sending
    obj->SetSendUpdates(true);
    obj->SetHasValidData(true);

    return instance;
}
```

### Remote Spawn Flow

```
┌──────────────────────────────────────────────────────────────────┐
│ 1. OnObjectCreated callback fires with ObjectRoot*               │
│ 2. Match Type.Hash to registered spawnable scene                 │
│ 3. Load and instantiate the scene                                │
│ 4. Deserialize spawn data from obj->Header                       │
│ 5. Bind synchronizer to Fusion object                            │
│ 6. Copy Words buffer to engine properties (deserialize_spawn_data)│
│ 7. Register in object registry                                   │
│ 8. Add instance to scene tree                                    │
│ 9. SetHasValidData(true)                                         │
└──────────────────────────────────────────────────────────────────┘
```

### Despawn

```cpp
void Despawn(void* engineInstance) {
    uint64_t engineId = GetEngineId(engineInstance);

    uint32_t origin, counter;
    registry.GetFusionId(engineId, origin, counter);

    SharedMode::ObjectId fusionId(origin, counter);
    SharedMode::ObjectRoot* obj = client->FindObjectRoot(fusionId);

    if (obj) {
        // Destroy the Fusion object
        // Second param: false = engine object NOT already destroyed
        client->DestroyObjectLocal(obj, false);
    }

    // The OnObjectDestroyed callback handles cleanup
}
```

`DestroyObjectLocal` signature:

```cpp
bool DestroyObjectLocal(ObjectRoot* obj, bool engineObjectAlreadyDestroyed);
```

The `engineObjectAlreadyDestroyed` parameter tells the SDK whether the engine-side object is already gone (e.g., the user called `queue_free()` before `despawn()`). If `true`, the `OnObjectDestroyed` callback should skip freeing the engine object.

### DestroyModes

The `OnObjectDestroyed` callback includes a `DestroyModes` enum indicating why the object was destroyed:

```cpp
enum class DestroyModes {
    Local = 0,        // DestroyObjectLocal() was called
    Remote = 1,       // Remote authority destroyed it
    SceneChange = 2,  // Scene changed, object cleaned up
    Shutdown = 3      // Client shutting down
};
```

## Multiple Spawner Support

An integration can support multiple spawners, each managing different object types. The pattern:

1. Each spawner registers its own set of spawnable scenes
2. On remote creation, broadcast to all spawners -- only the one with a matching type hash handles it
3. Spawners self-register with the client (via `register_spawner()` / `unregister_spawner()`)

```cpp
class SpawnerManager {
    std::set<Spawner*> spawners;

public:
    void Register(Spawner* s) { spawners.insert(s); }
    void Unregister(Spawner* s) { spawners.erase(s); }

    void OnRemoteObjectCreated(SharedMode::ObjectRoot* obj) {
        for (auto* spawner : spawners) {
            spawner->TryHandleRemoteCreation(obj);
        }
    }

    void OnRemoteObjectDestroyed(const SharedMode::ObjectRoot* obj, int mode) {
        for (auto* spawner : spawners) {
            spawner->TryHandleRemoteDestruction(obj, mode);
        }
    }
};
```

**Godot integration**: `FusionClient` maintains a `std::unordered_set<FusionSpawner*>` and iterates all spawners in `_on_object_created()`. Each `FusionSpawner` checks if `obj->Type.Hash` matches any of its registered scenes.

## Synchronizer Registration

Synchronizers (the components that handle per-frame Words buffer sync) also register with the client:

```cpp
class SyncManager {
    std::set<Synchronizer*> synchronizers;

public:
    void Register(Synchronizer* s) { synchronizers.insert(s); }
    void Unregister(Synchronizer* s) { synchronizers.erase(s); }

    void SyncOutboundAll() {
        for (auto* sync : synchronizers) {
            if (sync->HasAuthority()) {
                sync->SyncOutbound();
            }
        }
    }

    void SyncInboundAll() {
        for (auto* sync : synchronizers) {
            sync->SyncInbound();
        }
    }
};
```

The frame loop (from [Getting Started](getting-started.md)) calls `SyncOutboundAll()` before `UpdateFrameEnd()` and `SyncInboundAll()` after `UpdateFrameBegin()`.

## Integration Entry Point

The plugin's entry point registers all classes and creates the singleton client:

```cpp
// Pseudocode for a GDExtension-style plugin
void InitializePlugin() {
    // 1. Register settings
    RegisterProjectSettings();

    // 2. Register classes
    RegisterClass<ReplicationConfig>();
    RegisterClass<Spawner>();
    RegisterClass<Synchronizer>();
    RegisterClass<FusionClient>();

    // 3. Create singleton
    auto* singleton = new FusionClient();
    RegisterSingleton("FusionClient", singleton);
}

void UninitializePlugin() {
    UnregisterSingleton("FusionClient");
    delete g_fusionClient;
}
```

**Godot integration**: `register_types.cpp` handles this via `GDREGISTER_CLASS` macros and `Engine::get_singleton()->register_singleton()`.

## Summary: Complete Integration Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Your Engine                              │
│                                                                 │
│  ┌──────────┐  ┌──────────────┐  ┌────────────────────┐        │
│  │ Spawner  │  │ Synchronizer │  │ Object Registry     │        │
│  │ (factory)│  │ (sync loop)  │  │ (bidirectional map) │        │
│  └────┬─────┘  └──────┬───────┘  └─────────┬──────────┘        │
│       │               │                     │                   │
│       │   ┌───────────┴───────────┐         │                   │
│       │   │   FusionClient        │         │                   │
│       └───┤   (singleton)         ├─────────┘                   │
│           │   - spawners set      │                             │
│           │   - synchronizers set │                             │
│           │   - object_registry   │                             │
│           │   - frame loop        │                             │
│           │   - callbacks         │                             │
│           └───────────┬───────────┘                             │
│                       │                                         │
├───────────────────────┼─────────────────────────────────────────┤
│                       │ (opaque handle boundary)                │
│                       v                                         │
│  ┌──────────────────────────────────────────┐                   │
│  │         SharedMode::Client               │                   │
│  │  - Objects map                           │                   │
│  │  - Photon transport                      │                   │
│  │  - UpdateFrameBegin / UpdateFrameEnd     │                   │
│  │  - CreateObject / CreateSubObject        │                   │
│  │  - RPC system                            │                   │
│  └──────────────────────────────────────────┘                   │
│                   Fusion SDK                                    │
└─────────────────────────────────────────────────────────────────┘
```

## Next Steps

- [Getting Started](getting-started.md) -- Minimal integration skeleton
- [Object Sync Patterns](object-sync-patterns.md) -- Words buffer mechanics
- [Sub-Objects](sub-objects.md) -- Child object creation and lifecycle
- [Objects](../concepts/objects.md) -- Conceptual object hierarchy
- [Object Creation](../concepts/object-creation.md) -- Three creation paths
- [Client API Reference](../reference/client-api.md) -- Full Client API
