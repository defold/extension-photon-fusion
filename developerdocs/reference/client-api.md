# Client API

`SharedMode::Client` -- the Fusion networking client. Manages networked objects, state synchronization, RPCs, ownership, and interest management.

Header: `Client.h`

## Construction / Destruction

```cpp
explicit Client(PhotonMatchmaking::RealtimeClient& realtimeClient);
~Client();
```

| Method | Description |
|--------|-------------|
| `Client(realtimeClient)` | Construct a Fusion client bound to an existing Realtime connection. |
| `~Client()` | Destroy the client and release all resources. |

## Lifecycle

```cpp
void Start();
void Stop();
void Shutdown();
```

| Method | Description |
|--------|-------------|
| `Start()` | Begin the Fusion session (sends config to server, starts synchronization). |
| `Stop()` | Gracefully stop the Fusion session. |
| `Shutdown()` | Tear down the client, destroy the Notify connection and all objects. |

## Frame Updates

```cpp
void UpdateFrameBegin(double dt);
void UpdateFrameEnd();
void UpdateServiceOnly();
```

| Method | Description |
|--------|-------------|
| `UpdateFrameBegin(dt)` | Begin a frame tick. Call once per frame before reading/writing object state. `dt` is the frame delta time in seconds. |
| `UpdateFrameEnd()` | End a frame tick. Queues dirty state and RPCs for sending. |
| `UpdateServiceOnly()` | Pump the network layer without processing a full frame (useful during loading). |

## Status Queries

```cpp
bool IsRunning() const;
bool IsMasterClient();
PlayerId LocalPlayerId();
int32_t PlayerCount() const;
double GetRtt() const;
PhotonMatchmaking::RealtimeClient& GetRealtimeClient();
static SdkVersion GetSdkVersion();
```

| Method | Description |
|--------|-------------|
| `IsRunning()` | Returns `true` if the Notify connection is established and the server config has been received. |
| `IsMasterClient()` | Returns `true` if the local player is the current master client. |
| `LocalPlayerId()` | Returns the local player's `PlayerId`. |
| `PlayerCount()` | Returns the number of players in the room. |
| `GetRtt()` | Returns the current round-trip time estimate in seconds. |
| `GetRealtimeClient()` | Returns a reference to the underlying `RealtimeClient`. |
| `GetSdkVersion()` | Returns the compiled SDK version (major, minor, patch, build, protocol). |

## Time Synchronization

```cpp
double NetworkTime() const;
double NetworkTimeScale() const;
double NetworkTimeDiff() const;
double GetTime(const Object *obj);
int GetSendTick() const;
int GetReceivedCounter() const;
```

| Method | Description |
|--------|-------------|
| `NetworkTime()` | Returns the synchronized network time in seconds. |
| `NetworkTimeScale()` | Returns the current clock scale factor used for time smoothing. |
| `NetworkTimeDiff()` | Returns the raw difference between local and server clocks. |
| `GetTime(obj)` | Returns the last received server time for the given object. |
| `GetSendTick()` | Returns the current outgoing tick counter. |
| `GetReceivedCounter()` | Returns the number of state packets received. |

## Object Queries

```cpp
Object *FindObject(ObjectId id) const;
ObjectRoot *FindObjectRoot(ObjectId id) const;
Object *FindSubObjectWithHash(ObjectRoot *Root, uint32_t subObjectHash) const;
std::unordered_map<ObjectId, Object *> &AllObjects();
std::unordered_map<ObjectId, ObjectRoot *> &AllRootObjects();
ObjectRoot *GetRoot(Object *obj) const;
const ObjectRoot *GetRoot(const Object *obj) const;
bool IsRoot(const Object *object);
bool HasSubObjects(const Object *Root);
const std::vector<ObjectId> &GetSubObject(const Object *Root);
bool HasBeenUpdatedByPlugin(Object *obj, bool reset);
```

| Method | Description |
|--------|-------------|
| `FindObject(id)` | Look up any object (root or child) by `ObjectId`. Returns `nullptr` if not found. |
| `FindObjectRoot(id)` | Look up a root object by `ObjectId`. Returns `nullptr` if not found. |
| `FindSubObjectWithHash(Root, subObjectHash)` | Find a sub-object of a root by its hash. Returns `nullptr` if not found. |
| `AllObjects()` | Returns a mutable reference to the map of all objects (roots and children). |
| `AllRootObjects()` | Returns a mutable reference to the map of all root objects. |
| `GetRoot(obj)` | Returns the root object for a given object (returns itself if already a root). |
| `IsRoot(object)` | Returns `true` if the object is a root object. |
| `HasSubObjects(Root)` | Returns `true` if the root has any sub-objects. |
| `GetSubObject(Root)` | Returns the list of sub-object IDs for a root. |
| `HasBeenUpdatedByPlugin(obj, reset)` | Returns `true` if the object received a plugin update. If `reset` is true, clears the flag. |

## Object Creation (Root)

```cpp
ObjectRoot *CreateSceneObject(bool &alreadyPopulated, size_t words, const TypeRef &type,
    const PhotonCommon::CharType *header, size_t headerLength,
    uint32_t scene, uint32_t id, ObjectOwnerModes ownerMode,
    int32_t requiredObjectsCount = 0);

ObjectRoot *CreateGlobalInstanceObject(bool &alreadyPopulated, size_t words, const TypeRef &type,
    const PhotonCommon::CharType *header, size_t headerLength,
    uint32_t scene, uint32_t id, ObjectOwnerModes ownerMode,
    int32_t requiredObjectsCount = 0);

ObjectRoot *CreateObject(size_t words, const TypeRef &type,
    const PhotonCommon::CharType *header, size_t headerLength,
    uint32_t scene, ObjectOwnerModes ownerMode,
    int32_t requiredObjectsCount = 0);

ObjectId GetNewObjectId();
```

| Method | Description |
|--------|-------------|
| `CreateSceneObject(...)` | Create or retrieve a scene-placed object (deterministic ID from scene+id). Sets `alreadyPopulated` if the object already existed. |
| `CreateGlobalInstanceObject(...)` | Create or retrieve a globally-unique instance object (singleton per scene+id). Sets `alreadyPopulated` if the object already existed. |
| `CreateObject(...)` | Create a dynamically-spawned root object. Allocates a new `ObjectId` automatically. |
| `GetNewObjectId()` | Generate the next unique `ObjectId` for the local player. |

## Object Creation (Sub-Objects)

```cpp
ObjectChild *CreateSubObject(ObjectId parent, size_t words, const TypeRef &type,
    const PhotonCommon::CharType *header, size_t headerLength,
    uint32_t targetObjectHash, ObjectId id, ObjectSpecialFlags SpecialFlags);

bool AddSubObject(ObjectRoot *ParentObject, ObjectChild *SubObject);
```

| Method | Description |
|--------|-------------|
| `CreateSubObject(...)` | Create a child object attached to a parent root. Uses the provided `ObjectId`. |
| `AddSubObject(ParentObject, SubObject)` | Register an existing sub-object under a parent root. |

## Object Destruction

```cpp
bool DestroyObjectLocal(ObjectRoot *obj, bool engineObjectAlreadyDestroyed);
bool DestroySubObjectLocal(ObjectChild *obj);
```

| Method | Description |
|--------|-------------|
| `DestroyObjectLocal(obj, engineObjectAlreadyDestroyed)` | Destroy a root object locally and notify the server. If `engineObjectAlreadyDestroyed` is true, skips engine-side cleanup. |
| `DestroySubObjectLocal(obj)` | Destroy a sub-object locally and notify the server. |

## RPCs

```cpp
Rpc CreateUserRpc(uint64_t id, PlayerId targetPlayer, ObjectId targetObject,
    uint64_t DescriptorTypeHash, uint64_t EventHash,
    const char *data, size_t dataLength);

bool SendUserRpc(const Rpc &rpc);
```

| Method | Description |
|--------|-------------|
| `CreateUserRpc(...)` | Construct an RPC message with the given ID, target, type/event hashes, and payload data. |
| `SendUserRpc(rpc)` | Queue an RPC for sending. Returns `true` on success. |

## Ownership

```cpp
PlayerId GetOwner(const Object *obj);
bool IsOwner(const Object *obj);
bool CanModify(const Object *obj);
bool HasOwner(const Object *obj) const;
void SetWantOwner(Object *obj);
void SetDontWantOwner(Object *obj);
void ClearOwnerCooldown(Object *obj);
ObjectOwnerModes SanitizeOwnerMode(ObjectOwnerModes ownerMode) const;
```

| Method | Description |
|--------|-------------|
| `GetOwner(obj)` | Returns the current owner `PlayerId` of the object. |
| `IsOwner(obj)` | Returns `true` if the local player owns this object. |
| `CanModify(obj)` | Returns `true` if the local player can write to this object's state. |
| `HasOwner(obj)` | Returns `true` if the object has any owner assigned. |
| `SetWantOwner(obj)` | Request ownership of a dynamic object. |
| `SetDontWantOwner(obj)` | Release ownership intent on a dynamic object. |
| `ClearOwnerCooldown(obj)` | Reset the ownership transfer cooldown timer for the object. |
| `SanitizeOwnerMode(ownerMode)` | Validate and return a safe owner mode for the current client state. |

## Send Rate

```cpp
void SetSendRate(const Object *obj, int32_t sendRate);
void ResetSendRate(const Object *obj);
void SetLocalSendRate(Object *obj, uint32_t sendRate);
```

| Method | Description |
|--------|-------------|
| `SetSendRate(obj, sendRate)` | Set the server-authoritative send rate for the object (ticks between sends). |
| `ResetSendRate(obj)` | Reset the object's send rate to the default. |
| `SetLocalSendRate(obj, sendRate)` | Set a client-local send rate divisor (skips N-1 out of N sends). |

## Interest Keys

```cpp
void SetGlobalInterestKey(Object *obj);
void SetUserInterestKey(Object *obj, uint64_t key);
void SetAreaInterestKey(Object *obj, uint64_t key);
bool HasSetInterestKey(Object *obj);
void ClearInterestKey(Object *obj);
InterestKeyType GetInterestKeyType(Object *obj);

std::map<uint64_t, uint8_t> &GetInterestKeys();
void ClearAllKeys();
void ClearAreaKeys();
void ClearUserKeys();
void SetAreaKeys(const std::vector<std::tuple<uint64_t, uint8_t>> &keys);
void AddUserKey(uint64_t key, uint8_t sendRate = 0);
void RemoveUserKey(uint64_t key);
std::vector<std::tuple<uint64_t, uint8_t>> GetAllAreaKeys() const;
std::vector<std::tuple<uint64_t, uint8_t>> GetAllUserKeys() const;
```

| Method | Description |
|--------|-------------|
| `SetGlobalInterestKey(obj)` | Mark the object as globally visible (always sent to all players). |
| `SetUserInterestKey(obj, key)` | Assign a user-defined interest key to the object. |
| `SetAreaInterestKey(obj, key)` | Assign an area-of-interest key to the object (server-managed spatial key). |
| `HasSetInterestKey(obj)` | Returns `true` if any interest key is assigned to the object. |
| `ClearInterestKey(obj)` | Remove the interest key from the object. |
| `GetInterestKeyType(obj)` | Returns the type of interest key assigned (`Global`, `Area`, or `User`). |
| `GetInterestKeys()` | Returns a reference to the full interest key map (key -> send rate). |
| `ClearAllKeys()` | Remove all subscribed interest keys (area and user). |
| `ClearAreaKeys()` | Remove all subscribed area interest keys. |
| `ClearUserKeys()` | Remove all subscribed user interest keys. |
| `SetAreaKeys(keys)` | Replace all area interest key subscriptions. Each tuple is `(key, sendRate)`. |
| `AddUserKey(key, sendRate)` | Subscribe to a user interest key with an optional send rate override. |
| `RemoveUserKey(key)` | Unsubscribe from a user interest key. |
| `GetAllAreaKeys()` | Returns all currently subscribed area keys with send rates. |
| `GetAllUserKeys()` | Returns all currently subscribed user keys with send rates. |

## Scene Management

```cpp
void ChangeScene(uint32_t index, uint32_t sequence, const PhotonCommon::CharType *data);
void StateUpdatesPause();
void StateUpdatesResume();
```

| Method | Description |
|--------|-------------|
| `ChangeScene(index, sequence, data)` | Initiate a scene change with the given index, sequence counter, and serialized scene path. |
| `StateUpdatesPause()` | Pause outgoing state updates (e.g., during a scene transition). |
| `StateUpdatesResume()` | Resume outgoing state updates after a pause. |

## Version

```cpp
static SdkVersion GetSdkVersion();
```

See [SdkVersion](types-api.md#sdkversion) for the struct definition.

---

## Broadcaster Events

All broadcasters use `PhotonCommon::Broadcaster<Signature>`. Subscribe via `broadcaster.Subscribe(callback)` which returns a `Subscription` RAII handle.

```cpp
PhotonCommon::Broadcaster<void()> OnFusionStart;
```
Fired when the server config is received and the Fusion session is ready.

```cpp
PhotonCommon::Broadcaster<void(std::string message)> OnForcedDisconnect;
```
Fired when the server forces a disconnect. `message` contains the reason.

```cpp
PhotonCommon::Broadcaster<void(Rpc &)> OnRpc;
```
Fired when a user RPC is received. See [Rpc](types-api.md#rpc-class).

```cpp
PhotonCommon::Broadcaster<void(uint32_t index, uint32_t sequence, Data &)> OnSceneChange;
```
Fired when a remote scene change is received. `index` is the scene index, `sequence` is the change counter, `data` contains the serialized scene path.

```cpp
PhotonCommon::Broadcaster<void(ObjectRoot *)> OnObjectOwnerChanged;
```
Fired when an object's owner changes.

```cpp
PhotonCommon::Broadcaster<void(ObjectRoot *)> OnOwnerWasGiven;
```
Fired when ownership of an object is given to the local player.

```cpp
PhotonCommon::Broadcaster<void(ObjectRoot *)> OnObjectPredictionOverride;
```
Fired when a prediction override is received from the server (authoritative state correction).

```cpp
PhotonCommon::Broadcaster<void(ObjectRoot *)> OnObjectReady;
```
Fired when all required objects for a root are resolved and the object is ready for use.

```cpp
PhotonCommon::Broadcaster<void(ObjectChild *)> OnSubObjectCreated;
```
Fired when a remote sub-object is created.

```cpp
PhotonCommon::Broadcaster<void(const ObjectRoot *, DestroyModes)> OnObjectDestroyed;
```
Fired when a root object is destroyed. `DestroyModes` indicates the reason.

```cpp
PhotonCommon::Broadcaster<void(ObjectChild *, DestroyModes)> OnSubObjectDestroyed;
```
Fired when a sub-object is destroyed.

```cpp
PhotonCommon::Broadcaster<void(ObjectRoot *)> OnInterestEnter;
```
Fired when a remote object enters the local player's area of interest.

```cpp
PhotonCommon::Broadcaster<void(ObjectRoot *)> OnInterestExit;
```
Fired when a remote object exits the local player's area of interest.

```cpp
PhotonCommon::Broadcaster<void(uint32_t, ObjectId)> OnDestroyedMapActor;
```
Fired for each destroyed scene actor during a scene change. First argument is the scene index.
