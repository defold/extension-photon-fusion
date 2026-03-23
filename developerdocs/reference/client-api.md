# SharedMode::Client API Reference

`SharedMode::Client` is the central class of the Fusion SDK. It manages the connection lifecycle, object creation and destruction, ownership, synchronization, RPCs, scene management, and Area of Interest. A single `Client` instance represents one participant in a Fusion session.

**Header**: `Client.h`

---

## Table of Contents

- [Construction and Lifecycle](#construction-and-lifecycle)
- [Connection](#connection)
- [Frame Loop](#frame-loop)
- [Running State](#running-state)
- [Network Time](#network-time)
- [Object Creation](#object-creation)
- [Object Queries](#object-queries)
- [Object Destruction](#object-destruction)
- [Ownership and Authority](#ownership-and-authority)
- [Area of Interest](#area-of-interest)
- [Scene Management](#scene-management)
- [RPCs](#rpcs)
- [Object Priority](#object-priority)
- [Miscellaneous](#miscellaneous)
- [Callbacks](#callbacks)
- [Internal Enums](#internal-enums)

---

## Construction and Lifecycle

### Constructor

```cpp
Client(const CharType* appId, const CharType* appVersion,
       const ExitGames::LoadBalancing::ClientConstructOptions& clientConstructOptions = ClientConstructOptions());
```

Creates a new Fusion client instance. This allocates the underlying Photon transport layer and initializes internal state. The client is **not** connected after construction; call `ConnectCloud()` or `ConnectLocal()` to begin connecting.

| Parameter | Type | Description |
|-----------|------|-------------|
| `appId` | `const CharType*` | Photon application ID (obtained from the Photon dashboard). |
| `appVersion` | `const CharType*` | Application version string. Clients with different versions cannot see each other. |
| `clientConstructOptions` | `const ClientConstructOptions&` | Optional Photon client construction options (e.g., region selection mode). |

**Notes**:
- `CharType` is `char8_t` (UTF-8). Use the `FUSION_STR()` macro for string literals.
- `clientConstructOptions` defaults to `ClientConstructOptions()`. Set `setRegionSelectionMode(1)` (Select) to specify a region directly in `ConnectCloud()`.

### Destructor

```cpp
~Client();
```

Destroys the client and releases all resources. Call `Shutdown()` before destruction if you need a clean disconnect.

---

### GetSdkVersion

```cpp
static SdkVersion GetSdkVersion();
```

Returns the compiled SDK version information.

**Returns**: A `SdkVersion` struct containing `Major`, `Minor`, `Patch`, `Build`, and `Protocol` fields.

---

### Shutdown

```cpp
void Shutdown(bool development);
```

Shuts down the client and disconnects from the Photon cloud. Cleans up internal state. Should be called before deleting the `Client` instance.

| Parameter | Type | Description |
|-----------|------|-------------|
| `development` | `bool` | If `true`, enables development-mode shutdown behavior. Use `false` for production. |

---

### Photon

```cpp
Photon& Photon();
```

Returns a reference to the underlying `SharedMode::Photon` transport layer. Used to access room operations, connection status, player queries, and event sending.

**Returns**: Reference to the internal `Photon` instance.

See: [photon-api.md](photon-api.md)

---

## Connection

### ConnectCloud

```cpp
void ConnectCloud(const CharType* region, const CharType* userId, const CharType* serverAddress);
```

Initiates a connection to the Photon Cloud. The connection process is asynchronous; poll status via `Photon().Status()` or `Photon().IsConnected()`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `region` | `const CharType*` | Region code (e.g., `"us"`, `"eu"`, `"asia"`). Used when region selection mode is set to Select (1). |
| `userId` | `const CharType*` | Unique user identifier for this client. |
| `serverAddress` | `const CharType*` | Custom server address, or empty string for default Photon Cloud. **Must not be `nullptr`** — use an empty string `FUSION_STR("")` instead. |

**Preconditions**: Client must be constructed but not yet connected.

**Notes**:
- Start frame processing (call `Photon().Service(true)` each frame) **before** calling `ConnectCloud()`, otherwise the connection state machine will not advance.
- The `serverAddress` parameter should be an empty string for standard Photon Cloud connections. Passing `nullptr` may cause a crash.

---

### ConnectLocal

```cpp
void ConnectLocal(const CharType* endpoint);
```

Connects to a local Photon server instance (e.g., for development or LAN testing).

| Parameter | Type | Description |
|-----------|------|-------------|
| `endpoint` | `const CharType*` | Server address (e.g., `"localhost:5055"`). |

---

## Frame Loop

The frame loop is the heart of Fusion's synchronization. It must be called every frame to process network traffic, apply incoming state, and send outgoing state.

The recommended call order within a single frame is:

1. `Photon().Service(true)` — process Photon transport
2. Sync outbound — authority clients write local state to Words buffers
3. `UpdateFrameEnd()` — package and send outgoing state
4. `UpdateFrameBegin(dt)` — process incoming state, fire callbacks
5. Sync inbound — non-authority clients apply received state

### UpdateFrameBegin

```cpp
void UpdateFrameBegin(double dt);
```

Processes incoming Fusion packets received since the last frame. This triggers all incoming-data callbacks: `OnObjectCreated`, `OnObjectDestroyed`, `OnObjectOwnerChanged`, `OnRpc`, `OnSubObjectCreated`, `OnSceneChange`, etc.

| Parameter | Type | Description |
|-----------|------|-------------|
| `dt` | `double` | Elapsed time in seconds since the last frame. Used for internal timing and rate control. |

**Preconditions**: Must be in a room (`IsRunning() == true`).

**Notes**:
- Callbacks set on the `Client` instance fire during this call. Ensure they are configured before entering a room.

---

### UpdateFrameEnd

```cpp
void UpdateFrameEnd();
```

Packages all dirty object state and queued RPCs into network packets and sends them. This is the "send" phase of the frame loop.

**Preconditions**: Must be in a room.

**Notes**:
- Only objects with `SendUpdates == true` and modified Words buffers are transmitted.
- The SDK internally delta-compresses state against each object's Shadow buffer.

---

### UpdateSocketOnly

```cpp
void UpdateSocketOnly();
```

Processes only the socket/transport layer without running the full Fusion frame logic. Useful for keeping the connection alive during loading screens or other periods where full state processing is not desired.

**Notes**:
- Pair with `StateUpdatesPause()` / `StateUpdatesResume()` to suppress state processing without dropping the connection.

---

### StateUpdatesPause

```cpp
void StateUpdatesPause();
```

Pauses processing of incoming state updates. The connection remains active, but object state is not applied. Useful during scene transitions.

---

### StateUpdatesResume

```cpp
void StateUpdatesResume();
```

Resumes processing of incoming state updates after a `StateUpdatesPause()` call.

---

## Running State

### IsRunning

```cpp
bool IsRunning() const;
```

Returns `true` if the client has received its configuration and is currently in a room. This is the main readiness check for Fusion operations.

**Returns**: `true` if `!_configEmpty && Photon().IsInRoom()`.

---

### IsMasterClient

```cpp
bool IsMasterClient();
```

Returns `true` if this client is the current master client of the room. The master client has special privileges: it is the default authority for unowned objects and can initiate scene changes.

**Returns**: `true` if this client is the room's master.

**Notes**:
- The master client role can transfer if the current master disconnects.

---

### LocalPlayerId

```cpp
PlayerId LocalPlayerId();
```

Returns the Photon player ID of this client.

**Returns**: A `PlayerId` (`uint32_t`) identifying this client within the room.

---

### PlayerCount

```cpp
int32_t PlayerCount() const;
```

Returns the number of players currently in the room.

**Returns**: Player count as `int32_t`.

---

### GetRtt

```cpp
double GetRtt() const;
```

Returns the current round-trip time (RTT) to the server in seconds.

**Returns**: RTT in seconds as `double`.

---

## Network Time

### NetworkTime

```cpp
double NetworkTime() const;
```

Returns the synchronized network time. This clock is shared across all clients in the room and advances in lockstep, adjusted for server-client time differences.

**Returns**: Network time in seconds as `double`.

---

### NetworkTimeScale

```cpp
double NetworkTimeScale() const;
```

Returns the current scale factor of network time. The SDK adjusts this to smoothly converge local time toward server time. A value of 1.0 means no adjustment.

**Returns**: Time scale factor as `double`.

---

### NetworkTimeDiff

```cpp
double NetworkTimeDiff() const;
```

Returns the raw time difference between the local clock and the server clock.

**Returns**: Time difference in seconds as `double`.

---

### GetTime

```cpp
double GetTime(const Object* obj);
```

Returns the last-received network time for a specific object. This is the time at which the most recent state update was sent by the authority.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object to query. |

**Returns**: Object's last update time as `double`.

---

## Object Creation

All object creation methods allocate a Words buffer of the specified size for property synchronization. The total buffer must include space for the `ObjectTail` structure (6 words) appended by the SDK. Engine integrations should compute: `totalWords = userWords + Object::ExtraTailWords`.

See: [../concepts/object-creation.md](../concepts/object-creation.md)

### CreateObject

```cpp
ObjectRoot* CreateObject(size_t words, const TypeRef& type, const CharType* header,
                         size_t headerLength, uint32_t scene, ObjectFlags objectFlags);
```

Creates a new **spawned object** on the network. The object is immediately owned by the creating client and broadcast to all other clients (or to those within AOI range). Remote clients receive this object via the `OnObjectCreated` callback.

| Parameter | Type | Description |
|-----------|------|-------------|
| `words` | `size_t` | Total size of the Words buffer in 32-bit words (user properties + tail). |
| `type` | `const TypeRef&` | Type descriptor: `Hash` identifies the object type for remote instantiation; `WordCount` must match `words`. |
| `header` | `const CharType*` | Serialized spawn data (e.g., initial property values). May be `nullptr` if `headerLength` is 0. |
| `headerLength` | `size_t` | Length of the header data in bytes. |
| `scene` | `uint32_t` | Scene sequence number. Typically the current scene sequence. |
| `objectFlags` | `ObjectFlags` | Combined settings, owner mode, and interest mode flags. |

**Returns**: Pointer to the newly created `ObjectRoot`, or `nullptr` on failure.

**Notes**:
- After creation, call `SetSendUpdates(false)` until initial state is written to the Words buffer, then `SetSendUpdates(true)` and `SetHasValidData(true)`.
- The `TypeRef::Hash` must match on all clients so remote spawners can identify which scene/prefab to instantiate.

---

### CreateSceneObject

```cpp
ObjectRoot* CreateSceneObject(bool& alreadyPopulated, size_t words, const TypeRef& type,
                              const CharType* header, size_t headerLength,
                              uint32_t scene, uint32_t id, ObjectFlags objectFlags);
```

Creates or retrieves a **scene object** — an object that exists as part of a loaded scene/level rather than being dynamically spawned. Scene objects use deterministic IDs so all clients converge on the same object identity.

| Parameter | Type | Description |
|-----------|------|-------------|
| `alreadyPopulated` | `bool&` | **Output**. Set to `true` if the object already existed in the Fusion state (i.e., another client already created it). Set to `false` if this is the first client to create it. |
| `words` | `size_t` | Total Words buffer size in 32-bit words. |
| `type` | `const TypeRef&` | Type descriptor. |
| `header` | `const CharType*` | Serialized spawn data. May be `nullptr`. |
| `headerLength` | `size_t` | Header data length in bytes. |
| `scene` | `uint32_t` | Scene sequence number. Must match the current scene. |
| `id` | `uint32_t` | Deterministic object ID. All clients loading the same scene must produce the same ID for each scene object. |
| `objectFlags` | `ObjectFlags` | Combined flags. |

**Returns**: Pointer to the `ObjectRoot`.

**Notes**:
- The `alreadyPopulated` output parameter controls the initial data direction:
  - `false` (first creator): Copy engine defaults into the Fusion Words buffer.
  - `true` (late joiner): Copy Fusion Words buffer into engine properties.
- Scene objects are **not** delivered through `OnObjectCreated`. Each client creates them locally after loading the scene.
- The deterministic `id` is typically derived from the scene node's name (e.g., `String::hash(nodeName)`).

---

### CreateGlobalInstanceObject

```cpp
ObjectRoot* CreateGlobalInstanceObject(bool& alreadyPopulated, size_t words, const TypeRef& type,
                                       const CharType* header, size_t headerLength,
                                       uint32_t scene, uint32_t id, ObjectFlags objectFlags);
```

Creates or retrieves a **global instance object** — similar to a scene object but persists across scene changes.

| Parameter | Type | Description |
|-----------|------|-------------|
| `alreadyPopulated` | `bool&` | Output: `true` if the object already existed in Fusion state. |
| `words` | `size_t` | Total Words buffer size. |
| `type` | `const TypeRef&` | Type descriptor. |
| `header` | `const CharType*` | Serialized spawn data. |
| `headerLength` | `size_t` | Header data length in bytes. |
| `scene` | `uint32_t` | Scene identifier. |
| `id` | `uint32_t` | Deterministic object ID. |
| `objectFlags` | `ObjectFlags` | Combined flags. Should include `ObjectSettingsFlags::IsGlobalInstance`. |

**Returns**: Pointer to the `ObjectRoot`.

**Notes**:
- Global instance objects survive scene transitions, unlike regular scene objects.
- The `ObjectSettingsFlags::IsGlobalInstance` flag should be set in the `objectFlags`.

---

### CreateSubObject

```cpp
ObjectChild* CreateSubObject(ObjectId parent, size_t words, const TypeRef& type,
                             const CharType* header, size_t headerLength,
                             uint32_t targetObjectHash, ObjectId id, ObjectSpecialFlags SpecialFlags);
```

Creates a **sub-object** (child object) associated with a parent root object. Sub-objects have their own Words buffer but share the parent's `StringHeap` and authority.

| Parameter | Type | Description |
|-----------|------|-------------|
| `parent` | `ObjectId` | ID of the parent root object. |
| `words` | `size_t` | Total Words buffer size for the sub-object. |
| `type` | `const TypeRef&` | Type descriptor for the sub-object. |
| `header` | `const CharType*` | Serialized spawn data. |
| `headerLength` | `size_t` | Header data length in bytes. |
| `targetObjectHash` | `uint32_t` | Hash identifying the sub-object type for remote matching. |
| `id` | `ObjectId` | Object ID for the sub-object. Obtain via `GetNewObjectId()`. |
| `SpecialFlags` | `ObjectSpecialFlags` | Special behavior flags (e.g., `IsRootTransform`). |

**Returns**: Pointer to the newly created `ObjectChild`, or `nullptr` on failure.

**Notes**:
- After creating a sub-object, you must call `AddSubObject()` to attach it to the parent.
- Remote clients receive sub-objects via the `OnSubObjectCreated` callback.
- The `targetObjectHash` must match on all clients for remote instantiation.

---

### AddSubObject

```cpp
bool AddSubObject(ObjectRoot* ParentObject, ObjectChild* SubObject);
```

Attaches a previously created sub-object to its parent root object. This registers the child in the parent's `SubObjects` list and enables network replication.

| Parameter | Type | Description |
|-----------|------|-------------|
| `ParentObject` | `ObjectRoot*` | The parent root object. |
| `SubObject` | `ObjectChild*` | The child object to attach (created via `CreateSubObject()`). |

**Returns**: `true` if the sub-object was successfully attached.

**Notes**:
- Must be called after `CreateSubObject()` for the sub-object to be replicated.
- The parent must be a valid, living root object.

---

### GetNewObjectId

```cpp
ObjectId GetNewObjectId();
```

Generates a new unique `ObjectId` for this client. The ID combines the local player ID as `Origin` and an auto-incrementing counter.

**Returns**: A new `ObjectId` unique to this client.

**Notes**:
- Used when creating sub-objects that need an explicit ID.
- For spawned objects (`CreateObject`), the SDK generates the ID internally.

---

## Object Queries

### FindObject

```cpp
Object* FindObject(ObjectId id) const;
```

Looks up any object (root or child) by its `ObjectId`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `ObjectId` | The object ID to search for. |

**Returns**: Pointer to the `Object`, or `nullptr` if not found.

---

### FindObjectRoot

```cpp
ObjectRoot* FindObjectRoot(ObjectId id) const;
```

Looks up a root object by its `ObjectId`. Returns `nullptr` if the ID refers to a child object or does not exist.

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `ObjectId` | The object ID to search for. |

**Returns**: Pointer to the `ObjectRoot`, or `nullptr` if not found or not a root.

---

### FindSubObjectWithHash

```cpp
Object* FindSubObjectWithHash(ObjectRoot* Root, uint32_t subObjectHash) const;
```

Searches a root object's children for a sub-object matching the given type hash.

| Parameter | Type | Description |
|-----------|------|-------------|
| `Root` | `ObjectRoot*` | The parent root object to search within. |
| `subObjectHash` | `uint32_t` | The `TargetObjectHash` to match. |

**Returns**: Pointer to the matching `Object`, or `nullptr` if not found.

---

### AllObjects

```cpp
std::unordered_map<ObjectId, Object*>& AllObjects();
```

Returns a mutable reference to the map of **all** objects (roots and children) currently tracked by the client.

**Returns**: Reference to the internal object map, keyed by `ObjectId`.

---

### AllRootObjects

```cpp
std::unordered_map<ObjectId, ObjectRoot*>& AllRootObjects();
```

Returns a mutable reference to the map of **root** objects only.

**Returns**: Reference to the internal root object map.

---

### GetRoot

```cpp
ObjectRoot* GetRoot(Object* obj) const;
const ObjectRoot* GetRoot(const Object* obj) const;
```

Returns the root object for a given object. If `obj` is already a root, returns it directly. If `obj` is a child, returns its parent root.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `Object*` or `const Object*` | The object to query. |

**Returns**: Pointer to the root `ObjectRoot`.

---

### IsRoot

```cpp
bool IsRoot(const Object* object);
```

Checks whether an object is a root object (as opposed to a child/sub-object).

| Parameter | Type | Description |
|-----------|------|-------------|
| `object` | `const Object*` | The object to check. |

**Returns**: `true` if the object is an `ObjectRoot`.

---

### HasSubObjects

```cpp
bool HasSubObjects(const Object* Root);
```

Checks whether a root object has any attached sub-objects.

| Parameter | Type | Description |
|-----------|------|-------------|
| `Root` | `const Object*` | The root object to check. |

**Returns**: `true` if the object has one or more sub-objects.

---

### GetSubObject

```cpp
const std::vector<ObjectId>& GetSubObject(const Object* Root);
```

Returns the list of sub-object IDs attached to a root object.

| Parameter | Type | Description |
|-----------|------|-------------|
| `Root` | `const Object*` | The root object to query. |

**Returns**: Const reference to the vector of child `ObjectId` values.

---

### HasBeenUpdatedByPlugin

```cpp
bool HasBeenUpdatedByPlugin(Object* obj);
```

Checks whether the Fusion server plugin has modified this object's state since the last check. Used to detect server-authoritative overrides.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `Object*` | The object to query. |

**Returns**: `true` if the plugin has written to the object's Words buffer.

---

## Object Destruction

### DestroyObjectLocal

```cpp
bool DestroyObjectLocal(ObjectRoot* obj, bool engineObjectAlreadyDestroyed);
```

Destroys a root object and broadcasts the destruction to all clients. The object is removed from the Fusion state and remote clients receive the `OnObjectDestroyed` callback.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `ObjectRoot*` | The root object to destroy. |
| `engineObjectAlreadyDestroyed` | `bool` | Set to `true` if the engine-side representation (e.g., Godot Node) has already been freed. Set to `false` if the SDK should notify the engine to clean up. |

**Returns**: `true` if the object was successfully destroyed.

**Notes**:
- Only the owner or master client should destroy objects.
- Sub-objects attached to this root are also destroyed.

---

## Ownership and Authority

### IsOwner

```cpp
bool IsOwner(const Object* obj);
```

Checks whether this client is the current owner (authority) of the given object.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object to check. |

**Returns**: `true` if this client owns the object.

**Notes**:
- For child objects, ownership is determined by the parent root's owner.

---

### CanModify

```cpp
bool CanModify(const Object* obj);
```

Checks whether this client has permission to modify the object's state. This is a superset of `IsOwner()` — the master client can always modify unowned objects.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object to check. |

**Returns**: `true` if this client can write to the object's Words buffer.

---

### HasOwner

```cpp
bool HasOwner(const Object* obj) const;
```

Checks whether the object currently has any owner assigned.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object to check. |

**Returns**: `true` if `obj->Owner != 0`.

---

### GetOwner

```cpp
PlayerId GetOwner(const Object* obj);
```

Returns the player ID of the object's current owner.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object to query. |

**Returns**: The owner's `PlayerId`, or `0` if unowned.

---

### SetWantOwner

```cpp
void SetWantOwner(Object* obj);
```

Signals that this client wants to acquire ownership of the specified object. Used with `ObjectOwnerModes::Dynamic` objects.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `Object*` | The object to request ownership of. |

**Notes**:
- Ownership transfer is not immediate. The SDK processes ownership requests during the frame loop.
- There is a cooldown period (`DynamicOwnerCooldownTime = 1/3 second`) between ownership transfers.

See: [../concepts/ownership.md](../concepts/ownership.md)

---

### SetDontWantOwner

```cpp
void SetDontWantOwner(Object* obj);
```

Signals that this client no longer wants ownership of the specified object. If this client is the current owner, ownership may transfer to another client that has called `SetWantOwner()`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `Object*` | The object to release ownership intent for. |

---

### ClearOwnerCooldown

```cpp
void ClearOwnerCooldown(Object* obj);
```

Clears the ownership transfer cooldown for an object, allowing immediate ownership changes.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `Object*` | The object whose cooldown to clear. |

---

### SanitizeFlags

```cpp
ObjectFlags SanitizeFlags(ObjectFlags flags) const;
```

Validates and normalizes object flags. Ensures flag combinations are consistent (e.g., AOI interest mode requires AOI to be enabled).

| Parameter | Type | Description |
|-----------|------|-------------|
| `flags` | `ObjectFlags` | The flags to sanitize. |

**Returns**: A sanitized copy of the flags.

---

## Area of Interest

Area of Interest (AOI) limits which objects a client receives state updates for, based on spatial proximity. When AOI is enabled, objects outside a client's interest region are not synchronized.

### AreaOfInterestUsed

```cpp
bool AreaOfInterestUsed() const;
```

Returns whether AOI is enabled for this session.

**Returns**: `true` if Area of Interest filtering is active.

---

### AreaOfInterestCellSize

```cpp
int32_t AreaOfInterestCellSize() const;
```

Returns the AOI grid cell size configured for this session.

**Returns**: Cell size in world units.

---

### GetAreaOfInterestLocations

```cpp
std::set<AOILocation>& GetAreaOfInterestLocations();
```

Returns the set of AOI grid cells this client is currently interested in. Modifying this set changes which objects the client receives.

**Returns**: Mutable reference to the set of `AOILocation` values.

---

### CalculateAreaOfInterestLocation

```cpp
AOILocation CalculateAreaOfInterestLocation(double x, double y, double z) const;
```

Converts a world-space position to the corresponding AOI grid cell location.

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | `double` | World X coordinate. |
| `y` | `double` | World Y coordinate. |
| `z` | `double` | World Z coordinate. |

**Returns**: The `AOILocation` grid cell containing the given position.

---

### SetAreaOfInterestLocation

```cpp
void SetAreaOfInterestLocation(const Object* obj, AOILocation location);
```

Sets the AOI location for a specific object. This determines which grid cell the object occupies for interest management purposes.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object to position in the AOI grid. |
| `location` | `AOILocation` | The grid cell to assign. |

**Notes**:
- This writes AOI coordinates to the object's tail area (the last 6 words of the Words buffer).
- Only the authority client should set an object's AOI location.

---

## Scene Management

### ChangeScene

```cpp
void ChangeScene(uint32_t index, uint32_t sequence, const CharType* data);
```

Broadcasts a scene change to all clients. The master client calls this to initiate a synchronized scene transition. Remote clients receive the `OnSceneChange` callback.

| Parameter | Type | Description |
|-----------|------|-------------|
| `index` | `uint32_t` | Scene index identifier. |
| `sequence` | `uint32_t` | Monotonically increasing scene sequence number. Must be greater than the previous sequence to be accepted. |
| `data` | `const CharType*` | Scene path or identifier string, transmitted to remote clients. |

**Notes**:
- The `OnSceneChange` callback does **not** fire on the sending client. The sender must handle local scene loading separately.
- Scene sequence is used to detect and discard stale scene change messages.
- Scene objects created with `CreateSceneObject()` are associated with the current scene sequence.

---

## RPCs

### CreateUserRpc

```cpp
Rpc CreateUserRpc(uint64_t id, PlayerId targetPlayer, ObjectId targetObject,
                  uint64 DescriptorTypeHash, uint64 EventHash,
                  const char* data, size_t dataLength);
```

Constructs an RPC message. Does not send it — call `SendUserRpc()` to transmit.

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `uint64_t` | RPC identifier. Must be > 1023 (values 1-1023 are reserved for internal RPCs). |
| `targetPlayer` | `PlayerId` | Target player: `0` = all players, specific player ID = that player only, `MasterClientPlayerId` = master client, `ObjectOwnerPlayerId` = object owner. |
| `targetObject` | `ObjectId` | Target object for object-targeted RPCs. Use `ObjectId(0, 0)` for broadcast RPCs (not tied to any object). |
| `DescriptorTypeHash` | `uint64` | Type descriptor hash for the RPC. Can be `0` if not using typed dispatch. |
| `EventHash` | `uint64` | Event hash for method resolution on the receiver. Can be `0` if using ID-based dispatch. |
| `data` | `const char*` | Serialized payload data. |
| `dataLength` | `size_t` | Length of payload in bytes. |

**Returns**: A fully constructed `Rpc` struct ready for `SendUserRpc()`.

**Notes**:
- Special `PlayerId` constants for `targetPlayer`:
  - `MasterClientPlayerId` (0xFFFFFFFF): Routes to the current master client.
  - `PluginPlayerId` (0xFFFFFFFE): Routes to the server plugin.
  - `ObjectOwnerPlayerId` (0xFFFFFFFD): Routes to the object's owner.

---

### SendUserRpc

```cpp
bool SendUserRpc(const Rpc& rpc);
```

Sends an RPC over the network. The RPC is queued and transmitted during `UpdateFrameEnd()`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `rpc` | `const Rpc&` | The RPC message to send (created via `CreateUserRpc()`). |

**Returns**: `true` if the RPC was successfully queued.

**Notes**:
- RPCs are delivered via the `OnRpc` callback on receiving clients.
- Delivery is reliable by default through the Notify protocol layer.

---

## Object Priority

### SetObjectPriority

```cpp
void SetObjectPriority(ObjectId id, int32_t priority);
```

Sets the send priority for an object. Higher-priority objects are transmitted first when bandwidth is constrained.

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `ObjectId` | The object to prioritize. |
| `priority` | `int32_t` | Priority value. Higher values = higher priority. Default is 0. |

---

## Miscellaneous

### GetTail (static, private)

```cpp
static ObjectTail& GetTail(const Object* obj);
```

Returns a reference to the `ObjectTail` structure at the end of an object's Words buffer. This is an internal method used by the SDK to access AOI and Destroyed fields.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object whose tail to access. |

**Returns**: Reference to the `ObjectTail` at `Words[Length - ExtraTailWords]`.

**Notes**:
- This is a `private` method, not part of the public API. Documented here for completeness.
- Engine integrations must **never** write to the tail area of the Words buffer.

---

## Callbacks

All callbacks are `std::function` members set directly on the `Client` instance. They fire during `UpdateFrameBegin()` when processing incoming network data.

### OnRoomJoin

```cpp
std::function<void()> OnRoomJoin;
```

Fires when the client successfully joins a room.

---

### OnRoomLeave

```cpp
std::function<void()> OnRoomLeave;
```

Fires when the client leaves a room (voluntarily or due to disconnection).

---

### OnObjectCreated

```cpp
std::function<void(ObjectRoot*)> OnObjectCreated;
```

Fires when a **remote** client creates a spawned object that this client should instantiate locally. Does **not** fire for locally created objects or scene objects.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `ObjectRoot*` | The newly created remote object. Access `Type.Hash` to determine what to instantiate, and `Header` for spawn data. |

---

### OnSubObjectCreated

```cpp
std::function<void(ObjectChild*)> OnSubObjectCreated;
```

Fires when a remote client creates a sub-object. The engine should find the parent's representation and instantiate the child.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `ObjectChild*` | The newly created child object. Use `ObjectChild::GetParent()` to find the parent ID, and `TargetObjectHash` to identify the sub-object type. |

---

### OnObjectDestroyed

```cpp
std::function<void(const ObjectRoot*, DestroyModes)> OnObjectDestroyed;
```

Fires when an object is destroyed, whether locally or remotely. The engine should clean up its representation.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const ObjectRoot*` | The destroyed object. The pointer remains valid during the callback but is freed afterward. |
| `mode` | `DestroyModes` | Reason for destruction: `Local`, `Remote`, `SceneChange`, or `Shutdown`. |

---

### OnObjectOwnerChanged

```cpp
std::function<void(ObjectRoot*)> OnObjectOwnerChanged;
```

Fires when an object's owner changes. The engine should update authority state (e.g., switch between sync_inbound and sync_outbound).

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `ObjectRoot*` | The object whose ownership changed. Read `obj->Owner` for the new owner. |

---

### OnObjectPredictionOverride

```cpp
std::function<void(ObjectRoot*)> OnObjectPredictionOverride;
```

Fires when the server overrides a client's predicted state for an object.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `ObjectRoot*` | The object whose state was overridden. |

---

### OnRpc

```cpp
std::function<void(Rpc&)> OnRpc;
```

Fires when an RPC is received from any client. The engine should dispatch based on `TargetObject` (object-targeted vs broadcast) and `Id` (method identifier).

| Parameter | Type | Description |
|-----------|------|-------------|
| `rpc` | `Rpc&` | The received RPC. Contains `Id`, `OriginPlayer`, `TargetPlayer`, `TargetObject`, `Bytes` (payload), and optional `DescriptorTypeHash`/`EventHash`. |

---

### OnSceneChange

```cpp
std::function<void(uint32_t index, uint32_t sequence, Data&)> OnSceneChange;
```

Fires on remote clients when the master client calls `ChangeScene()`. Does **not** fire on the sending client.

| Parameter | Type | Description |
|-----------|------|-------------|
| `index` | `uint32_t` | Scene index. |
| `sequence` | `uint32_t` | Scene sequence number (monotonically increasing). |
| `data` | `Data&` | Scene path or identifier, as raw bytes. |

---

## Internal Enums

### DestroyModes

```cpp
enum class DestroyModes {
    Local      = 0,  // Object destroyed by local client
    Remote     = 1,  // Object destroyed by remote client
    SceneChange = 2, // Object destroyed due to scene transition
    Shutdown   = 3   // Object destroyed during client shutdown
};
```

### LogLevel

```cpp
enum LogLevel : uint8_t {
    Trace   = 1 << 0,  // 0x01
    Debug   = 1 << 1,  // 0x02
    Info    = 1 << 2,  // 0x04
    Warning = 1 << 3,  // 0x08
    Error   = 1 << 4   // 0x10
};
```

A bitmask enum for configuring SDK log output. Multiple levels can be combined with bitwise OR.
