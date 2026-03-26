# Architecture

Fusion is a C++ networking SDK built on the Photon real-time transport layer. It provides authoritative state replication, RPCs, and area-of-interest management through a compact, single-threaded API. The SDK is engine-agnostic -- your engine integration sits on top and drives everything through a small set of calls each frame.

## Layered Design

```
 +-------------------------------+
 |   Engine Integration          |   Your code (Godot, Unreal, custom)
 +-------------------------------+
 |   SharedMode::Client          |   Fusion SDK: objects, RPCs, AOI, ownership
 +-------------------------------+
 |   PhotonMatchmaking::         |   Matchmaking: rooms, lobbies, tasks
 |   RealtimeClient              |
 +-------------------------------+
 |   SharedMode::Notify          |   Reliable/unreliable channel protocol
 +-------------------------------+
 |   Photon Realtime SDK         |   UDP/TCP transport (ExitGames)
 +-------------------------------+
```

| Layer | Namespace | Responsibility |
|-------|-----------|----------------|
| **Fusion** | `SharedMode` | Object state replication, RPCs, ownership, AOI, scene management |
| **Matchmaking** | `PhotonMatchmaking` | Cloud connection, room management, async Task/Result API |
| **Notify** | `SharedMode::Notify` | Reliable/unreliable channel protocol, fragmentation, ack tracking |
| **Common** | `PhotonCommon` | Shared utilities: Broadcaster, Subscription, SubscriptionBag, string types |
| **Photon SDK** | `ExitGames::LoadBalancing` | Low-level UDP/TCP transport (opaque to integration code) |

The engine integration layer sits above Fusion. It drives the [frame loop](frame-loop.md), handles [object creation](object-creation.md) callbacks, and maps SDK objects to engine entities.

## Threading Model

Fusion is **single-threaded**. All SDK calls -- `RealtimeClient::Service()`, `Client::UpdateFrameBegin()`, `Client::UpdateFrameEnd()`, object creation, RPCs -- must happen on the same thread. The SDK does not use mutexes or thread-local storage; concurrent access from multiple threads is undefined behavior.

The transport layer (`RealtimeClient::Service()`) dispatches received network packets synchronously. Callbacks fire via `Broadcaster` during `UpdateFrameBegin()`, within the caller's thread context. The `RealtimeClient` may optionally run a background send/receive thread (controlled by `ConnectOptions::useBackgroundSendReceiveThread`), but all Fusion-level processing remains single-threaded.

## Namespaces

### `SharedMode`

The primary namespace. Contains:

- `Client` -- the main Fusion entry point ([connection](connection.md), [frame loop](frame-loop.md))
- `Object`, `ObjectRoot`, `ObjectChild` -- networked object types ([Objects](objects.md))
- `Rpc`, `RpcFlags` -- RPC message types ([RPCs](rpcs.md))
- Type aliases: `Word`, `Tick`, `PlayerId`, `ObjectId`, `TypeRef`
- Enums: `ObjectOwnerModes`, `ObjectOwnerIntent`, `ObjectSpecialFlags`, `InterestKeyType`, `DestroyModes`
- Memory primitives: `Data`, `BufferT<T>`, `LinkList<T>`
- Utilities: `FloatQuantize`, `FloatDequantize`, `QuaternionCompress`, `QuaternionDecompress`, `CRC64`, `ZigZag`, `ClockQuantize`

### `SharedMode::Notify`

The reliable delivery protocol layer. Contains:

- `Connection` -- manages send/receive windows, channels, ack masks
- `Channel` -- individual transport channel (reliable or unreliable)
- `Fragment`, `FragmentGroup`, `FragmentHeader` -- packet fragmentation
- `Platform` -- abstract interface for transport callbacks

### `PhotonMatchmaking`

The matchmaking and connection layer. Contains:

- `RealtimeClient` -- connection management, room operations, async API
- `Task<T>` -- C++20 coroutine-based async result
- `Result<T>` -- success/error result type
- `ConnectOptions`, `ClientConstructOptions` -- connection configuration
- `CreateRoomOptions`, `JoinRoomOptions`, `MatchmakingOptions` -- room configuration
- `MutableRoomView` -- live room state with read/write access
- `ConnectionState`, `DisconnectCause`, `ErrorCode` -- status enums
- `AuthenticationValues` -- custom authentication

### `PhotonCommon`

Shared types used across layers:

- `Broadcaster<Signature>` -- type-safe event dispatcher with `Subscribe()` / `Broadcast()`
- `Subscription` -- individual subscription handle with `Unsubscribe()`, `Block()`, `Unblock()`
- `ScopedSubscription` -- RAII wrapper that unsubscribes on destruction
- `SubscriptionBag` -- collection of scoped subscriptions for bulk cleanup
- `StringType` / `CharType` / `StringViewType` -- UTF-8 string types (`char8_t` based)

## Fundamental Types

### `Word`

```cpp
typedef int32_t Word;  // 4 bytes
```

The atomic unit of state replication. All networked properties are serialized as sequences of Words in the object's [Words buffer](objects.md).

### `Tick`

```cpp
typedef uint32_t Tick;
```

A monotonically increasing frame counter. Used for change detection (dirty tracking), ack tracking, and string heap versioning. Not the same as [network time](time.md).

### `PlayerId`

```cpp
typedef uint32_t PlayerId;

constexpr PlayerId MasterClientPlayerId = 0xFFFFFFFF;      // UINT32_MAX
constexpr PlayerId PluginPlayerId       = 0xFFFFFFFF - 1;   // Server plugin
constexpr PlayerId ObjectOwnerPlayerId  = 0xFFFFFFFF - 2;   // Owner placeholder
```

Identifies a client within a room. Assigned by the Photon server on room join. Special sentinel values identify the master client and server plugin.

### `ObjectId`

```cpp
struct ObjectId {
    PlayerId Origin;   // Creator's PlayerId
    uint32_t Counter;  // Monotonic counter per creator

    bool IsNone() const;  // Origin == 0 && Counter == 0
    bool IsSome() const;  // !IsNone()

    explicit ObjectId(const uint64_t& packed);
    operator uint64_t() const;
};
```

Globally unique identifier for a networked object. Composed of the creating player's ID and a per-player counter. Can be packed to/from `uint64_t`.

### `TypeRef`

```cpp
struct TypeRef {
    uint64_t Hash;       // Type identity (e.g., CRC64 of scene path)
    uint32_t WordCount;  // Total Words buffer size including tail
};
```

Describes an object type. `Hash` identifies which scene/prefab to instantiate on remote clients. `WordCount` determines the buffer size allocated for state replication (must include the `ExtraTailWords` for [ObjectTail](objects.md#objecttail)).

## Memory Primitives

### `Data`

```cpp
struct Data {
    uint8_t* Ptr;
    size_t Length;

    bool Valid() const;
    Data Clone() const;       // Deep copy
    void Free();              // Release memory
    void Resize(size_t length);
    Data Slice(size_t offset) const;      // View (no copy)
    Data CloneSlice(size_t offset) const; // Copy from offset

    operator std::span<const uint8_t>() const;
    operator std::span<uint8_t>() const;
};
```

A raw byte buffer. Used for RPC payloads, headers, serialized packets, and string heap data. `Clone()` performs a deep copy; `Slice()` returns a view without copying. Implicitly converts to `std::span` for interop with the Photon SDK.

### `BufferT<T>`

```cpp
template<typename T>
struct BufferT {
    T* Ptr;
    size_t Length;

    bool IsValid();
    void Init(size_t length);    // Allocate + zero-fill
    void Resize(size_t length);  // Grow, preserving content

    operator T*() const;
};
```

A typed, heap-allocated array. Used for the Words buffer (`BufferT<Word>`), shadow buffer, tick tracking (`BufferT<Tick>`), and received-state bitmasks (`BufferT<uint8_t>`).

### `LinkList<T>`

```cpp
template<typename T>
struct LinkList {
    T* Head;
    T* Tail;
    int Count;

    void AddFirst(T* item);
    void AddLast(T* item);
    bool Remove(T* item);
    T* RemoveFirst();
};
```

Intrusive doubly-linked list. Requires `T` to have `Prev` and `Next` pointer members. Used internally for packet queues, fragment management, and send windows.

## Utility Functions

### Float Quantization

```cpp
template<typename T>
int32_t FloatQuantize(T value, int decimals);

template<typename T>
T FloatDequantize(int32_t value, int decimals);
```

Converts floating-point values to fixed-point integers with configurable decimal precision. Useful for bandwidth-efficient position encoding when full `float` precision is not needed.

### Quaternion Compression

```cpp
template<typename T>
uint32_t QuaternionCompress(T x, T y, T z, T w);

template<typename T>
void QuaternionDecompress(uint32_t buffer, T& outX, T& outY, T& outZ, T& outW);
```

Compresses a quaternion from 16 bytes (4 floats) to 4 bytes (1 uint32) using smallest-three encoding with 10 bits per component plus a 2-bit largest-axis index. Matches the server-side implementation exactly.

### CRC64

```cpp
uint64_t CRC64(const void* data, size_t length);
uint64_t CRC64(uint64_t crc, const void* data, size_t length);  // Chained

template<typename T>
uint64_t CRC64(T data);  // Trivially-copyable types
```

64-bit CRC hash used for type identification (`TypeRef::Hash`), RPC routing (`DescriptorTypeHash`, `EventHash`), and general-purpose hashing.

### ZigZag Encoding

```cpp
int64_t ZigZagEncode(int64_t i);  // Signed -> unsigned mapping
int64_t ZigZagDecode(int64_t i);  // Reverse
```

Maps signed integers to unsigned values for efficient varint encoding. Used internally by `WriteBuffer::LongVar()` and `ReadBuffer::LongVar()`.

## String Conventions

The SDK uses `char8_t` (UTF-8) throughout:

```cpp
namespace PhotonCommon {
    using CharType = char8_t;
    using StringType = std::u8string;
    using StringViewType = std::u8string_view;
}

#define PHOTON_STR(str) u8##str
```

All string parameters accept `const CharType*` or `StringViewType`. Engine integrations must convert their native string types to UTF-8 before calling SDK functions.

## Platform Abstraction

The `Notify::Platform` abstract class provides the transport callback interface:

```cpp
class Platform {
public:
    virtual double Clock() = 0;
    virtual void Send(Connection* connection, Data data) = 0;
    virtual void Recv(Connection* connection, Channel& channel, Data data) = 0;
    virtual void Lost(Connection* connection, Channel& channel, void* user, Data data) = 0;
    virtual void Delivered(Connection* connection, Channel& channel, void* user, Data data) = 0;
};
```

The SDK provides `PhotonNotifyPlatform`, which bridges the Notify protocol to the Photon transport layer. Engine integrations do not need to implement this interface directly.

## Related

- [Frame Loop](frame-loop.md) -- The mandatory 3-step frame sequence
- [Connection](connection.md) -- Client construction and RealtimeClient
- [Objects](objects.md) -- Object hierarchy and Words buffer
- [Serialization](serialization.md) -- Words encoding and ReadBuffer/WriteBuffer
