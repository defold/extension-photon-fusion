# Object API

Networked object types representing replicated entities. All classes live in the `SharedMode` namespace.

Header: `Types.h`

---

## Object (Base Class)

Abstract base class for all networked objects. Contains the replicated word buffer, string heap, and per-object metadata.

### Public Fields

```cpp
ObjectId Id{0, 0};                          // Unique network identifier
void *Engine{nullptr};                      // Opaque pointer to the engine-side object
ObjectType ObjectType{ObjectType::Base};    // Discriminator: Base, Child, or Root
bool HasValidData{false};                   // True once the object has received at least one state update
Data Header{};                              // Serialized creation header (type path, etc.)
TypeRef Type{};                             // Type descriptor (hash + word count)

BufferT<Word> Shadow{};                     // Previous-tick state for dirty detection
BufferT<Word> Words{};                      // Current replicated state buffer

ObjectSpecialFlags SpecialFlags{};          // Bitflags (IsRootTransform, IgnoreRootTransformProperties)
uint8_t SendFlags{0};                       // Per-packet send flags (create, string heap changes, etc.)

Tick RemoteTickSent{0};                     // Last tick sent to the server
Tick RemoteTickAcked{0};                    // Last tick acknowledged by the server

int32_t Status{0};                          // Object lifecycle status (OBJECT_STATUS_NEW / PENDING / CREATED)

NetworkedStringHeap StringHeap{1024};       // Per-object string heap for networked string properties
```

### Public Constants

```cpp
static constexpr size_t ExtraTailWords = sizeof(ObjectTail) / 4;  // Words reserved for the ObjectTail (6)
static constexpr double DynamicOwnerCooldownTime = 1.0 / 3;       // Ownership transfer cooldown in seconds
```

### Constructor

```cpp
explicit Object(SharedMode::Client *client);
```

### Public Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Root` | `virtual ObjectRoot *Root() = 0` | Returns the root object (self for roots, parent's root for children). |
| `GetBytesSendLastTick` | `uint32_t GetBytesSendLastTick() const` | Returns bytes sent for this object in the last tick. |
| `GetBytesReceivedLastTick` | `uint32_t GetBytesReceivedLastTick() const` | Returns bytes received for this object in the last tick. |
| `ConsumeBytesSendLastTick` | `uint32_t ConsumeBytesSendLastTick()` | Returns and resets the sent byte counter. |
| `ConsumeBytesReceivedLastTick` | `uint32_t ConsumeBytesReceivedLastTick()` | Returns and resets the received byte counter. |
| `ResetReceivedBytes` | `void ResetReceivedBytes()` | Resets the received byte counter to zero. |
| `SetHasValidData` | `void SetHasValidData(bool hasValidData)` | Mark whether the object has valid replicated data. |
| `SetSendUpdates` | `void SetSendUpdates(bool sendUpdates)` | Enable or disable outgoing state updates for this object. |
| `AddString` | `StringHandle AddString(const PhotonCommon::CharType *str)` | Allocate and replicate a string in this object's string heap. |
| `ResolveString` | `const PhotonCommon::CharType *ResolveString(const StringHandle &handle, StringMessage &OutStatus)` | Look up a string by handle. Sets `OutStatus` to indicate validity. |
| `FreeString` | `StringHandle FreeString(const StringHandle &handle)` | Free a string from the heap. Returns an invalidated handle. |
| `IsValidStringHandle` | `bool IsValidStringHandle(const StringHandle &handle)` | Returns `true` if the handle points to a live entry. |
| `GetStringLength` | `uint32_t GetStringLength(const StringHandle &handle)` | Returns the byte length of the string referenced by the handle. |
| `LogStringData` | `void LogStringData(const StringHandle &handle)` | Dump debug info about a string handle to the log. |

---

## ObjectRoot

Final class inheriting from `Object`. Represents a top-level networked entity that can own sub-objects.

### Additional Fields

```cpp
int LocalSendRate{1};                       // Client-local send rate divisor
double Time{0};                             // Last received server time for this object
PlayerId Owner{0};                          // Current owner PlayerId
ObjectOwnerModes OwnerMode{};               // Ownership model (Transaction, Dynamic, etc.)
uint32_t Scene{0};                          // Scene index this object belongs to

ObjectOwnerIntent OwnerIntent{0};           // Current ownership intent (WantOwner / DontWantOwner)
double OwnerIntentCooldown{0};              // Remaining cooldown before next ownership request

int32_t UpdatesReceived{0};                 // Total state updates received from the server
bool SentThisFrame{false};                  // True if state was sent in the current frame
bool ObjectReady{false};                    // True once all required objects are resolved

int32_t PluginVersion{1};                   // Server-side version counter
int32_t ClientVersion{1};                   // Client-side version counter
int32_t ClientBaseVersion{0};               // Base version for delta encoding

std::vector<ObjectId> SubObjects{};         // IDs of all child sub-objects
```

### Static Methods

```cpp
static bool Is(const Object *obj);
```
Returns `true` if the object is an `ObjectRoot`.

```cpp
static ObjectRoot *Cast(Object *obj);
static const ObjectRoot *Cast(const Object *obj);
```
Safe downcast from `Object*`. Returns `nullptr` if the object is not a root.

### Instance Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Root` | `ObjectRoot *Root() override` | Returns `this`. |
| `IsRequired` | `bool IsRequired(ObjectId id) const` | Returns `true` if `id` is in the required objects list. |
| `RequiredObjectsCount` | `int32_t RequiredObjectsCount() const` | Returns the number of required objects. |
| `RequiredObjects` | `ObjectId *RequiredObjects() const` | Returns a pointer to the required objects array (stored in the tail). |

---

## ObjectChild

Final class inheriting from `Object`. Represents a sub-object attached to a root.

### Additional Fields

```cpp
ObjectId Parent{0, 0};         // ObjectId of the parent root
uint32_t TargetObjectHash{0};  // Hash identifying which engine sub-object this maps to
```

### Static Methods

```cpp
static ObjectId GetParent(const Object *obj);
```
Returns the parent `ObjectId` if `obj` is a child, otherwise `ObjectId(0)`.

```cpp
static bool Is(const Object *obj);
```
Returns `true` if the object is an `ObjectChild`.

```cpp
static ObjectChild *Cast(Object *obj);
static const ObjectChild *Cast(const Object *obj);
```
Safe downcast from `Object*`. Returns `nullptr` if the object is not a child.

### Instance Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Root` | `ObjectRoot *Root() override` | Returns the parent root object via the client. |

---

## ObjectTail

Packed struct appended after the replicated word buffer of every object. Stored inline in the `Words` buffer.

```cpp
#pragma pack(push, 4)
struct ObjectTail {
    int32_t RequiredObjectsCount;   // Number of required object dependencies
    uint64_t InterestKey;           // Interest key value (0 = global)
    int32_t Destroyed;              // Non-zero if marked for destruction
    int32_t SendRate;               // Server-authoritative send rate
    int32_t Dummy;                  // Reserved padding
};
#pragma pack(pop)

static_assert(sizeof(ObjectTail) == 24);
```

| Field | Offset | Description |
|-------|--------|-------------|
| `RequiredObjectsCount` | 0 | Number of `ObjectId`s following the tail in the word buffer. |
| `InterestKey` | 4 | The interest key assigned to this object (see [Interest Keys](client-api.md#interest-keys)). |
| `Destroyed` | 12 | Non-zero when the object has been destroyed. |
| `SendRate` | 16 | Ticks between server sends (0 = default). |
| `Dummy` | 20 | Unused reserved field. |

---

## ObjectPacketEnvelope

Tracks which objects were included in a single outgoing packet, used for delivery/loss callbacks.

```cpp
class ObjectPacketEnvelope {
public:
    std::vector<std::tuple<ObjectId, Tick>> ObjectUpdates{};
};
```

| Field | Description |
|-------|-------------|
| `ObjectUpdates` | List of `(ObjectId, Tick)` pairs for each object update included in the packet. |
