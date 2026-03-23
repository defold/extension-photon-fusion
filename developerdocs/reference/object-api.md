# Object, ObjectRoot, ObjectChild, ObjectTail API Reference

The object classes form the data model for all networked entities in Fusion. `Object` is the abstract base class. `ObjectRoot` represents top-level networked objects (spawned, scene, or global). `ObjectChild` represents sub-objects attached to a root. `ObjectTail` is a fixed-layout structure occupying the last 6 words of every object's Words buffer.

**Header**: `Types.h`

---

## Table of Contents

- [Object (Base Class)](#object-base-class)
  - [Constants](#constants)
  - [Fields](#fields)
  - [Methods](#methods)
  - [String Operations](#string-operations)
- [ObjectRoot](#objectroot)
  - [Fields](#objectroot-fields)
  - [Methods](#objectroot-methods)
  - [Static Helpers](#objectroot-static-helpers)
- [ObjectChild](#objectchild)
  - [Fields](#objectchild-fields)
  - [Methods](#objectchild-methods)
  - [Static Helpers](#objectchild-static-helpers)
- [ObjectTail](#objecttail)
  - [Layout](#objecttail-layout)
- [ObjectType Enum](#objecttype-enum)

---

## Object (Base Class)

```cpp
class Object {
    friend class Client;
    // ...
public:
    // Fields and methods below
};
```

`Object` is the abstract base class for all networked objects. It owns the Words buffer (the replicated state), the Shadow buffer (previous-frame state for delta detection), and provides string heap operations. You never instantiate `Object` directly; use `ObjectRoot` or `ObjectChild`.

### Constants

#### ExtraTailWords

```cpp
static constexpr size_t ExtraTailWords = sizeof(ObjectTail) / 4;  // = 6
```

The number of 32-bit words reserved at the end of every Words buffer for the `ObjectTail` structure. When computing total Words buffer size, add this to the user property word count: `totalWords = userWords + ExtraTailWords`.

---

#### DynamicOwnerCooldownTime

```cpp
static constexpr double DynamicOwnerCooldownTime = 1.0 / 3;
```

The cooldown period (in seconds) between ownership transfers for objects using `ObjectOwnerModes::Dynamic`. Prevents ownership from ping-ponging between clients.

---

### Fields

#### Id

```cpp
ObjectId Id{0, 0};
```

The unique network identifier for this object. Composed of `Origin` (creating player's ID) and `Counter` (per-player auto-increment). An ID of `{0, 0}` indicates an uninitialized or invalid object.

---

#### Engine

```cpp
void* Engine{nullptr};
```

An opaque pointer to the engine-side representation of this object (e.g., a Godot `Node*` or Unreal `AActor*`). Set by the engine integration after instantiation. The SDK does not use this field; it exists for engine convenience.

---

#### ObjectType

```cpp
ObjectType ObjectType{ObjectType::Base};
```

Discriminator indicating whether this is a `Base` (abstract, should not appear at runtime), `Root`, or `Child` object. Set automatically by the `ObjectRoot` and `ObjectChild` constructors.

---

#### HasValidData

```cpp
bool HasValidData{false};
```

Indicates whether the Words buffer contains valid, initialized data. The engine should set this to `true` after writing initial state. Objects with `HasValidData == false` may be skipped during synchronization.

---

#### Header

```cpp
Data Header{};
```

The spawn/creation data sent at object creation time. Contains serialized initial property values as raw bytes. Available on remote clients via the `OnObjectCreated` callback. This data is separate from the Words buffer and is only transmitted once.

---

#### Type

```cpp
TypeRef Type{};
```

The type descriptor for this object. Contains `Hash` (a 64-bit identifier matching the object across clients) and `WordCount` (the total Words buffer size including tail).

---

#### Shadow

```cpp
BufferT<Word> Shadow{};
```

A copy of the Words buffer from the last time state was sent. Used internally by the SDK for delta compression: only words that differ between `Words` and `Shadow` are transmitted.

---

#### Words

```cpp
BufferT<Word> Words{};
```

The primary replicated state buffer. This is the array of 32-bit words that the engine reads from (sync_inbound) and writes to (sync_outbound). The layout is defined by the engine's property configuration.

**Structure**: `[user properties...] [ObjectTail (6 words)]`

The last `ExtraTailWords` (6) words are reserved for `ObjectTail` and **must not** be written by engine code.

---

#### SpecialFlags

```cpp
ObjectSpecialFlags SpecialFlags{};
```

Special behavior flags. See [ObjectSpecialFlags](#objectspecialflags).

---

### Methods

#### Constructor

```cpp
explicit Object(SharedMode::Client* client);
```

Constructs a base Object. This is `protected` in practice -- only `ObjectRoot` and `ObjectChild` constructors call it.

| Parameter | Type | Description |
|-----------|------|-------------|
| `client` | `Client*` | The owning client instance. |

---

#### Destructor

```cpp
virtual ~Object() = default;
```

Virtual destructor for proper cleanup through base pointer.

---

#### SetHasValidData

```cpp
void SetHasValidData(const bool hasValidData);
```

Sets the `HasValidData` flag. Call with `true` after initial state has been written to the Words buffer.

| Parameter | Type | Description |
|-----------|------|-------------|
| `hasValidData` | `bool` | Whether the Words buffer is initialized. |

---

#### SetSendUpdates

```cpp
void SetSendUpdates(bool sendUpdates);
```

Controls whether this object's state is included in outgoing network packets. Set to `false` during initialization to prevent sending incomplete state, then `true` once the Words buffer is fully populated.

| Parameter | Type | Description |
|-----------|------|-------------|
| `sendUpdates` | `bool` | Whether to send state updates for this object. |

---

#### Root (pure virtual)

```cpp
virtual ObjectRoot* Root() = 0;
```

Returns the root object for this entity. For `ObjectRoot`, returns `this`. For `ObjectChild`, returns the parent root.

**Returns**: Pointer to the `ObjectRoot` that this object belongs to.

---

### String Operations

String operations delegate to the `NetworkedStringHeap` on the root object. For `ObjectChild`, these methods traverse to the parent root's heap. Strings are replicated automatically as part of the heap data.

See: [stringheap-api.md](stringheap-api.md)

#### AddString

```cpp
StringHandle AddString(const CharType* str);
```

Allocates a string in the networked string heap and returns a handle.

| Parameter | Type | Description |
|-----------|------|-------------|
| `str` | `const CharType*` | The UTF-8 string to store. |

**Returns**: A `StringHandle` with `id` and `generation` fields. Store this handle in the Words buffer (2 words) for replication.

**Notes**:
- For empty or null strings, returns a handle with `id == 0` (invalid handle).
- The string data is replicated to all clients via the heap synchronization protocol.

---

#### ResolveString

```cpp
const CharType* ResolveString(const StringHandle& handle, StringMessage& OutStatus);
```

Retrieves the string data for a given handle.

| Parameter | Type | Description |
|-----------|------|-------------|
| `handle` | `const StringHandle&` | The handle obtained from `AddString()`. |
| `OutStatus` | `StringMessage&` | **Output**. Set to `StringMessage::Valid` on success, or an error code on failure. |

**Returns**: Pointer to the string data, or `nullptr` if the handle is invalid.

**Possible `OutStatus` values**:
| Value | Meaning |
|-------|---------|
| `Valid` | String resolved successfully. |
| `NotALiveEntry` | The entry has been freed. |
| `WrongGeneration` | Handle refers to a previous allocation (stale). |
| `OutOfRange` | Handle ID exceeds the entry table size. |
| `WrongSize` | Internal heap inconsistency. |
| `EmptyString` | Entry exists but has zero length. |
| `InvalidHandle` | Handle has `id == 0`. |

---

#### FreeString

```cpp
StringHandle FreeString(const StringHandle& handle);
```

Frees a string from the heap and returns an invalidated handle. Must be called when a replicated string property changes to avoid leaking heap space.

| Parameter | Type | Description |
|-----------|------|-------------|
| `handle` | `const StringHandle&` | The handle to free. |

**Returns**: An invalidated `StringHandle` (typically `{0, 0}`).

---

#### GetStringLength

```cpp
uint32_t GetStringLength(const StringHandle& handle);
```

Returns the byte length of the string stored at the given handle.

| Parameter | Type | Description |
|-----------|------|-------------|
| `handle` | `const StringHandle&` | The handle to query. |

**Returns**: String length in bytes, or 0 if the handle is invalid.

---

#### LogStringData

```cpp
void LogStringData(const StringHandle& handle);
```

Outputs debug information about the given string handle to the log.

| Parameter | Type | Description |
|-----------|------|-------------|
| `handle` | `const StringHandle&` | The handle to inspect. |

---

## ObjectRoot

```cpp
class ObjectRoot final : public Object {
public:
    // ...
};
```

`ObjectRoot` represents a top-level networked object. Root objects own a `NetworkedStringHeap`, track ownership, maintain sub-object lists, and carry per-object metadata (time, flags, scene, version counters).

### ObjectRoot Fields

#### Time

```cpp
double Time{0};
```

The network time at which this object's state was last sent by the authority. Used by remote clients to timestamp received state.

---

#### Owner

```cpp
PlayerId Owner{0};
```

The player ID of the client that currently owns (has authority over) this object. A value of `0` means the object is unowned.

---

#### Flags

```cpp
ObjectFlags Flags{0};
```

Combined object settings: `SettingsFlags`, `OwnerMode`, and `InterestMode`. These are set at creation time and define the object's ownership and visibility behavior.

---

#### Scene

```cpp
uint32_t Scene{0};
```

The scene sequence number this object was created in. Used to associate objects with specific scenes.

---

#### OwnerIntent

```cpp
ObjectOwnerIntent OwnerIntent{0};
```

This client's ownership intent for the object: `DontWantOwner` or `WantOwner`. Set via `Client::SetWantOwner()` / `Client::SetDontWantOwner()`.

---

#### OwnerIntentCooldown

```cpp
double OwnerIntentCooldown{0};
```

Time remaining before this client can change ownership intent again. Decremented each frame by the SDK.

---

#### RemoteTickSent

```cpp
Tick RemoteTickSent{0};
```

The tick number of the most recently sent state update for this object.

---

#### RemoteTickAcked

```cpp
Tick RemoteTickAcked{0};
```

The tick number of the most recently acknowledged state update. Used for delta compression: only state changes since this tick need to be retransmitted.

---

#### Status

```cpp
int32_t Status{0};
```

Internal object lifecycle status:

| Value | Constant | Meaning |
|-------|----------|---------|
| 0 | `OBJECT_STATUS_NEW` | Newly allocated, not yet fully initialized. |
| 1 | `OBJECT_STATUS_PENDING` | Waiting for initial data. |
| 2 | `OBJECT_STATUS_CREATED` | Fully created and active. |

---

#### UpdatesReceived

```cpp
int32_t UpdatesReceived{0};
```

Counter of state updates received from the network for this object.

---

#### UpdatesInFlight

```cpp
int32_t UpdatesInFlight{0};
```

Counter of state updates sent but not yet acknowledged.

---

#### PluginVersion

```cpp
int32_t PluginVersion{1};
```

Version counter for server plugin modifications to this object.

---

#### ClientVersion

```cpp
int32_t ClientVersion{1};
```

Version counter for the owning client's modifications.

---

#### ClientBaseVersion

```cpp
int32_t ClientBaseVersion{0};
```

Baseline version used for detecting version conflicts between clients.

---

#### SubObjects

```cpp
std::vector<ObjectId> SubObjects{};
```

List of child object IDs attached to this root. Populated when `Client::AddSubObject()` is called.

---

#### StringHeap

```cpp
NetworkedStringHeap StringHeap{1024};
```

The per-object string heap for networked string replication. Initialized with 1024 bytes of heap space. All string operations on this root and its children delegate to this heap.

See: [stringheap-api.md](stringheap-api.md)

---

### ObjectRoot Methods

#### Constructor

```cpp
explicit ObjectRoot(SharedMode::Client* client);
```

Constructs a root object. Sets `ObjectType` to `ObjectType::Root`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `client` | `Client*` | The owning client instance. |

---

#### Root

```cpp
ObjectRoot* Root() override;
```

Returns `this`. A root object is its own root.

**Returns**: `this`.

---

### ObjectRoot Static Helpers

#### Is

```cpp
static bool Is(const Object* obj);
```

Checks whether the given object is a root object.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object to check. |

**Returns**: `true` if `obj` is non-null and `obj->ObjectType == ObjectType::Root`.

---

#### Cast (mutable)

```cpp
static ObjectRoot* Cast(Object* obj);
```

Safely downcasts an `Object*` to `ObjectRoot*`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `Object*` | The object to cast. |

**Returns**: The downcasted pointer, or `nullptr` if `obj` is null or not a root.

---

#### Cast (const)

```cpp
static const ObjectRoot* Cast(const Object* obj);
```

Safely downcasts a `const Object*` to `const ObjectRoot*`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object to cast. |

**Returns**: The downcasted pointer, or `nullptr` if `obj` is null or not a root.

---

## ObjectChild

```cpp
class ObjectChild final : public Object {
public:
    // ...
};
```

`ObjectChild` represents a sub-object attached to a parent `ObjectRoot`. Child objects have their own Words buffer and ID, but share the parent root's `NetworkedStringHeap` and authority. They cannot own themselves — authority is always derived from the root.

### ObjectChild Fields

#### Parent

```cpp
ObjectId Parent{0, 0};
```

The `ObjectId` of the parent root object this child is attached to.

---

#### TargetObjectHash

```cpp
uint32_t TargetObjectHash{0};
```

A hash identifying the sub-object type. Used by remote clients to match the child to the correct scene/prefab for instantiation.

---

#### SubObjectStatus

```cpp
int32_t SubObjectStatus{0};
```

Internal status tracking for sub-object lifecycle management.

---

### ObjectChild Methods

#### Constructor

```cpp
explicit ObjectChild(SharedMode::Client* client);
```

Constructs a child object. Sets `ObjectType` to `ObjectType::Child`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `client` | `Client*` | The owning client instance. |

---

#### Root

```cpp
ObjectRoot* Root() override;
```

Returns the parent root object. Traverses to the root via the `Parent` ID using the client's object lookup.

**Returns**: Pointer to the parent `ObjectRoot`.

---

### ObjectChild Static Helpers

#### GetParent

```cpp
static ObjectId GetParent(const Object* obj);
```

Extracts the parent object ID from a child object. Returns `ObjectId(0)` if the object is not a child.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object to query. |

**Returns**: The parent `ObjectId`, or `ObjectId(0, 0)` if not a child.

---

#### Is

```cpp
static bool Is(const Object* obj);
```

Checks whether the given object is a child object.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object to check. |

**Returns**: `true` if `obj` is non-null and `obj->ObjectType == ObjectType::Child`.

---

#### Cast (mutable)

```cpp
static ObjectChild* Cast(Object* obj);
```

Safely downcasts an `Object*` to `ObjectChild*`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `Object*` | The object to cast. |

**Returns**: The downcasted pointer, or `nullptr` if `obj` is null or not a child.

---

#### Cast (const)

```cpp
static const ObjectChild* Cast(const Object* obj);
```

Safely downcasts a `const Object*` to `const ObjectChild*`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `const Object*` | The object to cast. |

**Returns**: The downcasted pointer, or `nullptr` if `obj` is null or not a child.

---

## ObjectTail

```cpp
struct ObjectTail {
    int32_t AOI_X;
    int32_t AOI_Y;
    int32_t AOI_Z;
    int32_t AOI_SET;
    int32_t Destroyed;
    int32_t Dummy;
};
```

`ObjectTail` is a fixed-layout structure occupying the last 6 words (24 bytes) of every object's Words buffer. It is managed exclusively by the SDK.

### ObjectTail Layout

| Offset (words) | Field | Type | Description |
|-----------------|-------|------|-------------|
| 0 | `AOI_X` | `int32_t` | AOI grid X coordinate. |
| 1 | `AOI_Y` | `int32_t` | AOI grid Y coordinate. |
| 2 | `AOI_Z` | `int32_t` | AOI grid Z coordinate. |
| 3 | `AOI_SET` | `int32_t` | Non-zero if AOI position has been set. |
| 4 | `Destroyed` | `int32_t` | Non-zero if the object has been destroyed. |
| 5 | `Dummy` | `int32_t` | Padding for alignment. |

**Static assertions** (enforced at compile time):
- `sizeof(ObjectTail) == 24` (6 x 4 bytes)
- Trivially copyable
- 4-byte aligned
- Field offsets match sequential layout

**Critical**: Engine code must **never** write to the tail area of the Words buffer. The SDK writes AOI location data via `Client::SetAreaOfInterestLocation()` and the Destroyed flag internally.

---

## ObjectType Enum

```cpp
enum class ObjectType : uint8_t {
    Base  = 1,  // Abstract base (should not appear at runtime)
    Child = 2,  // Sub-object (ObjectChild)
    Root  = 3   // Top-level object (ObjectRoot)
};
```

Discriminator used by `Object::ObjectType` to enable safe downcasting via the `Is()` and `Cast()` static helpers.
