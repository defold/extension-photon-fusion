# Sub-Objects

Sub-objects (child objects) are dynamically created `ObjectChild` instances attached to an existing `ObjectRoot`. They have their own Words buffer, their own `ObjectId`, and participate in Fusion's replication system independently. However, they share authority with their parent root and use the parent's `NetworkedStringHeap`.

Use sub-objects when a root object needs to spawn additional networked entities at runtime (e.g., inventory items, equipped weapons, dynamic attachments, vehicle seats).

See [Object Sync Patterns](object-sync-patterns.md) for Words buffer mechanics.

## Object Hierarchy

```
ObjectRoot (root object)
├── Words buffer (own)
├── Shadow buffer (own)
├── NetworkedStringHeap (shared with all children)
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

Both `ObjectRoot` and `ObjectChild` inherit from `Object`, so they share the same Words buffer API (`Words.Ptr`, `Words.Length`, `SetSendUpdates()`, `SetHasValidData()`, etc.).

**Key differences:**

| Property | ObjectRoot | ObjectChild |
|----------|-----------|-------------|
| `Root()` | Returns `this` | Returns the parent `ObjectRoot*` |
| `StringHeap` | Owns it | Delegates to `Root()->StringHeap` |
| `SubObjects` | `vector<ObjectId>` of children | N/A |
| `Parent` | N/A | `ObjectId` of parent root |
| `TargetObjectHash` | N/A | `uint32_t` for matching on remote side |
| `Owner` | `PlayerId` | Inherits from root |
| `OwnerMode` | Own `ObjectOwnerModes` | N/A (inherits root authority) |
| `ObjectType` | `ObjectType::Root` | `ObjectType::Child` |

## ObjectChild Overview

The `ObjectChild` class extends `Object` with parent-linking fields:

```cpp
class ObjectChild final : public Object {
public:
    ObjectId Parent{0, 0};         // Parent root's ObjectId
    uint32_t TargetObjectHash{0};  // Secondary matching key

    // Safe downcast: returns nullptr if obj is not a child
    static ObjectChild* Cast(Object* obj);
    static const ObjectChild* Cast(const Object* obj);

    // Check if an Object is a child
    static bool Is(const Object* obj);

    // Get parent ID from any Object (returns {0,0} if not a child)
    static ObjectId GetParent(const Object* obj);

    // Returns the parent ObjectRoot*
    ObjectRoot* Root() override;
};
```

## Authority-Side Creation Flow

Creating a sub-object on the authority side is a three-step process: create, write data, register.

### Step 1: CreateSubObject

```cpp
SharedMode::ObjectChild* CreateChildObject(
    SharedMode::Client* client,
    SharedMode::ObjectRoot* parent,
    size_t userWordCount,
    uint64_t typeHash,
    uint32_t targetObjectHash)
{
    size_t totalWords = userWordCount + SharedMode::Object::ExtraTailWords;

    SharedMode::TypeRef typeRef;
    typeRef.Hash = typeHash;
    typeRef.WordCount = static_cast<uint32_t>(totalWords);

    // Get a unique ObjectId for the child
    SharedMode::ObjectId childId = client->GetNewObjectId();

    SharedMode::ObjectChild* child = client->CreateSubObject(
        parent->Id,           // parent ObjectId
        totalWords,           // total words including tail
        typeRef,              // type reference (hash + word count)
        nullptr,              // header data (spawn payload)
        0,                    // header length
        targetObjectHash,     // matching key for remote identification
        childId,              // child's unique ObjectId
        SharedMode::ObjectSpecialFlags::None
    );

    return child;
}
```

### Step 2: Write Spawn Data to Words

```cpp
void WriteChildSpawnData(SharedMode::ObjectChild* child,
                         const int32_t* spawnWords,
                         int userWordCount)
{
    if (!child || !child->Words.IsValid() || !spawnWords) return;

    int usable = static_cast<int>(child->Words.Length)
               - SharedMode::Object::ExtraTailWords;
    int toCopy = (userWordCount < usable) ? userWordCount : usable;

    memcpy(child->Words.Ptr, spawnWords, toCopy * sizeof(int32_t));
}
```

### Step 3: AddSubObject

```cpp
bool RegisterChild(SharedMode::Client* client,
                   SharedMode::ObjectRoot* parent,
                   SharedMode::ObjectChild* child)
{
    bool added = client->AddSubObject(parent, child);
    if (!added) {
        // Parent may not exist or child was already added
        return false;
    }

    child->SetHasValidData(true);
    return true;
}
```

`AddSubObject()` appends the child's `ObjectId` to the parent's `SubObjects` vector and triggers replication. On remote clients, `OnSubObjectCreated` fires.

**Critical ordering:** `CreateSubObject` -> write spawn data -> `AddSubObject`. Calling `AddSubObject` before writing spawn data sends zeroed Words to remote clients. Forgetting `AddSubObject` means the child exists locally but is never replicated.

### Complete Authority-Side Example

```cpp
void SpawnSubObject(SharedMode::Client* client,
                    SharedMode::ObjectRoot* parent,
                    uint64_t typeHash,
                    uint32_t targetHash,
                    const int32_t* spawnWords,
                    int userWordCount,
                    void* engineInstance)
{
    // 1. Compute total words
    size_t totalWords = userWordCount + SharedMode::Object::ExtraTailWords;

    SharedMode::TypeRef typeRef{typeHash, static_cast<uint32_t>(totalWords)};

    // 2. Create the child object
    SharedMode::ObjectChild* child = client->CreateSubObject(
        parent->Id,
        totalWords,
        typeRef,
        reinterpret_cast<const PhotonCommon::CharType*>(spawnWords),
        userWordCount * sizeof(int32_t),
        targetHash,
        client->GetNewObjectId(),
        SharedMode::ObjectSpecialFlags::None
    );

    if (!child) return;

    // 3. Copy spawn data to Words buffer
    if (child->Words.IsValid() && spawnWords) {
        int usable = static_cast<int>(child->Words.Length)
                   - SharedMode::Object::ExtraTailWords;
        int toCopy = (userWordCount < usable) ? userWordCount : usable;
        memcpy(child->Words.Ptr, spawnWords, toCopy * sizeof(int32_t));
    }

    // 4. Register with parent
    bool added = client->AddSubObject(parent, child);
    if (!added) return;

    // 5. Mark as ready
    child->SetHasValidData(true);

    // 6. Store engine pointer
    child->Engine = engineInstance;
}
```

## Remote-Side Handling

When the authority creates a sub-object, Fusion fires `OnSubObjectCreated` on all other clients:

```cpp
void SetupSubObjectCallback(SharedMode::Client* client,
                            PhotonCommon::SubscriptionBag& subs)
{
    subs += client->OnSubObjectCreated.Subscribe(
        [client](SharedMode::ObjectChild* child) {
            // 1. Find the parent's engine representation
            SharedMode::ObjectId parentId = child->Parent;
            SharedMode::ObjectRoot* parentRoot = client->FindObjectRoot(parentId);

            if (!parentRoot || !parentRoot->Engine) {
                // Parent not ready yet -- queue for later processing
                AddToPendingQueue(child);
                return;
            }

            // 2. Match the type hash to a registered scene/prefab
            uint64_t typeHash = child->Type.Hash;
            const char* scenePath = FindSceneByTypeHash(typeHash);
            if (!scenePath) {
                printf("Unknown sub-object type: %llu\n",
                       static_cast<unsigned long long>(typeHash));
                return;
            }

            // 3. Instantiate the engine representation
            void* instance = InstantiateScene(scenePath);

            // 4. Deserialize initial state from child's Words buffer
            if (child->Words.IsValid()) {
                int usable = static_cast<int>(child->Words.Length)
                           - SharedMode::Object::ExtraTailWords;
                DeserializeProperties(instance, child->Words.Ptr, usable);
            }

            // 5. Mark as having valid data
            child->SetHasValidData(true);

            // 6. Store engine pointer and register
            child->Engine = instance;
            RegisterInRegistry(instance, child->Id);

            // 7. Attach to parent in engine
            AttachToParent(instance, parentRoot->Engine);
        }
    );
}
```

## Pending Queue Pattern

Sub-objects may arrive before their parent is ready on the remote side. This happens when:
- The parent object creation callback has not been processed yet
- The parent's engine representation has not finished loading
- The parent is a scene object and the scene has not loaded yet

The solution is a **pending queue** that retries each frame:

```cpp
class SubObjectPendingQueue {
    std::vector<SharedMode::ObjectChild*> pending;

public:
    void Add(SharedMode::ObjectChild* child) {
        pending.push_back(child);
    }

    // Call each frame to process pending sub-objects.
    // Iterates backward so removal does not shift unprocessed elements.
    void ProcessPending(SharedMode::Client* client) {
        for (int i = static_cast<int>(pending.size()) - 1; i >= 0; i--) {
            SharedMode::ObjectChild* child = pending[i];
            SharedMode::ObjectId parentId = child->Parent;

            SharedMode::ObjectRoot* parentRoot = client->FindObjectRoot(parentId);
            if (!parentRoot || !parentRoot->Engine) {
                continue; // Parent still not ready, keep in queue
            }

            // Parent is ready -- process the sub-object
            HandleSubObjectCreated(client, child, parentRoot);

            // Remove from pending list
            pending.erase(pending.begin() + i);
        }
    }

    // Call when a parent is destroyed to clean up orphaned pending children
    void RemoveForParent(SharedMode::ObjectId parentId) {
        for (int i = static_cast<int>(pending.size()) - 1; i >= 0; i--) {
            if (pending[i]->Parent == parentId) {
                pending.erase(pending.begin() + i);
            }
        }
    }

    void Clear() {
        pending.clear();
    }
};

static SubObjectPendingQueue g_pendingSubObjects;
```

Integrate with the frame loop:

```cpp
void FrameUpdate(double dt) {
    // ... Service, sync_outbound, UpdateFrameEnd, UpdateFrameBegin ...

    // After UpdateFrameBegin (callbacks have fired), process pending
    g_pendingSubObjects.ProcessPending(g_client);

    // ... sync_inbound ...
}
```

Clean up on parent destruction:

```cpp
subs += client->OnObjectDestroyed.Subscribe(
    [](const SharedMode::ObjectRoot* obj, SharedMode::DestroyModes mode) {
        g_pendingSubObjects.RemoveForParent(obj->Id);
        // ... other cleanup ...
    }
);
```

## Dual Handle Pattern

When building a synchronizer that works with both root objects and sub-objects, maintain two handles:

| Handle | Type | Purpose |
|--------|------|---------|
| `rootHandle` | `ObjectRoot*` | Authority checks: `IsOwner()`, `GetOwner()`, `CanModify()` |
| `dataHandle` | `Object*` | Words buffer access: `Words.Ptr`, `SetSendUpdates()` |

For root objects, both point to the same `ObjectRoot*`. For sub-objects, the root handle points to the parent, and the data handle points to the child:

```cpp
class MySynchronizer {
    SharedMode::ObjectRoot* rootHandle = nullptr;  // Always a root
    SharedMode::Object* dataHandle = nullptr;       // Root or child

public:
    // Bind to a root object
    void BindRoot(SharedMode::ObjectRoot* root) {
        rootHandle = root;
        dataHandle = root;  // Same object
    }

    // Bind to a sub-object
    void BindChild(SharedMode::ObjectChild* child) {
        rootHandle = child->Root();  // Navigate to parent root
        dataHandle = child;          // Child for Words buffer access
    }

    bool HasAuthority(SharedMode::Client* client) const {
        // Always check authority against the root
        return client->IsOwner(rootHandle);
    }

    void SyncOutbound(SharedMode::Client* client) {
        if (!HasAuthority(client)) return;

        // Write to the data handle's Words buffer
        // (works identically for root and child)
        int32_t* words = dataHandle->Words.Ptr;
        int offset = 0;
        // ... write properties at offset ...
    }

    void SyncInbound(SharedMode::Client* client) {
        if (HasAuthority(client)) return;

        const int32_t* words = dataHandle->Words.Ptr;
        int offset = 0;
        // ... read properties from offset ...
    }
};
```

This pattern allows the sync loop to be completely agnostic about whether it is operating on a root object or a sub-object.

## Required Objects

A root object can declare a number of required sub-objects. The object is not considered "ready" until all required sub-objects have been created. This is useful for compound objects where the root and its children must all exist before gameplay begins.

### Creating with Required Objects

```cpp
// Create a root that requires 2 sub-objects
int32_t requiredCount = 2;

SharedMode::ObjectRoot* root = client->CreateObject(
    totalWords, typeRef, header, headerLen,
    sceneIndex, ownerMode,
    requiredCount  // last parameter
);
```

### Querying Required Objects

```cpp
// How many required sub-objects are declared?
int32_t count = root->RequiredObjectsCount();

// Get pointer to array of required ObjectIds
SharedMode::ObjectId* reqIds = root->RequiredObjects();
for (int32_t i = 0; i < count; i++) {
    printf("Required: origin=%u counter=%u\n",
           reqIds[i].Origin, reqIds[i].Counter);
}

// Check if a specific ObjectId is required
bool isReq = root->IsRequired(someId);
```

The required object IDs are stored in the Words buffer immediately after the ObjectTail.

## Destruction

### Destroying a Sub-Object Locally

```cpp
// Authority side: destroy a specific sub-object
bool success = client->DestroySubObjectLocal(child);
```

This removes the child from the parent's `SubObjects` list and fires `OnSubObjectDestroyed` on all clients.

### Cascade on Root Destroy

When a root object is destroyed (via `DestroyObjectLocal` or remotely), all its sub-objects are automatically destroyed. The `OnSubObjectDestroyed` callback fires for each child.

### Handling Destruction Callbacks

```cpp
subs += client->OnSubObjectDestroyed.Subscribe(
    [](SharedMode::ObjectChild* child, SharedMode::DestroyModes mode) {
        // 1. Free any string handles from the root's heap
        FreeChildStringHandles(child);

        // 2. Clean up engine representation
        if (child->Engine) {
            DestroyEngineObject(child->Engine);
            child->Engine = nullptr;
        }

        // 3. Unregister from bidirectional registry
        UnregisterByFusionId(child->Id);
    }
);
```

## String Heap Delegation

Sub-objects do **not** have their own `NetworkedStringHeap`. All string operations on a child delegate to the root's heap:

```cpp
// These work identically whether obj is ObjectRoot or ObjectChild.
// The Object base class routes through Root() internally.
SharedMode::StringHandle handle = childObj->AddString(
    reinterpret_cast<const PhotonCommon::CharType*>("hello")
);

SharedMode::StringMessage status;
const PhotonCommon::CharType* str = childObj->ResolveString(handle, status);

childObj->FreeString(handle);
```

**Implications:**
- All children share the root's string heap capacity
- String handles are root-scoped, not child-scoped
- **Destroying a child does NOT automatically free its strings from the root heap**

**Rule:** When destroying a sub-object, explicitly free all its string handles from the root's heap first. Track which handles belong to which child to avoid leaks.

```cpp
void FreeChildStringHandles(SharedMode::ObjectChild* child) {
    int32_t* words = child->Words.Ptr;
    int usable = static_cast<int>(child->Words.Length)
               - SharedMode::Object::ExtraTailWords;

    // Iterate through known string property offsets
    for (int stringOffset : GetStringPropertyOffsets(child->Type.Hash)) {
        if (stringOffset + 1 >= usable) break;

        SharedMode::StringHandle handle;
        handle.id = static_cast<uint32_t>(words[stringOffset]);
        handle.generation = static_cast<uint32_t>(words[stringOffset + 1]);

        if (handle.id != 0) {
            child->FreeString(handle);  // Delegates to root heap
        }
    }
}
```

## Finding Sub-Objects

The SDK provides several methods to query sub-objects:

```cpp
// Check if a root has any sub-objects
bool hasSubs = client->HasSubObjects(rootObj);

// Get all sub-object IDs for a root
const std::vector<SharedMode::ObjectId>& subIds = client->GetSubObject(rootObj);

// Find a specific sub-object by TargetObjectHash
SharedMode::Object* found = client->FindSubObjectWithHash(rootObj, targetHash);

// Find any object (root or child) by ObjectId
SharedMode::Object* obj = client->FindObject(objectId);
```

Iterate through a root's children:

```cpp
SharedMode::ObjectRoot* root = /* ... */;
for (const auto& subId : root->SubObjects) {
    SharedMode::Object* sub = client->FindObject(subId);
    if (sub) {
        SharedMode::ObjectChild* child = SharedMode::ObjectChild::Cast(sub);
        if (child) {
            // Process child...
        }
    }
}
```

## SDK API Reference

### Client Methods

| Method | Signature |
|--------|-----------|
| `CreateSubObject` | `ObjectChild* CreateSubObject(ObjectId parent, size_t words, const TypeRef& type, const CharType* header, size_t headerLength, uint32_t targetObjectHash, ObjectId id, ObjectSpecialFlags flags)` |
| `AddSubObject` | `bool AddSubObject(ObjectRoot* parent, ObjectChild* child)` |
| `DestroySubObjectLocal` | `bool DestroySubObjectLocal(ObjectChild* obj)` |
| `HasSubObjects` | `bool HasSubObjects(const Object* root)` |
| `GetSubObject` | `const vector<ObjectId>& GetSubObject(const Object* root)` |
| `FindSubObjectWithHash` | `Object* FindSubObjectWithHash(ObjectRoot* root, uint32_t hash)` |
| `GetNewObjectId` | `ObjectId GetNewObjectId()` |

### ObjectChild Members

| Member | Type | Description |
|--------|------|-------------|
| `Parent` | `ObjectId` | Parent root's ObjectId |
| `TargetObjectHash` | `uint32_t` | Secondary matching key for remote identification |

### ObjectChild Static Methods

| Method | Signature | Purpose |
|--------|-----------|---------|
| `GetParent` | `static ObjectId GetParent(const Object*)` | Get parent ID (safe for any Object) |
| `Is` | `static bool Is(const Object*)` | Check if Object is a child |
| `Cast` | `static ObjectChild* Cast(Object*)` | Safe downcast (returns nullptr if not child) |

## Next Steps

- [Engine Binding](engine-binding.md) -- Connecting SDK objects to engine representations
- [Object Sync Patterns](object-sync-patterns.md) -- Words buffer mechanics
- [Pitfalls](../pitfalls.md) -- Sub-object-specific pitfalls (string heap delegation, two-step creation)
