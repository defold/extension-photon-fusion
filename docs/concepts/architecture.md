# Architecture

Fusion is a C++ networking SDK built on the Photon real-time transport layer. It provides authoritative state replication, RPCs, and area-of-interest management through a compact, single-threaded API.

## Layered Design

| Layer | Namespace | Responsibility |
|-------|-----------|----------------|
| **Fusion** | `SharedMode` | Object state replication, RPCs, ownership, AOI, scene management |
| **Notify** | `SharedMode::Notify` | Reliable/unreliable channel protocol, fragmentation, ack tracking |
| **Photon** | `SharedMode::Photon` (wraps `ExitGames::LoadBalancing`) | Cloud connection, room management, transport |

The engine integration layer (your code) sits above Fusion. It drives the [frame loop](frame-loop.md), handles [object creation](object-creation.md) callbacks, and maps SDK objects to engine entities.

```
 +---------------------------+
 |   Engine Integration      |   Your code
 +---------------------------+
 |   SharedMode::Client      |   Fusion SDK
 +---------------------------+
 |   SharedMode::Notify      |   Reliable delivery
 +---------------------------+
 |   SharedMode::Photon      |   Photon transport
 +---------------------------+
 |   ExitGames LoadBalancing  |   Photon C++ SDK
 +---------------------------+
```

## Threading Model

Fusion is **single-threaded**. All SDK calls -- `UpdateFrameBegin()`, `UpdateFrameEnd()`, `Service()`, object creation, RPCs -- must happen on the same thread. The SDK does not use mutexes or thread-local storage; concurrent access from multiple threads is undefined behavior.

The transport layer (`Photon::Service()`) dispatches received network packets synchronously. Callbacks like `OnObjectCreated`, `OnRpc`, and `OnSceneChange` fire during `UpdateFrameBegin()`, within the caller's thread context.

## Namespaces

### `SharedMode`

The primary namespace. Contains:

- `Client` -- the main entry point ([Client API](../reference/client-api.md))
- `Object`, `ObjectRoot`, `ObjectChild` -- networked object types ([Objects](objects.md))
- `Rpc`, `RpcFlags` -- RPC message types ([RPCs](rpcs.md))
- Type aliases: `Word`, `Tick`, `PlayerId`, `ObjectId`, `TypeRef`
- Enums: `ObjectOwnerModes`, `ObjectInterestModes`, `ObjectSettingsFlags`, `DestroyModes`
- Memory primitives: `Data`, `BufferT<T>`, `LinkList<T>`

### `SharedMode::Notify`

The reliable delivery protocol layer. Contains:

- `Connection` -- manages send/receive windows, channels, ack masks
- `Channel` -- individual transport channel (reliable or unreliable)
- `Fragment`, `FragmentGroup`, `FragmentHeader` -- packet fragmentation
- `Platform` -- abstract interface for transport callbacks

See [Notify API](../reference/notify-api.md) for details.

## Fundamental Types

### `Word`

```cpp
typedef int32_t Word;  // 4 bytes
```

The atomic unit of state replication. All networked properties are serialized as sequences of Words in the object's [Words buffer](objects.md#words-buffer).

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

    bool IsNone() const;
    bool IsSome() const;

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

Describes an object type. `Hash` identifies which scene/prefab to instantiate on remote clients. `WordCount` determines the buffer size allocated for state replication (must include the 6-word [ObjectTail](objects.md#objecttail)).

## Memory Primitives

### `Data`

```cpp
struct Data {
    uint8_t* Ptr;
    size_t Length;

    bool Valid() const;
    Data Clone() const;
    void Free();
    void Resize(size_t length);
    Data Slice(size_t offset) const;
};
```

A raw byte buffer. Used for RPC payloads, headers, serialized packets, and string heap data. `Clone()` performs a deep copy; `Slice()` returns a view without copying.

### `BufferT<T>`

```cpp
template<typename T>
struct BufferT {
    T* Ptr;
    size_t Length;

    bool IsValid();
    void Init(size_t length);
    void Resize(size_t length);

    operator T*() const;
};
```

A typed, heap-allocated array. Used for the Words buffer (`BufferT<Word>`), shadow buffer, tick tracking (`BufferT<Tick>`), and received-state bitmasks (`BufferT<uint8_t>`). `Init()` allocates and zero-fills; `Resize()` preserves existing content.

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
    // ...
};
```

Intrusive doubly-linked list. Requires `T` to have `Prev` and `Next` pointer members. Used internally for object tracking, packet queues, and fragment management.

## String Conventions

The SDK uses `char8_t` (UTF-8) throughout:

```cpp
using CharType = char8_t;
using StringType = std::u8string;

#define FUSION_STR(str) u8##str
```

All string parameters (`ConnectCloud`, `JoinRoom`, `ChangeScene`, etc.) accept `const CharType*`. Engine integrations must convert their native string types to UTF-8 before calling SDK functions.

## Related

- [Objects](objects.md) -- Object hierarchy and Words buffer
- [Frame Loop](frame-loop.md) -- The mandatory 3-step frame sequence
- [Connection](connection.md) -- Client construction and connection management
- [Client API](../reference/client-api.md) -- Full Client class reference
