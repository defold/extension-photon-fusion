# RPCs (Remote Procedure Calls)

RPCs in Fusion are fire-and-forget messages sent between clients. Unlike [state synchronization](serialization.md), which continuously replicates property values, RPCs deliver discrete events: damage notifications, chat messages, ability activations, and other one-shot actions.

## Rpc Structure

```cpp
class Rpc {
public:
    uint64_t Id;                    // RPC identifier
    RpcFlags Flags;                 // Delivery flags (opaque)
    PlayerId OriginPlayer;          // Sender
    PlayerId TargetPlayer;          // Recipient (0 = all)
    ObjectId TargetObject;          // Target object ((0,0) = broadcast)
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
| `TargetPlayer` | Specific player to receive the RPC. `0` for all players, or a `PlayerId` for targeted delivery. |
| `TargetObject` | The networked object this RPC is addressed to. `ObjectId(0,0)` indicates a broadcast RPC. |
| `DescriptorTypeHash` | Hash identifying the type/class of the RPC handler. Used for routing to the correct receiver. |
| `EventHash` | Hash identifying the specific method to call. Combined with `DescriptorTypeHash`, uniquely identifies the handler. |
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

Queues the RPC for delivery. RPCs are batched and sent during the next `UpdateFrameEnd()` cycle. Returns `true` if the RPC was accepted.

### Complete Example

```cpp
// 1. Serialize payload
WriteBuffer payload;
payload.Int(damageAmount);
payload.UIntVar(targetEntityId);
Data payloadData = payload.Take();

// 2. Create the RPC
Rpc rpc = client->CreateUserRpc(
    1024,                                         // User RPC ID
    0,                                            // All players
    targetObjectId,                               // Object-targeted
    CRC64("DamageHandler", strlen("DamageHandler")),  // Type hash
    CRC64("OnDamage", strlen("OnDamage")),            // Method hash
    reinterpret_cast<const char*>(payloadData.Ptr),
    payloadData.Length
);

// 3. Send
client->SendUserRpc(rpc);

// 4. Clean up payload
payloadData.Free();
```

## Receiving RPCs

All incoming RPCs arrive through a single broadcaster:

```cpp
Broadcaster<void(Rpc&)> OnRpc;
```

Subscribe to receive RPCs:

```cpp
subs += client->OnRpc.Subscribe([](SharedMode::Rpc& rpc) {
    if (rpc.IsInternal()) return;  // Skip SDK-internal RPCs

    if (rpc.TargetObject.IsSome()) {
        // Object-targeted RPC: route to the specific object
        dispatch_to_object(rpc);
    } else {
        // Broadcast RPC: deliver to all registered receivers
        dispatch_broadcast(rpc);
    }
});
```

### Routing by Hashes

`DescriptorTypeHash` and `EventHash` together identify the handler:

- `DescriptorTypeHash` identifies the class or script type (e.g., `CRC64("PlayerController")`).
- `EventHash` identifies the method within that type (e.g., `CRC64("on_damage_received")`).

The integration layer computes these hashes at registration time and matches them against incoming RPCs.

### Reading the Payload

```cpp
void handle_damage_rpc(SharedMode::Rpc& rpc) {
    ReadBuffer reader(rpc.Bytes);
    int32_t damage = reader.Int();
    uint32_t entityId = reader.UIntVar();
    // Apply damage...
}
```

## ID Ranges

| Range | Owner | Purpose |
|-------|-------|---------|
| 1 - 1023 | SDK internal | Scene change, object priority, etc. |
| 1024+ | User | Application-defined RPCs |

### Internal RPC Constants

```cpp
constexpr uint64_t RPC_InternalMinId         = 1;
constexpr uint64_t RPC_InternalMaxId         = 1023;
constexpr uint64_t RPC_InternalSceneChange   = 1;    // Scene transition
constexpr uint64_t RPC_InternalObjectPriority = 2;   // AOI priority
```

Internal RPCs are processed by the SDK before reaching `OnRpc`. The `IsInternal()` method identifies them. User code should never send RPCs with IDs in the internal range.

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
| `ObjectId(0, 0)` (.IsNone()) | Broadcast RPC -- not object-targeted |
| Valid `ObjectId` (.IsSome()) | Delivered to the specific networked object |

## RpcFlags

```cpp
struct RpcFlags {
    uint32_t _value;

    static RpcFlags Read(ReadBuffer& reader);
    static void Write(WriteBuffer& writer, const RpcFlags& rpc);
};
```

Flags are packed into a single `uint32_t` and serialized/deserialized via the buffer helpers. The flag values are opaque to user code -- they control internal delivery semantics.

## Rpc Wire Format

The `Rpc` struct has built-in serialization for the SDK's packet protocol:

```cpp
static Rpc  Rpc::Read(ReadBuffer& reader);
static void Rpc::Write(WriteBuffer& writer, const Rpc& rpc);
```

These are used internally by the SDK for packet construction and parsing. User code does not need to call them directly.

## Delivery Guarantees

RPCs are sent through Fusion's Notify protocol, which provides ordered, reliable delivery with acknowledgment tracking. Lost packets are detected and retransmitted. This means:

- RPCs are guaranteed to arrive (eventually).
- RPCs from the same sender arrive in order.
- Duplicate delivery does not occur.

## Common Mistakes

| Mistake | Symptom |
|---------|---------|
| Using RPC ID < 1024 | Conflicts with SDK internal RPCs |
| Not checking `IsInternal()` | Processing SDK-internal RPCs as user RPCs |
| Sending RPCs before `IsRunning()` | RPCs silently dropped |
| Not freeing payload `Data` after `Take()` | Memory leak |
| Mismatched read/write order in payload | Corrupted arguments |

## Related

- [Serialization](serialization.md) -- ReadBuffer/WriteBuffer for payload encoding
- [Architecture](architecture.md) -- CRC64 for hash generation
- [Scene Management](scene-management.md) -- Scene change uses internal RPC
- [Frame Loop](frame-loop.md) -- When RPCs are sent and received
