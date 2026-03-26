# Engine Binding

This guide covers the patterns for connecting Fusion SDK objects to engine-side representations: the `Object::Engine` pointer, bidirectional registry design, type hash conventions, spawnable type registration, and the spawner/factory pattern.

These patterns are engine-agnostic. The examples use generic C++ constructs that can be adapted to any game engine.

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
subs += client->OnObjectOwnerChanged.Subscribe(
    [](SharedMode::ObjectRoot* obj) {
        auto* node = static_cast<MyEngineNode*>(obj->Engine);
        if (node) {
            node->OnAuthorityChanged();
        }
    }
);
```

**Lifetime rule:** You must null out `Engine` when the engine-side object is destroyed, and check for null before dereferencing. The Fusion object may outlive the engine representation (e.g., during scene transitions or async destruction).

## Bidirectional Registry Pattern

The `Engine` pointer gives you Fusion-to-engine lookup. But you also need engine-to-Fusion lookup: given an engine object, find its Fusion counterpart (e.g., for despawning or RPC sending).

### PackedObjectId Helper

Fusion's `ObjectId` has two `uint32_t` components: `Origin` (PlayerId) and `Counter`. The `ObjectId` struct has a built-in conversion to `uint64_t`:

```cpp
// ObjectId has an implicit conversion operator to uint64_t:
//   operator uint64_t() const;
// And a constructor from uint64_t:
//   explicit ObjectId(const uint64_t& packed);

// Pack:
SharedMode::ObjectId id(origin, counter);
uint64_t packed = static_cast<uint64_t>(id);

// Unpack:
SharedMode::ObjectId unpacked(packed);
// unpacked.Origin == origin
// unpacked.Counter == counter
```

The packing layout is: low 32 bits = Origin, high 32 bits = Counter.

### Registry Design

```cpp
class ObjectRegistry {
    // Engine ID -> packed Fusion ObjectId
    std::unordered_map<uint64_t, uint64_t> engineToFusion;
    // Packed Fusion ObjectId -> Engine ID
    std::unordered_map<uint64_t, uint64_t> fusionToEngine;

public:
    void Register(uint64_t engineId, SharedMode::ObjectId fusionId) {
        uint64_t packed = static_cast<uint64_t>(fusionId);
        engineToFusion[engineId] = packed;
        fusionToEngine[packed] = engineId;
    }

    void UnregisterByEngineId(uint64_t engineId) {
        auto it = engineToFusion.find(engineId);
        if (it != engineToFusion.end()) {
            fusionToEngine.erase(it->second);
            engineToFusion.erase(it);
        }
    }

    void UnregisterByFusionId(SharedMode::ObjectId fusionId) {
        uint64_t packed = static_cast<uint64_t>(fusionId);
        auto it = fusionToEngine.find(packed);
        if (it != fusionToEngine.end()) {
            engineToFusion.erase(it->second);
            fusionToEngine.erase(it);
        }
    }

    bool HasEngineId(uint64_t engineId) const {
        return engineToFusion.count(engineId) > 0;
    }

    bool HasFusionId(SharedMode::ObjectId fusionId) const {
        return fusionToEngine.count(static_cast<uint64_t>(fusionId)) > 0;
    }

    uint64_t GetEngineId(SharedMode::ObjectId fusionId) const {
        uint64_t packed = static_cast<uint64_t>(fusionId);
        auto it = fusionToEngine.find(packed);
        return (it != fusionToEngine.end()) ? it->second : 0;
    }

    SharedMode::ObjectId GetFusionId(uint64_t engineId) const {
        auto it = engineToFusion.find(engineId);
        if (it != engineToFusion.end()) {
            return SharedMode::ObjectId(it->second);
        }
        return SharedMode::ObjectId(0, 0);
    }

    void Clear() {
        engineToFusion.clear();
        fusionToEngine.clear();
    }
};
```

### Registration Points

Register and unregister objects at these lifecycle events:

| Event | Action |
|-------|--------|
| After `CreateObject()` + engine instantiation | `Register(engineId, obj->Id)` |
| In `OnObjectReady` callback | `Register(engineId, obj->Id)` |
| After `CreateSceneObject()` | `Register(engineId, obj->Id)` |
| After `CreateSubObject()` + `AddSubObject()` | `Register(engineId, child->Id)` |
| In `OnSubObjectCreated` callback | `Register(engineId, child->Id)` |
| In `OnObjectDestroyed` callback | `UnregisterByFusionId(obj->Id)` |
| In `OnSubObjectDestroyed` callback | `UnregisterByFusionId(child->Id)` |
| When engine object is freed | `UnregisterByEngineId(engineId)` |

### Usage: RPC Routing

The registry enables routing incoming RPCs to the correct engine object:

```cpp
subs += client->OnRpc.Subscribe([&](SharedMode::Rpc& rpc) {
    if (rpc.TargetObject.IsSome()) {
        // Object-targeted RPC
        uint64_t engineId = registry.GetEngineId(rpc.TargetObject);
        auto* node = GetEngineObject(engineId);
        if (node) {
            DispatchRpc(node, rpc);
        }
    } else {
        // Broadcast RPC (TargetObject is {0, 0})
        DispatchBroadcastRpc(rpc);
    }
});
```

### Usage: Sub-Object Parent Lookup

When a sub-object arrives, find its parent's engine representation:

```cpp
subs += client->OnSubObjectCreated.Subscribe([&](SharedMode::ObjectChild* child) {
    uint64_t parentEngineId = registry.GetEngineId(child->Parent);
    auto* parentNode = GetEngineObject(parentEngineId);
    if (parentNode) {
        HandleSubObjectCreated(child, parentNode);
    }
});
```

## Type Hash Convention

When a remote client creates an object, the local client needs to know which scene/prefab to instantiate. This is solved by matching `TypeRef::Hash` values.

A type hash is a `uint64_t` that uniquely identifies a scene/prefab type. The convention is to hash the resource path:

```cpp
// Using the SDK's built-in CRC64:
uint64_t typeHash = SharedMode::CRC64(path.c_str(), path.length());

// Or using engine-specific hashing:
// uint64_t typeHash = scenePath.hash();
```

The type hash is set at object creation time via `TypeRef::Hash` and is available on remote objects via `Object::Type.Hash`.

```cpp
struct TypeRef {
    uint64_t Hash;        // Type identifier (scene path hash)
    uint32_t WordCount;   // Total Words buffer size (including tail)
};
```

## Spawnable Type Registry

Each spawner maintains a list of spawnable scenes/prefabs:

```cpp
struct SpawnableType {
    std::string scenePath;
    uint64_t typeHash;
    void* cachedResource = nullptr; // Lazy-loaded scene resource
};

class SpawnableTypeRegistry {
    std::vector<SpawnableType> types;

public:
    void RegisterScene(const std::string& path) {
        SpawnableType type;
        type.scenePath = path;
        type.typeHash = SharedMode::CRC64(path.c_str(), path.length());
        types.push_back(type);
    }

    SpawnableType* FindByTypeHash(uint64_t hash) {
        for (auto& type : types) {
            if (type.typeHash == hash) {
                return &type;
            }
        }
        return nullptr;
    }

    bool HasTypeHash(uint64_t hash) const {
        for (const auto& type : types) {
            if (type.typeHash == hash) return true;
        }
        return false;
    }
};
```

## Spawner / Factory Pattern

A spawner is the engine-side component that manages the full lifecycle of networked objects: registration, local spawning, remote instantiation, and despawning.

### Spawner Responsibilities

| Responsibility | When |
|----------------|------|
| Register spawnable scenes | At initialization |
| Local spawn | When authority wants to create an object |
| Remote spawn | When `OnObjectReady` fires with a matching type hash |
| Local despawn | When authority wants to remove an object |
| Remote despawn | When `OnObjectDestroyed` fires |

### Local Spawn Flow

```
┌──────────────────────────────────────────────────────────────────────┐
│  1. Load scene resource, instantiate off-tree                        │
│  2. Apply initial data (position, properties, etc.)                  │
│  3. Compute word count from replication config                       │
│  4. Serialize spawn-time properties to header byte buffer            │
│  5. Call CreateObject(totalWords, typeRef, header, ..., ownerMode)   │
│  6. SetSendUpdates(false) on the new object                         │
│  7. memcpy serialized data to Words buffer                           │
│  8. Store obj->Engine = instance                                     │
│  9. Register in bidirectional registry                               │
│ 10. Add instance to the scene tree                                   │
│ 11. SetSendUpdates(true), SetHasValidData(true)                     │
└──────────────────────────────────────────────────────────────────────┘
```

```cpp
void* SpawnLocal(SharedMode::Client* client,
                 ObjectRegistry& registry,
                 SpawnableTypeRegistry& typeRegistry,
                 const std::string& scenePath,
                 SharedMode::ObjectOwnerModes ownerMode)
{
    // 1-2. Instantiate and configure
    void* instance = InstantiateScene(scenePath);
    ApplyInitialData(instance);

    // 3. Compute word count
    int userWords = ComputeWordCount(instance);
    size_t totalWords = userWords + SharedMode::Object::ExtraTailWords;

    // 4. Serialize spawn data
    std::vector<uint8_t> spawnData = SerializeSpawnData(instance);

    // 5. Create Fusion object
    uint64_t typeHash = SharedMode::CRC64(scenePath.c_str(), scenePath.length());
    SharedMode::TypeRef typeRef{typeHash, static_cast<uint32_t>(totalWords)};

    SharedMode::ObjectRoot* obj = client->CreateObject(
        totalWords, typeRef,
        reinterpret_cast<const PhotonCommon::CharType*>(spawnData.data()),
        spawnData.size(),
        0,          // scene index
        ownerMode
    );

    if (!obj) return nullptr;

    // 6. Disable sending until ready
    obj->SetSendUpdates(false);

    // 7. Copy spawn data to Words buffer
    int usable = static_cast<int>(obj->Words.Length)
               - SharedMode::Object::ExtraTailWords;
    int toCopy = (userWords < usable) ? userWords : usable;
    // (SerializePropertiesToWords fills the Words buffer)
    SerializePropertiesToWords(instance, obj->Words.Ptr, toCopy);

    // 8. Store engine pointer
    obj->Engine = instance;

    // 9. Register
    uint64_t engineId = GetEngineObjectId(instance);
    registry.Register(engineId, obj->Id);

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
┌──────────────────────────────────────────────────────────────────────┐
│  1. OnObjectReady callback fires with ObjectRoot*                    │
│  2. Match Type.Hash to a registered spawnable scene                  │
│  3. Load and instantiate the scene                                   │
│  4. Deserialize spawn data from obj->Header                          │
│  5. Deserialize initial Words to engine properties                   │
│  6. Store obj->Engine = instance                                     │
│  7. Register in bidirectional registry                               │
│  8. Add instance to scene tree                                       │
│  9. SetHasValidData(true)                                            │
└──────────────────────────────────────────────────────────────────────┘
```

```cpp
void HandleRemoteSpawn(SharedMode::Client* client,
                       SharedMode::ObjectRoot* obj,
                       ObjectRegistry& registry,
                       SpawnableTypeRegistry& typeRegistry)
{
    // 2. Find matching type
    SpawnableType* type = typeRegistry.FindByTypeHash(obj->Type.Hash);
    if (!type) {
        printf("Unknown type hash: %llu\n",
               static_cast<unsigned long long>(obj->Type.Hash));
        return;
    }

    // 3. Instantiate
    void* instance = InstantiateScene(type->scenePath);

    // 4. Deserialize header (spawn data)
    if (obj->Header.Valid()) {
        DeserializeSpawnData(instance, obj->Header.Ptr, obj->Header.Length);
    }

    // 5. Apply initial Words to engine
    if (obj->Words.IsValid()) {
        int usable = static_cast<int>(obj->Words.Length)
                   - SharedMode::Object::ExtraTailWords;
        DeserializeWordsToProperties(instance, obj->Words.Ptr, usable);
    }

    // 6-7. Bind and register
    obj->Engine = instance;
    uint64_t engineId = GetEngineObjectId(instance);
    registry.Register(engineId, obj->Id);

    // 8. Add to scene
    AddToSceneTree(instance);

    // 9. Mark ready
    obj->SetHasValidData(true);
}
```

### Despawn

```cpp
void Despawn(SharedMode::Client* client,
             ObjectRegistry& registry,
             void* engineInstance)
{
    uint64_t engineId = GetEngineObjectId(engineInstance);
    SharedMode::ObjectId fusionId = registry.GetFusionId(engineId);

    if (fusionId.IsNone()) return;

    SharedMode::ObjectRoot* obj = client->FindObjectRoot(fusionId);
    if (obj) {
        // Second param: false = engine object NOT already destroyed.
        // The OnObjectDestroyed callback will handle engine cleanup.
        client->DestroyObjectLocal(obj, false);
    }
}
```

**`DestroyObjectLocal` signature:**

```cpp
bool DestroyObjectLocal(ObjectRoot* obj, bool engineObjectAlreadyDestroyed);
```

The `engineObjectAlreadyDestroyed` parameter tells the SDK whether the engine-side object is already gone (e.g., the user destroyed it before calling despawn). If `true`, your `OnObjectDestroyed` handler should skip freeing the engine object.

### DestroyModes Enum

The `OnObjectDestroyed` callback includes a `DestroyModes` enum indicating why the object was destroyed:

```cpp
enum class DestroyModes {
    Local = 0,            // DestroyObjectLocal() was called by this client
    Remote = 1,           // Remote authority destroyed the object
    SceneChange = 2,      // Scene changed, object cleaned up
    Shutdown = 3,         // Client shutting down
    RejectedNotOwner = 4, // Creation rejected: not the owner
    ForceDestroy = 5      // Server force-destroyed the object
};
```

Use this to decide whether to free the engine object:

```cpp
subs += client->OnObjectDestroyed.Subscribe(
    [&](const SharedMode::ObjectRoot* obj, SharedMode::DestroyModes mode) {
        // Unregister from our registry
        registry.UnregisterByFusionId(obj->Id);

        // Free the engine object (unless already destroyed)
        if (obj->Engine && mode != SharedMode::DestroyModes::Shutdown) {
            DestroyEngineObject(obj->Engine);
        }
    }
);
```

## Multiple Spawner Support

An integration can support multiple spawners, each managing different object types. The pattern:

1. Each spawner registers its own set of spawnable scenes
2. On remote creation, broadcast to all spawners -- only the one with a matching type hash handles it
3. Spawners self-register with the client

```cpp
class SpawnerManager {
    std::unordered_set<Spawner*> spawners;

public:
    void RegisterSpawner(Spawner* s) { spawners.insert(s); }
    void UnregisterSpawner(Spawner* s) { spawners.erase(s); }

    bool HandleRemoteObjectReady(SharedMode::Client* client,
                                 SharedMode::ObjectRoot* obj)
    {
        for (auto* spawner : spawners) {
            if (spawner->CanHandleType(obj->Type.Hash)) {
                spawner->SpawnRemote(client, obj);
                return true;
            }
        }
        return false; // No spawner claimed this object
    }

    void HandleRemoteObjectDestroyed(const SharedMode::ObjectRoot* obj,
                                     SharedMode::DestroyModes mode)
    {
        for (auto* spawner : spawners) {
            spawner->TryHandleRemoteDestruction(obj, mode);
        }
    }
};

static SpawnerManager g_spawnerManager;
```

Wire it to the callbacks:

```cpp
subs += client->OnObjectReady.Subscribe(
    [](SharedMode::ObjectRoot* obj) {
        if (!g_spawnerManager.HandleRemoteObjectReady(g_client, obj)) {
            printf("No spawner for type hash %llu\n",
                   static_cast<unsigned long long>(obj->Type.Hash));
        }
    }
);

subs += client->OnObjectDestroyed.Subscribe(
    [](const SharedMode::ObjectRoot* obj, SharedMode::DestroyModes mode) {
        g_spawnerManager.HandleRemoteObjectDestroyed(obj, mode);
    }
);
```

## Synchronizer Registration Pattern

Synchronizers (components that handle per-frame Words buffer sync) also register with the client:

```cpp
class SyncManager {
    std::unordered_set<Synchronizer*> synchronizers;

public:
    void Register(Synchronizer* s) { synchronizers.insert(s); }
    void Unregister(Synchronizer* s) { synchronizers.erase(s); }

    void SyncOutboundAll(SharedMode::Client* client) {
        for (auto* sync : synchronizers) {
            if (sync->HasAuthority(client)) {
                sync->SyncOutbound();
            }
        }
    }

    void SyncInboundAll(SharedMode::Client* client) {
        for (auto* sync : synchronizers) {
            if (!sync->HasAuthority(client)) {
                sync->SyncInbound();
            }
        }
    }
};
```

The frame loop calls these at the appropriate points:

```cpp
void FrameUpdate(double dt) {
    g_realtime->Service();

    if (!g_client->IsRunning()) return;

    g_syncManager.SyncOutboundAll(g_client);    // Before End
    g_client->UpdateFrameEnd();
    g_client->UpdateFrameBegin(dt);
    g_syncManager.SyncInboundAll(g_client);     // After Begin
}
```

## Integration Entry Point

The plugin's entry point registers all classes and creates the singleton client:

```cpp
// Pseudocode for a plugin entry point
void InitializePlugin() {
    // 1. Register project settings (app ID, log level, etc.)
    RegisterProjectSettings();

    // 2. Register custom classes with the engine
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

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Your Engine                                   │
│                                                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────────────┐      │
│  │ Spawner(s)   │  │ Synchronizer │  │ Object Registry        │      │
│  │ (factory)    │  │ (sync loop)  │  │ (bidirectional map)    │      │
│  └──────┬───────┘  └──────┬───────┘  └──────────┬────────────┘      │
│         │                 │                      │                   │
│         │   ┌─────────────┴──────────────┐       │                   │
│         │   │   FusionClient             │       │                   │
│         └───┤   (singleton)              ├───────┘                   │
│             │   - spawner manager        │                           │
│             │   - sync manager           │                           │
│             │   - object registry        │                           │
│             │   - frame loop             │                           │
│             │   - subscription bag       │                           │
│             └────────────┬───────────────┘                           │
│                          │                                           │
├──────────────────────────┼───────────────────────────────────────────┤
│                          │ (void* Engine boundary)                   │
│                          v                                           │
│  ┌───────────────────────────────────────────────┐                   │
│  │         SharedMode::Client                    │                   │
│  │  - AllObjects() / AllRootObjects()            │                   │
│  │  - CreateObject / CreateSubObject             │                   │
│  │  - UpdateFrameBegin / UpdateFrameEnd          │                   │
│  │  - RPC system (CreateUserRpc / SendUserRpc)   │                   │
│  │  - Broadcasters (OnObjectReady, etc.)         │                   │
│  └────────────────────┬──────────────────────────┘                   │
│                       │                                              │
│  ┌────────────────────┴──────────────────────────┐                   │
│  │   PhotonMatchmaking::RealtimeClient           │                   │
│  │  - Connect / SelectRegion / JoinOrCreateRoom  │                   │
│  │  - Service()                                  │                   │
│  │  - Task<Result<T>> async operations           │                   │
│  └───────────────────────────────────────────────┘                   │
│                   Fusion SDK                                         │
└─────────────────────────────────────────────────────────────────────┘
```

## Next Steps

- [Getting Started](getting-started.md) -- Minimal integration skeleton
- [Object Sync Patterns](object-sync-patterns.md) -- Words buffer mechanics
- [Sub-Objects](sub-objects.md) -- Child object creation and lifecycle
- [Pitfalls](../pitfalls.md) -- Critical gotchas
