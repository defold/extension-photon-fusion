# Sub-Objects

Sub-objects (also called child objects) are dynamically created `ObjectChild` instances attached to an existing `ObjectRoot`. They have their own Words buffer, their own `ObjectId`, and participate in Fusion's replication system independently -- but they share authority with their parent and use the parent's `NetworkedStringHeap`.

Use sub-objects when a root object needs to spawn additional networked entities at runtime (e.g., inventory items, equipped weapons, dynamic attachments).

See [Objects](../concepts/objects.md) for the class hierarchy and [Object Sync Patterns](object-sync-patterns.md) for Words buffer mechanics.

## Object Hierarchy

```
ObjectRoot (root object)
├── Words buffer (own)
├── Shadow buffer (own)
├── NetworkedStringHeap (shared with children)
├── SubObjects: vector<ObjectId>
│
├── ObjectChild (sub-object #1)
│   ├── Words buffer (own)
│   ├── Shadow buffer (own)
│   ├── Parent: ObjectId (points to root)
│   └── TargetObjectHash: uint32_t (matching key)
│
└── ObjectChild (sub-object #2)
    ├── Words buffer (own)
    └── ...
```

Both `ObjectRoot` and `ObjectChild` inherit from `Object`, so they share the same Words buffer API (`Words.Ptr`, `Words.Length`, `SetSendUpdates()`, `SetHasValidData()`, etc.). The key differences:

| Property | ObjectRoot | ObjectChild |
|----------|-----------|-------------|
| `Root()` | Returns `this` | Returns the parent `ObjectRoot*` |
| `StringHeap` | Owns it | Delegates to `Root()->StringHeap` |
| `SubObjects` | `vector<ObjectId>` of children | N/A |
| `Parent` | N/A | `ObjectId` of parent root |
| `TargetObjectHash` | N/A | `uint32_t` for matching on remote side |
| `Owner` | `PlayerId` | Inherits from root |
| `Flags` | Own ObjectFlags | N/A |
| `ObjectType` | `ObjectType::Root` | `ObjectType::Child` |

## Authority-Side Creation Flow

The authority client (object owner) creates sub-objects and adds them to the parent:

### Step 1: Create the Sub-Object

```cpp
SharedMode::ObjectChild* CreateSubObject(
    SharedMode::Client* client,
    SharedMode::ObjectRoot* parent,
    size_t userWordCount,
    uint64_t typeHash,
    uint32_t targetObjectHash,
    const uint8_t* headerData,
    size_t headerLength)
{
    constexpr size_t tailWords = sizeof(SharedMode::ObjectTail) / sizeof(int32_t);
    size_t totalWords = userWordCount + tailWords;

    SharedMode::TypeRef typeRef;
    typeRef.Hash = typeHash;
    typeRef.WordCount = static_cast<uint32_t>(totalWords);

    // Get a new unique ObjectId for the child
    SharedMode::ObjectId childId = client->GetNewObjectId();

    SharedMode::ObjectChild* child = client->CreateSubObject(
        parent->Id,           // parent ObjectId
        totalWords,           // total word count including tail
        typeRef,              // type reference (hash + word count)
        reinterpret_cast<const SharedMode::CharType*>(headerData),
        headerLength,
        targetObjectHash,     // matching hash for remote identification
        childId,              // unique child ID
        SharedMode::ObjectSpecialFlags::None
    );

    return child;
}
```

### Step 2: Add to Parent

```cpp
bool added = client->AddSubObject(parent, child);
if (!added) {
    // Handle error: parent may not exist or child already added
}
```

`AddSubObject()` registers the child with the parent's `SubObjects` list and triggers replication. On remote clients, `OnSubObjectCreated` fires.

### Step 3: Copy Initial State to Words

```cpp
if (child && child->Words.IsValid() && headerData) {
    constexpr int tailWords = sizeof(SharedMode::ObjectTail) / sizeof(int32_t);
    int usable = static_cast<int>(child->Words.Length) - tailWords;
    int toCopy = std::min(static_cast<int>(headerLength / sizeof(int32_t)), usable);
    if (toCopy > 0) {
        memcpy(child->Words.Ptr, headerData, toCopy * sizeof(int32_t));
    }
}
```

### Complete Authority-Side Example

```cpp
// typeHash identifies the sub-object scene/prefab (e.g., "res://bullet.tscn".hash())
// targetHash identifies the specific instance (e.g., node_name.hash())

void SpawnSubObject(SharedMode::Client* client,
                    SharedMode::ObjectRoot* parent,
                    uint64_t typeHash,
                    uint32_t targetHash,
                    const int32_t* spawnWords,
                    int userWordCount)
{
    constexpr size_t tailWords = sizeof(SharedMode::ObjectTail) / sizeof(int32_t);
    size_t totalWords = userWordCount + tailWords;

    SharedMode::TypeRef typeRef{typeHash, static_cast<uint32_t>(totalWords)};

    auto* child = client->CreateSubObject(
        parent->Id, totalWords, typeRef,
        reinterpret_cast<const SharedMode::CharType*>(spawnWords),
        userWordCount * sizeof(int32_t),
        targetHash,
        client->GetNewObjectId(),
        SharedMode::ObjectSpecialFlags::None
    );

    if (!child) return;

    bool added = client->AddSubObject(parent, child);
    if (!added) return;

    // Copy spawn data to Words buffer
    if (child->Words.IsValid() && spawnWords) {
        int usable = static_cast<int>(child->Words.Length) - tailWords;
        int toCopy = std::min(userWordCount, usable);
        memcpy(child->Words.Ptr, spawnWords, toCopy * sizeof(int32_t));
    }

    // Sub-objects inherit SendUpdates from parent -- no explicit set needed.
    child->SetHasValidData(true);

    // Store engine pointer for later access
    child->Engine = /* your engine-side node/actor */;
}
```

## Remote-Side Handling

When the authority creates a sub-object, Fusion fires `OnSubObjectCreated` on all other clients:

```cpp
client->OnSubObjectCreated = [](SharedMode::ObjectChild* child) {
    // 1. Find the parent's engine representation
    SharedMode::ObjectId parentId = child->Parent;
    // ... look up parent in your registry ...

    // 2. Match the type hash to a registered scene/prefab
    uint64_t typeHash = child->Type.Hash;
    // ... find matching scene ...

    // 3. Instantiate the engine representation
    // ... create node/actor ...

    // 4. Deserialize spawn data from child's Words buffer
    if (child->Words.IsValid()) {
        constexpr int tailWords = sizeof(SharedMode::ObjectTail) / sizeof(int32_t);
        int usable = static_cast<int>(child->Words.Length) - tailWords;
        DeserializeProperties(child->Words.Ptr, usable);
    }

    // 5. Mark as having valid data
    child->SetHasValidData(true);

    // 6. Store engine pointer and register in your object registry
    child->Engine = /* engine-side instance */;
};
```

### Type Matching

Both the authority and remote sides must agree on the type hash. The convention is to use a hash of the scene/prefab resource path:

| Side | Computes | From |
|------|----------|------|
| Authority | `typeRef.Hash` | `scene_path.hash()` |
| Remote | Compare `child->Type.Hash` | Against registered scene hashes |

The Godot integration registers sub-object scenes via `register_sub_object_scene(path)` and stores `path.hash()` as the type hash. The Unreal integration uses a similar spawnable type registration.

### TargetObjectHash

`TargetObjectHash` is a secondary identifier for matching within a type. For example, if a player can equip multiple items of the same type, `TargetObjectHash` distinguishes between them (e.g., `node_name.hash()`).

The remote side can access it via `child->TargetObjectHash` to restore the correct instance identity.

## Pending Queue Pattern

Sub-objects may arrive before their parent is ready on the remote side. This happens when:
- The parent object creation callback hasn't been processed yet
- The parent's engine representation hasn't finished loading
- The parent is a scene object and the scene hasn't loaded yet

The solution is a **pending queue**:

```cpp
// Storage
std::vector<SharedMode::ObjectChild*> pendingSubObjects;

// In OnSubObjectCreated: if parent isn't ready, queue it
client->OnSubObjectCreated = [&](SharedMode::ObjectChild* child) {
    SharedMode::ObjectId parentId = child->Parent;

    // Try to find parent
    auto* parentEngine = FindEngineObject(parentId);
    if (!parentEngine) {
        pendingSubObjects.push_back(child);
        return;
    }

    // Process normally
    HandleSubObjectCreated(child, parentEngine);
};

// Each frame: retry pending sub-objects
void ProcessPendingSubObjects() {
    for (int i = static_cast<int>(pendingSubObjects.size()) - 1; i >= 0; i--) {
        SharedMode::ObjectChild* child = pendingSubObjects[i];
        SharedMode::ObjectId parentId = child->Parent;

        auto* parentEngine = FindEngineObject(parentId);
        if (parentEngine) {
            HandleSubObjectCreated(child, parentEngine);
            pendingSubObjects.erase(pendingSubObjects.begin() + i);
        }
    }
}
```

The Godot integration maintains `pending_sub_objects` in `FusionClient` and processes them each frame in `_process_pending_sub_objects()`.

**Clean up on parent destruction:** When a parent is destroyed, remove any pending sub-objects that reference it:

```cpp
client->OnObjectDestroyed = [&](const SharedMode::ObjectRoot* obj,
                                SharedMode::DestroyModes mode) {
    // Remove pending sub-objects for this parent
    for (int i = static_cast<int>(pendingSubObjects.size()) - 1; i >= 0; i--) {
        SharedMode::ObjectId parentId = pendingSubObjects[i]->Parent;
        if (parentId == obj->Id) {
            pendingSubObjects.erase(pendingSubObjects.begin() + i);
        }
    }
};
```

## Dual Handle Pattern

In the Godot integration, each `FusionSynchronizer` maintains two handles:

| Handle | Type | Used For |
|--------|------|----------|
| `fusion_object` | Always `ObjectRoot*` | Authority checks (`IsOwner()`, `GetOwner()`) |
| `fusion_data_object` | `Object*` (root or child) | Words buffer access (`Words.Ptr`) |

For root objects, both handles point to the same `ObjectRoot*`. For sub-objects:

```cpp
// Root object binding:
void BindRootObject(ObjectRoot* root) {
    fusion_object = root;       // Root for authority
    fusion_data_object = root;  // Same object for Words
}

// Sub-object binding:
void BindChildObject(ObjectChild* child) {
    fusion_object = child->Root();  // Root for authority checks
    fusion_data_object = child;     // Child for Words buffer
}
```

This allows `sync_outbound()` and `sync_inbound()` to work identically for both root and sub-objects -- they always cast `fusion_data_object` to `Object*` for Words access:

```cpp
void SyncOutbound() {
    SharedMode::Object* obj = static_cast<SharedMode::Object*>(fusion_data_object);
    // Write to obj->Words -- works for both root and child
}

bool HasAuthority() {
    // Always check against the root object
    return client->IsOwner(static_cast<SharedMode::Object*>(fusion_object));
}
```

## Finding Sub-Objects

The SDK provides methods to query sub-objects:

```cpp
// Check if a root has any sub-objects
bool hasSubs = client->HasSubObjects(rootObj);

// Get all sub-object IDs for a root
const std::vector<SharedMode::ObjectId>& subIds = client->GetSubObject(rootObj);

// Find a specific sub-object by hash
SharedMode::Object* found = client->FindSubObjectWithHash(rootObj, targetHash);
```

You can also iterate via the root's `SubObjects` vector:

```cpp
SharedMode::ObjectRoot* root = /* ... */;
for (const auto& subId : root->SubObjects) {
    SharedMode::Object* sub = client->FindObject(subId);
    if (sub) {
        // Process sub-object
    }
}
```

## String Operations on Sub-Objects

Sub-objects share their root's `NetworkedStringHeap`. The `AddString()`, `ResolveString()`, and `FreeString()` methods on `Object` automatically delegate to `Root()->StringHeap`:

```cpp
// This works the same whether obj is ObjectRoot or ObjectChild
SharedMode::StringHandle handle = obj->AddString(
    reinterpret_cast<const SharedMode::CharType*>("hello")
);

SharedMode::StringMessage status;
const SharedMode::CharType* str = obj->ResolveString(handle, status);

obj->FreeString(handle);
```

There is no need to manually navigate to the root for string operations.

## Cleanup

When destroying sub-objects, clean up both the Fusion state and your registry:

```cpp
// On object destruction callback
client->OnObjectDestroyed = [&](const SharedMode::ObjectRoot* obj,
                                SharedMode::DestroyModes mode) {
    // Clean up sub-object tracking
    if (client->HasSubObjects(obj)) {
        const auto& subIds = client->GetSubObject(obj);
        for (const auto& subId : subIds) {
            // Remove from your registry
            registry.UnregisterByFusionId(subId.Origin, subId.Counter);
        }
    }

    // Remove root from registry
    registry.UnregisterByFusionId(obj->Id.Origin, obj->Id.Counter);
};
```

## SDK API Reference

### Client Methods

| Method | Signature |
|--------|-----------|
| `CreateSubObject` | `ObjectChild* CreateSubObject(ObjectId parent, size_t words, const TypeRef& type, const CharType* header, size_t headerLength, uint32_t targetObjectHash, ObjectId id, ObjectSpecialFlags flags)` |
| `AddSubObject` | `bool AddSubObject(ObjectRoot* parent, ObjectChild* child)` |
| `HasSubObjects` | `bool HasSubObjects(const Object* root)` |
| `GetSubObject` | `const std::vector<ObjectId>& GetSubObject(const Object* root)` |
| `FindSubObjectWithHash` | `Object* FindSubObjectWithHash(ObjectRoot* root, uint32_t subObjectHash)` |
| `GetNewObjectId` | `ObjectId GetNewObjectId()` |

### ObjectChild Members

| Member | Type | Description |
|--------|------|-------------|
| `Parent` | `ObjectId` | Parent root's ID |
| `TargetObjectHash` | `uint32_t` | Secondary matching key |
| `SubObjectStatus` | `int32_t` | Internal status |

### ObjectChild Static Methods

| Method | Signature | Purpose |
|--------|-----------|---------|
| `GetParent` | `static ObjectId GetParent(const Object*)` | Get parent ID (safe for any Object) |
| `Is` | `static bool Is(const Object*)` | Check if Object is a child |
| `Cast` | `static ObjectChild* Cast(Object*)` | Safe downcast (returns nullptr if not child) |

## Next Steps

- [Engine Binding](engine-binding.md) -- Connecting SDK objects to engine representations
- [Object Sync Patterns](object-sync-patterns.md) -- Words buffer mechanics
- [Objects](../concepts/objects.md) -- Full object hierarchy conceptual model
- [Object Creation](../concepts/object-creation.md) -- The three creation paths
