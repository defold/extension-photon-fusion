# RPCs (Remote Procedure Calls)

RPCs in Fusion are fire-and-forget messages sent between clients. Unlike [state synchronization](serialization.md), which continuously replicates property values, RPCs deliver discrete events: damage notifications, chat messages, ability activations, and other one-shot actions.

## Rpc Structure

```cpp
class Rpc {
public:
    uint64_t Id;                    // RPC identifier
    RpcFlags Flags;                 // Delivery flags
    PlayerId OriginPlayer;          // Sender
    PlayerId TargetPlayer;          // Recipient (0 = all)
    ObjectId TargetObject;          // Target object (0,0 = broadcast)
    uint16_t TargetComponent;       // Component index on target object

    uint64_t DescriptorTypeHash;    // Type descriptor hash for routing
    uint64_t EventHash;             // Method/event hash for dispatch

    Data     Bytes;                 // Serialized payload

    bool IsInternal() const;        // True if Id is in [1, 1023]
};
```

### Key Fields

| Field | Purpose |
|-------|---------|
| `Id` | Unique RPC identifier. IDs 1-1023 are reserved for SDK internal RPCs. User RPCs use IDs >= 1024. |
| `TargetPlayer` | Specific player to receive the RPC. Use `0` for all players, or a `PlayerId` for targeted delivery. |
| `TargetObject` | The networked object this RPC is addressed to. An `ObjectId(0,0)` indicates a broadcast RPC (not targeted at any object). |
| `DescriptorTypeHash` | Hash identifying the type/class of the RPC handler. Used by the integration layer to route to the correct receiver. |
| `EventHash` | Hash identifying the specific method to call. Combined with `DescriptorTypeHash`, uniquely identifies the RPC handler. |
| `Bytes` | Opaque payload containing serialized arguments. |

## Creating and Sending RPCs

### CreateUserRpc

```cpp
Rpc Client::CreateUserRpc(
    uint64_t    id,                 // RPC ID (must be >= 1024)
    PlayerId    targetPlayer,       // 0 = all, specific PlayerId = targeted
    ObjectId    targetObject,       // Object target or (0,0) for broadcast
    uint64_t    DescriptorTypeHash, // Type hash for routing
    uint64_t    EventHash,          // Method hash for dispatch
    const char* data,               // Serialized payload bytes
    size_t      dataLength          // Payload length
);
```

This constructs an `Rpc` struct with the local player as `OriginPlayer`. The `data` parameter should contain pre-serialized arguments.

### SendUserRpc

```cpp
bool Client::SendUserRpc(const Rpc& rpc);
```

Queues the RPC for delivery. RPCs are batched and sent during the next `PacketQueue()` cycle (end of the frame update). Returns `true` if the RPC was accepted.

### Example

```cpp
// Serialize payload
WriteBuffer payload;
payload.Int(damageAmount);
payload.UIntVar(targetEntityId);
Data payloadData = payload.Take();

// Create and send
Rpc rpc = client->CreateUserRpc(
    1024,                           // User RPC ID
    0,                              // All players
    targetObjectId,                 // Object-targeted
    CRC64("DamageHandler"),         // Type hash
    CRC64("OnDamage"),              // Method hash
    reinterpret_cast<const char*>(payloadData.Ptr),
    payloadData.Length
);
client->SendUserRpc(rpc);
```

## Receiving RPCs

All incoming RPCs (both user and internal) arrive through a single callback:

```cpp
std::function<void(Rpc&)> OnRpc;
```

The integration layer routes based on `TargetObject`:

1. **Object-targeted** (`TargetObject` is not `(0,0)`): Look up the object in the registry, find its synchronizer/component, and dispatch using `DescriptorTypeHash` + `EventHash`.
2. **Broadcast** (`TargetObject` is `(0,0)`): Deliver to all registered broadcast receivers.

### Routing by Hashes

`DescriptorTypeHash` and `EventHash` together identify the handler:

- `DescriptorTypeHash` identifies the class or script type (e.g., `CRC64("PlayerController")`).
- `EventHash` identifies the method within that type (e.g., `CRC64("on_damage_received")`).

The integration layer computes these hashes at registration time and matches them against incoming RPCs.

## Target Filtering

### Player Targeting

| TargetPlayer Value | Behavior |
|-------------------|----------|
| `0` | Delivered to all players |
| Specific `PlayerId` | Delivered only to that player |
| `MasterClientPlayerId` (0xFFFFFFFF) | Delivered to the current master client |
| `ObjectOwnerPlayerId` (0xFFFFFFFF - 2) | Delivered to the owner of `TargetObject` |

### Object Targeting

| TargetObject Value | Behavior |
|-------------------|----------|
| `ObjectId(0, 0)` | Broadcast RPC -- not object-targeted |
| Valid `ObjectId` | Delivered to the specific networked object |

## Internal RPCs

RPC IDs 1 through 1023 are reserved for SDK internal use:

```cpp
constexpr uint64_t RPC_InternalMinId     = 1;
constexpr uint64_t RPC_InternalMaxId     = 1023;
constexpr uint64_t RPC_InternalSceneChange   = 1;   // Scene transition notification
constexpr uint64_t RPC_InternalObjectPriority = 2;  // AOI priority update
```

Internal RPCs are processed by the SDK before reaching `OnRpc`. The `IsInternal()` method identifies them:

```cpp
bool Rpc::IsInternal() const {
    return Id >= RPC_InternalMinId && Id <= RPC_InternalMaxId;
}
```

User code should never send RPCs with IDs in the internal range.

## RpcFlags

```cpp
struct RpcFlags {
    uint32_t _value;

    static RpcFlags Read(ReadBuffer& reader);
    static void Write(WriteBuffer& writer, const RpcFlags& rpc);
};
```

Flags are packed into a single `uint32_t` and serialized/deserialized via the buffer helpers. The flag values control delivery semantics (reliable vs. unreliable, etc.).

## Serialization and Deserialization

RPC payloads are serialized into the `Bytes` field using `ReadBuffer`/`WriteBuffer` or a custom serialization format. The SDK does not interpret payload contents -- it passes `Bytes` through as-is. The integration layer is responsible for:

1. **Serializing** arguments into a byte array before `CreateUserRpc`.
2. **Deserializing** `Bytes` back into arguments in the `OnRpc` handler.

### Rpc Wire Format

The `Rpc` struct itself has built-in serialization:

```cpp
static Rpc  Rpc::Read(ReadBuffer& reader);
static void Rpc::Write(WriteBuffer& writer, const Rpc& rpc);
```

These are used internally by the SDK for packet construction and parsing.

## Delivery Guarantees

RPCs are sent through Fusion's Notify protocol, which provides ordered, reliable delivery with acknowledgment tracking. Lost packets are detected and retransmitted. The `PacketLost` and `PacketDelivered` callbacks on the `Client` track delivery status for sent packets.

## See Also

- [Serialization](serialization.md) -- ReadBuffer/WriteBuffer for payload encoding
- [Architecture](architecture.md) -- Frame loop and packet queue timing
- [Scene Management](scene-management.md) -- Scene change uses internal RPC
- [Client API Reference](../reference/client-api.md) -- Full `CreateUserRpc` / `SendUserRpc` signatures
