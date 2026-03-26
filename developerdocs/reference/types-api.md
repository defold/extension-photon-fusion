# Types API

Core types, aliases, enums, and utility functions shared across the Fusion SDK. All types live in the `SharedMode` namespace unless noted otherwise.

Headers: `Aliases.h`, `Types.h`, `Misc.h`

---

## ObjectId

Unique identifier for a networked object, composed of a player origin and a sequential counter.

```cpp
struct ObjectId {
    static constexpr size_t WordSize = 2;

    PlayerId Origin{0};   // Player who created this object
    uint32_t Counter{0};  // Sequential counter unique per player

    ObjectId() = default;
    ObjectId(PlayerId origin, uint32_t counter);
    explicit ObjectId(const uint64_t &packed);   // Unpack from a 64-bit value

    bool IsNone() const;   // True if both Origin and Counter are 0
    bool IsSome() const;   // True if either Origin or Counter is non-zero

    bool operator==(const ObjectId &other) const;
    bool operator!=(const ObjectId &other) const;

    operator PhotonCommon::StringType() const;   // Convert to string "(Origin:Counter)"
    operator uint64_t() const;                   // Pack into a 64-bit value
};
```

A `std::hash<SharedMode::ObjectId>` specialization is provided in `Client.h` for use in unordered containers.

---

## Type Aliases

```cpp
typedef uint32_t Tick;      // Frame/tick counter
typedef uint32_t PlayerId;  // Player identifier
typedef int32_t  Word;      // Single replicated state word (4 bytes)
```

## Special PlayerId Constants

```cpp
constexpr PlayerId MasterClientPlayerId = 0xFFFFFFFF;       // Targets the current master client
constexpr PlayerId PluginPlayerId       = 0xFFFFFFFF - 1;   // Server plugin actor
constexpr PlayerId ObjectOwnerPlayerId  = 0xFFFFFFFF - 2;   // Targets the object's current owner
```

---

## TypeRef

Descriptor identifying a networked type by its hash and word count.

```cpp
struct TypeRef {
    uint64_t Hash;        // CRC64 hash of the type name/path
    uint32_t WordCount;   // Number of replicated words (excluding tail)
};
```

---

## SdkVersion

Version information returned by `Client::GetSdkVersion()`.

```cpp
struct SdkVersion {
    int32_t Major;     // Major version number
    int32_t Minor;     // Minor version number
    int32_t Patch;     // Patch version number
    int32_t Build;     // Build number
    int32_t Protocol;  // Wire protocol version
};
```

---

## Enums

### ObjectOwnerModes

Ownership model for a networked object.

```cpp
enum class ObjectOwnerModes : uint8_t {
    Transaction = 0,     // Ownership transferred via explicit request
    PlayerAttached = 1,  // Owned by a specific player for its lifetime
    Dynamic = 2,         // Ownership can be claimed by any player
    MasterClient = 3,    // Always owned by the master client
    GameGlobal = 4       // No owner; globally shared state
};
```

### ObjectOwnerIntent

Client-side ownership intent for dynamic objects.

```cpp
enum class ObjectOwnerIntent : uint8_t {
    DontWantOwner = 0,   // Not requesting ownership
    WantOwner = 1        // Requesting ownership
};
```

### ObjectType

Discriminator for object hierarchy position.

```cpp
enum class ObjectType : uint8_t {
    Base = 1,    // Abstract base (should not appear at runtime)
    Child = 2,   // Sub-object attached to a root
    Root = 3     // Top-level networked entity
};
```

### ObjectSpecialFlags

Bitflags for special object behaviors.

```cpp
enum class ObjectSpecialFlags : uint8_t {
    None = 0,
    IsRootTransform = 1 << 1,                  // Object carries the root transform
    IgnoreRootTransformProperties = 1 << 2      // Skip root transform during replication
};
```

Supports `|`, `&`, and `|=` operators.

### InterestKeyType

Classification of interest keys assigned to objects.

```cpp
enum class InterestKeyType : uint8_t {
    Global = 0,   // Visible to all players
    Area = 1,     // Server-managed spatial interest
    User = 2      // User-defined interest group
};
```

### DestroyModes

Reason why a networked object was destroyed.

```cpp
enum class DestroyModes {
    Local = 0,             // Destroyed by the local client
    Remote = 1,            // Destroyed by a remote client
    SceneChange = 2,       // Destroyed due to a scene transition
    Shutdown = 3,          // Destroyed during client shutdown
    RejectedNotOwner = 4,  // Server rejected creation (not owner)
    ForceDestroy = 5       // Force-destroyed by the server
};
```

### LogLevel

Bitmask for SDK log filtering.

```cpp
enum LogLevel : uint8_t {
    Trace   = 1 << 0,   // Verbose trace messages
    Debug   = 1 << 1,   // Debug messages
    Info    = 1 << 2,   // Informational messages
    Warning = 1 << 3,   // Warnings
    Error   = 1 << 4    // Errors
};
```

---

## Status Constants

Object lifecycle status values (stored in `Object::Status`).

```cpp
constexpr int32_t OBJECT_STATUS_NEW     = 0;   // Newly created, not yet sent
constexpr int32_t OBJECT_STATUS_PENDING = 1;   // Sent to server, awaiting confirmation
constexpr int32_t OBJECT_STATUS_CREATED = 2;   // Confirmed by server
```

---

## Send Flag Constants

Per-packet flags written into `Object::SendFlags`.

```cpp
constexpr uint8_t OBJECT_SENDFLAG_CREATE                   = 1;    // Object creation packet
constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_ENTRIES_CHANGE = 2;   // String heap entry metadata changed
constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_DATA_CHANGE   = 4;    // String heap data changed
constexpr uint8_t OBJECT_SENDFLAG_IN_INTEREST_SET          = 8;    // Object is in the sender's interest set
constexpr uint8_t OBJECT_SENDFLAG_IS_SUBOBJECT             = 16;   // Packet is for a sub-object
constexpr uint8_t OBJECT_SENDFLAG_TIMEONLY                 = 32;   // Time-only update (no state data)
```

---

## RPC ID Constants

Reserved internal RPC identifier ranges and well-known IDs.

```cpp
constexpr uint64_t RPC_InternalMinId         = 1;      // Start of internal RPC range
constexpr uint64_t RPC_InternalMaxId         = 1023;   // End of internal RPC range
constexpr uint64_t RPC_InternalSceneChange   = 1;      // Scene change RPC
constexpr uint64_t RPC_InternalObjectPriority = 2;     // Object priority update RPC
```

---

## Rpc Class

Represents a remote procedure call message.

```cpp
class Rpc {
public:
    uint64_t Id{};                      // RPC identifier (user IDs start at 1024)
    RpcFlags Flags{};                   // Delivery flags
    PlayerId OriginPlayer{};            // Sender player ID
    PlayerId TargetPlayer{};            // Target player ID (0 = broadcast)
    ObjectId TargetObject{0, 0};        // Target object (optional)
    uint16_t TargetComponent{};         // Target component index on the object

    uint64_t DescriptorTypeHash{0};     // CRC64 of the target type descriptor
    uint64_t EventHash{0};             // CRC64 of the event/method name

    Data Bytes;                         // Serialized payload

    bool IsInternal() const;            // True if Id is in the internal range [1, 1023]

    static Rpc Read(ReadBuffer &reader);            // Deserialize an Rpc from a buffer
    static void Write(WriteBuffer &writer, const Rpc &rpc);  // Serialize an Rpc to a buffer
};
```

## RpcFlags

Delivery flags for an RPC.

```cpp
struct RpcFlags {
    uint32_t _value;

    static RpcFlags Read(ReadBuffer &reader);                     // Deserialize flags
    static void Write(WriteBuffer &writer, const RpcFlags &rpc);  // Serialize flags
};
```

---

## Utility Functions

### Float Quantization

```cpp
template<typename T>
int32_t FloatQuantize(T value, int decimals);
```
Quantize a float/double to a fixed-point integer with `decimals` decimal places.

```cpp
template<typename T>
T FloatDequantize(int32_t value, int decimals);
```
Dequantize a fixed-point integer back to float/double.

### Quaternion Compression

```cpp
template<typename T>
uint32_t QuaternionCompress(T x, T y, T z, T w);
```
Compress a unit quaternion to 32 bits using smallest-three encoding (10 bits per component + 2-bit axis index).

```cpp
template<typename T>
void QuaternionDecompress(uint32_t buffer, T &outX, T &outY, T &outZ, T &outW);
```
Decompress a 32-bit encoded quaternion back to four components.

### CRC64

```cpp
uint64_t CRC64(const void *data, size_t length);
uint64_t CRC64(uint64_t crc, const void *data, size_t length);

template<typename T> uint64_t CRC64(T data);
template<typename T> uint64_t CRC64(uint64_t crc, T data);
```
Compute a CRC-64 hash. The seeded overloads continue hashing from a previous CRC value. Template overloads hash any trivially-copyable value directly.

### ZigZag Encoding

```cpp
int64_t ZigZagEncode(int64_t i);
int64_t ZigZagDecode(int64_t i);
```
Encode/decode signed integers using ZigZag encoding for efficient variable-length representation (maps negative values to positive).

### Clock Quantization

```cpp
int64_t ClockQuantizeEncode(double clock);
double ClockQuantizeDecode(int64_t clock);
```
Encode/decode clock timestamps for compact wire representation.

### String Formatting

```cpp
std::string stringf(const char *format, ...);
```
Printf-style string formatting returning a `std::string`.

---

## Timer

Monotonic elapsed-time timer.

```cpp
class Timer {
public:
    void Start();                       // Start or restart the timer
    bool Running() const;               // True if the timer has been started
    double ElapsedSeconds() const;      // Seconds elapsed since Start()
};
```

## TimerDelta

Monotonic timer that tracks consumable deltas.

```cpp
class TimerDelta {
public:
    void Start();                       // Start or restart the timer
    bool Running() const;               // True if the timer has been started
    double Peek() const;                // Seconds since last Start/Consume without resetting
    double Consume();                   // Seconds since last Start/Consume, then reset
    static TimerDelta StartNew();       // Create and start a new timer
};
```

---

## LinkList\<T\>

Intrusive doubly-linked list. Elements must have `Prev` and `Next` pointer fields.

```cpp
template<typename T>
struct LinkList {
    T *Head{nullptr};    // First element
    T *Tail{nullptr};    // Last element
    int Count{0};        // Number of elements
};
```

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddFirst` | `void AddFirst(T *item)` | Insert at the head. |
| `AddLast` | `void AddLast(T *item)` | Insert at the tail. |
| `AddBefore` | `void AddBefore(T *item, T *before)` | Insert before an existing element. |
| `AddAfter` | `void AddAfter(T *item, T *after)` | Insert after an existing element. |
| `Remove` | `bool Remove(T *item)` | Remove an element. Returns `true` if found. |
| `RemoveFirst` | `T *RemoveFirst()` | Remove and return the head element. |
| `RemoveLast` | `T *RemoveLast()` | Remove and return the tail element. |
| `TryRemoveFirst` | `bool TryRemoveFirst(T *&result)` | Try to remove the head. Returns `false` if empty. |
| `TryRemoveLast` | `bool TryRemoveLast(T *&result)` | Try to remove the tail. Returns `false` if empty. |
| `TryPeekFirst` | `bool TryPeekFirst(T *&result)` | Peek at the head without removing. Returns `false` if empty. |

---

## WordData

Utility struct for sparse state updates.

```cpp
struct WordData {
    int32_t offset;   // Word index in the buffer
    int32_t value;    // Word value
};
```
