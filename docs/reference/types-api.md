# Types, Aliases, and Utilities API Reference

Core type definitions, enums, flags, structs, and utility functions used throughout the Fusion SDK. Defined across `Types.h`, `Aliases.h`, `Misc.h`, and `StringType.h` in the `SharedMode` namespace.

For Object class hierarchy (`Object`, `ObjectRoot`, `ObjectChild`), see [Object API Reference](object-api.md).

---

## Type Aliases (Aliases.h)

### `Word`

```cpp
typedef int32_t Word;
```

The fundamental unit of the networked state buffer. All property data in the Words buffer is measured and accessed in words (4 bytes each).

### `Tick`

```cpp
typedef uint32_t Tick;
```

A monotonically increasing simulation tick counter. Used for change detection, delta synchronization, and temporal ordering.

### `PlayerId`

```cpp
typedef uint32_t PlayerId;
```

Unique identifier for a connected player within a room. Assigned by the Photon server.

### Special PlayerId Constants

| Constant | Value | Description |
|---|---|---|
| `MasterClientPlayerId` | `0xFFFFFFFF` | The master client (room host). |
| `PluginPlayerId` | `0xFFFFFFFF - 1` | The server-side plugin. |
| `ObjectOwnerPlayerId` | `0xFFFFFFFF - 2` | Placeholder meaning "the object's current owner". |

---

## ObjectId (Aliases.h)

Globally unique identifier for a networked object within a room. Composed of the creating player's ID and a per-player counter.

```cpp
struct ObjectId {
    PlayerId Origin;
    uint32_t Counter;
};
```

### Fields

| Field | Type | Description |
|---|---|---|
| `Origin` | `PlayerId` | The player who created this object. |
| `Counter` | `uint32_t` | Per-player sequential counter. Together with `Origin`, forms a unique ID. |

### Constructors

```cpp
ObjectId();                                           // Default: {0, 0}
ObjectId(PlayerId origin, uint32_t counter);          // Explicit origin + counter
explicit ObjectId(const uint64_t &packed);            // Unpack from 64-bit value
```

The packed constructor splits a `uint64_t`: low 32 bits become `Origin`, high 32 bits become `Counter`.

### Methods

| Method | Return Type | Description |
|---|---|---|
| `IsNone()` | `bool` | Returns `true` if both `Origin` and `Counter` are `0`. |
| `IsSome()` | `bool` | Returns `true` if either `Origin` or `Counter` is non-zero. |

### Operators

| Operator | Description |
|---|---|
| `operator==(const ObjectId&)` | Equality: both fields match. |
| `operator!=(const ObjectId&)` | Inequality. |
| `operator StringType()` | Conversion to string representation. |
| `operator uint64_t()` | Pack into 64-bit value. |

---

## TypeRef (Aliases.h)

Describes an object's type for the replication system. Used to match objects across clients.

```cpp
struct TypeRef {
    uint64_t Hash;
    uint32_t WordCount;
};
```

| Field | Type | Description |
|---|---|---|
| `Hash` | `uint64_t` | CRC64 hash of the type descriptor. Must match on all clients. |
| `WordCount` | `uint32_t` | Total number of words in the object's state buffer (excluding tail). |

---

## Enums (Types.h)

### ObjectType

Discriminator for the object class hierarchy.

```cpp
enum class ObjectType : uint8_t {
    Base  = 1,
    Child = 2,
    Root  = 3,
};
```

| Value | Description |
|---|---|
| `Base` | Base `Object` (should not appear in practice). |
| `Child` | An `ObjectChild` -- a sub-object attached to a root. |
| `Root` | An `ObjectRoot` -- a top-level networked object. |

### ObjectSettingsFlags

Bitfield controlling object behavior.

```cpp
enum class ObjectSettingsFlags : uint8_t {
    None                   = 0,
    OwnerLeavesOwnerToNone = 1 << 0,
    IsGlobalInstance       = 1 << 1,
};
```

| Value | Bit | Description |
|---|---|---|
| `None` | `0x00` | No special settings. |
| `OwnerLeavesOwnerToNone` | `0x01` | When the owner disconnects, set owner to none instead of transferring. |
| `IsGlobalInstance` | `0x02` | Object is a global singleton, not tied to any specific player. |

Supports bitwise operators: `~`, `&`, `|`, `|=`.

### ObjectInterestModes

Controls which clients receive updates for an object.

```cpp
enum class ObjectInterestModes : uint8_t {
    All      = 0,
    Area     = 1,
    Assigned = 2,
};
```

| Value | Description |
|---|---|
| `All` | All clients in the room receive updates. |
| `Area` | Only clients whose Area of Interest overlaps the object's position receive updates. |
| `Assigned` | Only explicitly assigned clients receive updates. |

### ObjectOwnerModes

Determines how ownership is managed for an object.

```cpp
enum class ObjectOwnerModes : uint8_t {
    Transaction  = 0,
    Dynamic      = 1,
    MasterClient = 2,
};
```

| Value | Description |
|---|---|
| `Transaction` | Ownership is transferred via explicit request-and-confirm transactions. |
| `Dynamic` | Ownership automatically transfers based on `ObjectOwnerIntent` with a cooldown. |
| `MasterClient` | The master client always owns this object. |

### ObjectOwnerIntent

Declares a client's desire regarding ownership of a dynamically-owned object.

```cpp
enum class ObjectOwnerIntent : uint8_t {
    DontWantOwner = 0,
    WantOwner     = 1,
};
```

| Value | Description |
|---|---|
| `DontWantOwner` | This client does not want ownership. |
| `WantOwner` | This client wants to become the owner. |

### ObjectSpecialFlags

Bitfield for special object-level properties, primarily used for physics replication.

```cpp
enum class ObjectSpecialFlags : uint8_t {
    None                          = 0,
    IsRootTransform               = 1 << 1,
    IgnoreRootTransformProperties = 1 << 2,
};
```

| Value | Bit | Description |
|---|---|---|
| `None` | `0x00` | No special flags. |
| `IsRootTransform` | `0x02` | This object represents a root transform (physics body). |
| `IgnoreRootTransformProperties` | `0x04` | Skip root transform property serialization. |

Supports bitwise operators: `&`, `|`, `|=`.

---

## Structs (Types.h)

### ObjectFlags

Packed representation of an object's settings, owner mode, and interest mode in a single 32-bit value.

```cpp
struct ObjectFlags {
    union {
        uint32_t _packed;
        struct {
            ObjectSettingsFlags SettingsFlags;
            ObjectOwnerModes OwnerMode;
            ObjectInterestModes InterestMode;
        };
    };
};
```

#### Fields (via union)

| Field | Type | Description |
|---|---|---|
| `_packed` | `uint32_t` | Raw packed value for network serialization. |
| `SettingsFlags` | `ObjectSettingsFlags` | Object behavior flags. |
| `OwnerMode` | `ObjectOwnerModes` | Ownership transfer model. |
| `InterestMode` | `ObjectInterestModes` | Interest/visibility model. |

#### Constructors

```cpp
explicit ObjectFlags(uint32_t packed);
ObjectFlags(ObjectSettingsFlags settings, ObjectOwnerModes owner, ObjectInterestModes interest);
```

#### Static Methods

| Method | Description |
|---|---|
| `Read(ReadBuffer&)` | Deserialize from a buffer. |
| `Write(WriteBuffer&, const ObjectFlags&)` | Serialize to a buffer. |

### ObjectTail

Reserved area at the end of every object's Words buffer. Contains AOI position and the destroyed flag. The engine must **never** write to this region -- it is managed by the SDK.

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

| Field | Type | Description |
|---|---|---|
| `AOI_X` | `int32_t` | Area of Interest X position (quantized). |
| `AOI_Y` | `int32_t` | Area of Interest Y position (quantized). |
| `AOI_Z` | `int32_t` | Area of Interest Z position (quantized). |
| `AOI_SET` | `int32_t` | Non-zero if AOI position has been explicitly set. |
| `Destroyed` | `int32_t` | Non-zero if the object has been marked for destruction. |
| `Dummy` | `int32_t` | Reserved padding. |

**Size**: 24 bytes (6 words). Verified by static assertions:
- `sizeof(ObjectTail) == 24`
- `alignof(ObjectTail) == 4`
- Trivially copyable

The constant `Object::ExtraTailWords` equals `sizeof(ObjectTail) / 4 = 6`.

### AOILocation

Integer 3D position used for Area of Interest calculations.

```cpp
struct AOILocation {
    int32_t X;
    int32_t Y;
    int32_t Z;
};
```

| Field | Type | Description |
|---|---|---|
| `X` | `int32_t` | X coordinate (quantized world units). |
| `Y` | `int32_t` | Y coordinate (quantized world units). |
| `Z` | `int32_t` | Z coordinate (quantized world units). |

#### Constructors

```cpp
AOILocation();                                      // Default: {0, 0, 0}
AOILocation(int32_t x, int32_t y, int32_t z);      // Explicit
```

#### Methods

```cpp
AOILocation GetNeighbour(int32_t xOffset = 0, int32_t yOffset = 0, int32_t zOffset = 0);
```

Returns a new `AOILocation` offset by the given amounts. Used for iterating neighboring cells in the AOI grid.

### InterestBox

Axis-aligned box for Area of Interest queries.

```cpp
struct InterestBox {
    AOILocation Center;
    AOILocation Extents;
};
```

| Field | Type | Description |
|---|---|---|
| `Center` | `AOILocation` | Center of the interest region. |
| `Extents` | `AOILocation` | Half-extents of the box in each dimension. |

#### Constructors

```cpp
InterestBox();                                          // Default
InterestBox(AOILocation center, AOILocation extents);   // Explicit
```

### RpcFlags

Packed flags for RPC metadata.

```cpp
struct RpcFlags {
    uint32_t _value;

    static RpcFlags Read(ReadBuffer &reader);
    static void Write(WriteBuffer &writer, const RpcFlags &rpc);
};
```

| Field | Type | Description |
|---|---|---|
| `_value` | `uint32_t` | Packed flag bits. |

### Rpc

Represents a single Remote Procedure Call in transit.

```cpp
class Rpc {
public:
    uint64_t Id;
    RpcFlags Flags;
    PlayerId OriginPlayer;
    PlayerId TargetPlayer;
    ObjectId TargetObject;
    uint16_t TargetComponent;
    uint64_t DescriptorTypeHash;
    uint64_t EventHash;
    Data Bytes;

    bool IsInternal() const;

    static Rpc Read(ReadBuffer &reader);
    static void Write(WriteBuffer &writer, const Rpc &rpc);
};
```

| Field | Type | Description |
|---|---|---|
| `Id` | `uint64_t` | RPC identifier. Internal RPCs use IDs in range `[1, 1023]`. |
| `Flags` | `RpcFlags` | Packed delivery and routing flags. |
| `OriginPlayer` | `PlayerId` | The player who sent this RPC. |
| `TargetPlayer` | `PlayerId` | The intended recipient player. |
| `TargetObject` | `ObjectId` | The target networked object. |
| `TargetComponent` | `uint16_t` | Index of the target component on the object. |
| `DescriptorTypeHash` | `uint64_t` | CRC64 hash of the RPC descriptor type. |
| `EventHash` | `uint64_t` | CRC64 hash of the RPC event name. |
| `Bytes` | `Data` | Serialized RPC payload. |

#### Methods

| Method | Return Type | Description |
|---|---|---|
| `IsInternal()` | `bool` | Returns `true` if `Id` is in the reserved internal range `[1, 1023]`. |
| `Read(ReadBuffer&)` | `Rpc` | Deserialize an RPC from a buffer. |
| `Write(WriteBuffer&, const Rpc&)` | `void` | Serialize an RPC to a buffer. |

### Internal RPC Constants

| Constant | Value | Description |
|---|---|---|
| `RPC_InternalMinId` | `1` | Minimum ID for internal RPCs. |
| `RPC_InternalMaxId` | `1023` | Maximum ID for internal RPCs. |
| `RPC_InternalSceneChange` | `1` | Internal RPC ID for scene change notifications. |
| `RPC_InternalObjectPriority` | `2` | Internal RPC ID for object priority updates. |

### Object Status Constants

| Constant | Value | Description |
|---|---|---|
| `OBJECT_STATUS_NEW` | `0` | Object has been created locally but not yet sent. |
| `OBJECT_STATUS_PENDING` | `1` | Object creation has been sent, awaiting server confirmation. |
| `OBJECT_STATUS_CREATED` | `2` | Object is fully created and replicating. |

### Object Send Flag Constants

| Constant | Value | Description |
|---|---|---|
| `OBJECT_SENDFLAG_CREATE` | `1` | This update includes object creation data. |
| `OBJECT_SENDFLAG_STRINGHEAP_ENTRIES_CHANGE` | `2` | StringHeap entry metadata has changed. |
| `OBJECT_SENDFLAG_STRINGHEAP_DATA_CHANGE` | `4` | StringHeap data bytes have changed. |

### SdkVersion

SDK version descriptor with five components packed into a union.

```cpp
struct SdkVersion {
    union {
        struct {
            int32_t Major;
            int32_t Minor;
            int32_t Patch;
            int32_t Build;
            int32_t Protocol;
        };
        unsigned char _packed[20];
    };
};
```

| Field | Type | Description |
|---|---|---|
| `Major` | `int32_t` | Major version number. |
| `Minor` | `int32_t` | Minor version number. |
| `Patch` | `int32_t` | Patch version number. |
| `Build` | `int32_t` | Build number. |
| `Protocol` | `int32_t` | Wire protocol version. Must match between client and server. |

### WordData

A single word-level change record: an offset and a value.

```cpp
struct WordData {
    int32_t offset;
    int32_t value;
};
```

| Field | Type | Description |
|---|---|---|
| `offset` | `int32_t` | Word index in the object's Words buffer. |
| `value` | `int32_t` | The word value at that offset. |

### ObjectPacketEnvelope

Tracks which objects were updated in a single outgoing packet, along with their ticks.

```cpp
class ObjectPacketEnvelope {
public:
    std::vector<std::tuple<ObjectId, Tick>> ObjectUpdates;
};
```

| Field | Type | Description |
|---|---|---|
| `ObjectUpdates` | `std::vector<std::tuple<ObjectId, Tick>>` | List of (object ID, tick) pairs included in this packet. |

---

## Data and Buffer Types (Misc.h)

### Data

A raw byte buffer with pointer and length. Used throughout the SDK for passing variable-length binary data.

```cpp
struct Data {
    uint8_t *Ptr;
    size_t Length;
};
```

| Field | Type | Description |
|---|---|---|
| `Ptr` | `uint8_t*` | Pointer to the byte data. May be `nullptr`. |
| `Length` | `size_t` | Number of bytes. |

#### Constructors

```cpp
Data();                                              // Default: {nullptr, 0}
explicit Data(size_t length);                        // Allocate new buffer of given size
explicit Data(const CharType* ptr, size_t length);   // Copy from CharType source
explicit Data(uint8_t* ptr, size_t length);          // Wrap existing pointer (no copy)
```

The `CharType*` constructor allocates new memory and copies the data. The `uint8_t*` constructor wraps the existing pointer without allocation.

#### Methods

| Method | Return Type | Description |
|---|---|---|
| `Valid()` | `bool` | Returns `true` if `Ptr` is non-null and `Length > 0`. |
| `Clone()` | `Data` | Allocates a new buffer and copies all bytes. |
| `Free()` | `void` | Deletes the underlying buffer and resets to `{nullptr, 0}`. |
| `Resize(size_t)` | `void` | Allocates a new buffer, copies existing data, frees the old buffer. |
| `Slice(size_t offset)` | `Data` | Returns a view starting at `offset`. No allocation; shares memory. |
| `CloneSlice(size_t offset)` | `Data` | Returns an allocated copy of the data from `offset` to the end. |

#### Operators

| Operator | Description |
|---|---|
| `operator bool()` | Returns `true` if `Ptr` is non-null and `Length > 0`. |

### BufferT\<T\>

Typed buffer template for managing arrays of a specific type. Used for Words, Shadow, Ticks, and other typed arrays.

```cpp
template<typename T>
struct BufferT {
    T *Ptr;
    size_t Length;
};
```

| Field | Type | Description |
|---|---|---|
| `Ptr` | `T*` | Pointer to the typed array. May be `nullptr`. |
| `Length` | `size_t` | Number of elements (not bytes). |

#### Methods

| Method | Return Type | Description |
|---|---|---|
| `IsValid()` | `bool` | Returns `true` if `Ptr` is non-null and `Length > 0`. |
| `Init(size_t length)` | `void` | Allocates a zero-initialized array of `length` elements. |
| `Resize(size_t length)` | `void` | Allocates a new array, copies existing elements, frees the old array. |

#### Operators

| Operator | Description |
|---|---|
| `operator T*()` | Implicit conversion to raw pointer. |
| `operator void*()` | Implicit conversion to void pointer. |

#### Destructor

The destructor calls `delete[]` on `Ptr`. `BufferT` owns its memory.

---

## Timer Classes (Misc.h)

### TimerDelta

A consumable timer that measures elapsed time since the last consumption. Useful for measuring frame deltas.

```cpp
class TimerDelta {
public:
    void Start();
    bool Running() const;
    double Peek() const;
    double Consume();
    static TimerDelta StartNew();
};
```

| Method | Return Type | Description |
|---|---|---|
| `Start()` | `void` | Record the current time as the start point. |
| `Running()` | `bool` | Returns `true` if the timer has been started. |
| `Peek()` | `double` | Returns elapsed seconds since start without resetting. |
| `Consume()` | `double` | Returns elapsed seconds since start and resets the start to now. |
| `StartNew()` | `TimerDelta` | Static factory: creates and starts a new timer. |

### Timer

A simple elapsed-time timer.

```cpp
class Timer {
public:
    void Start();
    bool Running() const;
    double ElapsedSeconds() const;
};
```

| Method | Return Type | Description |
|---|---|---|
| `Start()` | `void` | Record the current time as the start point. |
| `Running()` | `bool` | Returns `true` if the timer has been started. |
| `ElapsedSeconds()` | `double` | Returns elapsed seconds since `Start()` was called. |

---

## LinkList\<T\> (Misc.h)

Intrusive doubly-linked list. Elements must have `Prev` and `Next` pointer fields of type `T*`. Used internally for Object chains, Fragment queues, and FragmentGroup lists.

```cpp
template<typename T>
struct LinkList {
    T *Head;
    T *Tail;
    int Count;
};
```

### Fields

| Field | Type | Description |
|---|---|---|
| `Head` | `T*` | First element, or `nullptr` if empty. |
| `Tail` | `T*` | Last element, or `nullptr` if empty. |
| `Count` | `int` | Number of elements in the list. |

### Methods

| Method | Return Type | Description |
|---|---|---|
| `AddFirst(T*)` | `void` | Insert at the head. |
| `AddLast(T*)` | `void` | Insert at the tail. |
| `AddBefore(T* item, T* before)` | `void` | Insert `item` immediately before `before`. |
| `AddAfter(T* item, T* after)` | `void` | Insert `item` immediately after `after`. |
| `RemoveFirst()` | `T*` | Remove and return the head element. |
| `RemoveLast()` | `T*` | Remove and return the tail element. |
| `Remove(T*)` | `bool` | Remove a specific element. Returns `true` if removed. |
| `TryRemoveFirst(T*&)` | `bool` | Remove head if non-empty. Returns `false` if list is empty. |
| `TryRemoveLast(T*&)` | `bool` | Remove tail if non-empty. Returns `false` if list is empty. |
| `TryPeekFirst(T*&)` | `bool` | Read head without removing. Returns `false` if list is empty. |

---

## Utility Functions (Misc.h)

### `stringf`

```cpp
std::string stringf(const char *format, ...);
```

Printf-style string formatting. Returns a `std::string`.

### `ZigZagEncode`

```cpp
int64_t ZigZagEncode(int64_t i);
```

Encodes a signed integer into an unsigned integer using ZigZag encoding. Maps negative values to odd positive values and non-negative values to even positive values, making small-magnitude values compact under varint encoding.

### `ZigZagDecode`

```cpp
int64_t ZigZagDecode(int64_t i);
```

Reverses ZigZag encoding. Converts the unsigned representation back to the original signed value.

### `ClockQuantizeEncode`

```cpp
int64_t ClockQuantizeEncode(double clock);
```

Quantizes a double-precision clock value into an integer for compact serialization. Used by `WriteBuffer::TimeBase` and `WriteBuffer::Time`.

### `ClockQuantizeDecode`

```cpp
double ClockQuantizeDecode(int64_t clock);
```

Reverses clock quantization. Used by `ReadBuffer::TimeBase` and `ReadBuffer::Time`.

### `CRC64`

```cpp
uint64_t CRC64(const void *data, size_t length);
uint64_t CRC64(uint64_t crc, const void *data, size_t length);

template<typename T>
uint64_t CRC64(T data);

template<typename T>
uint64_t CRC64(uint64_t crc, T data);
```

Computes a CRC-64 hash over arbitrary data. The two-parameter overloads allow incremental hashing by chaining a previous CRC value. The template overloads hash a single value of any trivially-copyable type.

Used extensively for computing `TypeRef::Hash`, `Rpc::DescriptorTypeHash`, and `Rpc::EventHash`.

---

## String Types (StringType.h)

### `CharType`

```cpp
using CharType = char8_t;
```

The character type used for all string data in the Fusion SDK. UTF-8 encoded.

### `StringType`

```cpp
using StringType = std::u8string;   // When __cpp_lib_char8_t is defined
using StringType = std::basic_string<char8_t>;  // Fallback
```

Standard string type using `CharType`.

### `StringViewType`

```cpp
using StringViewType = std::u8string_view;  // When __cpp_lib_char8_t is defined
using StringViewType = std::basic_string_view<char8_t>;  // Fallback
```

Non-owning string view using `CharType`.

### `FUSION_STR` Macro

```cpp
#define FUSION_STR(str) u8##str
```

Convenience macro for creating `char8_t` string literals. Example: `FUSION_STR("hello")` produces `u8"hello"`.

### `to_string_type`

```cpp
template<typename T>
StringType to_string_type(T value);           // Integral types (not bool)

StringType to_string_type(bool value);        // Returns u8"True" or u8"False"

StringType& to_string_type(StringType& value);       // Pass-through for StringType

StringType to_string_type(const CharType* value);    // Wrap raw pointer
```

Converts values to `StringType`. The integral overload uses `std::to_chars` for fast conversion without locale dependency.
