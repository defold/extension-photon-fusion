# Notify Protocol API Reference

Low-level network transport layer for the Fusion SDK. Handles packet fragmentation, reassembly, sequencing, acknowledgments, and reliable/unreliable delivery. All types live in the `SharedMode::Notify` namespace and are defined in `Notify.h`.

---

## Overview

The Notify layer sits between the Photon transport (UDP) and the Fusion replication system. It provides two channels per connection:

- **Game** (channel 1, unreliable) -- State updates that can tolerate packet loss.
- **Streaming** (channel 2, reliable) -- Data that must arrive in order, such as string heap updates.

Large messages are fragmented into MTU-sized pieces, each tagged with a fragment group and index. The receiver reassembles fragments and delivers complete groups to the upper layer. Acknowledgments use a sliding-window bitmask to efficiently report received/lost sequences.

---

## Constants

| Constant | Value | Description |
|---|---|---|
| `RECV_RESULT_NONE` | `0` | Receive returned no actionable result. |
| `RECV_RESULT_DISCONNECT` | `2` | Receive detected a disconnect condition. |
| `DEFAULT_HEADERS` | `144` | Overhead bytes reserved for protocol headers (`40 + 8 + 96`). |
| `MAX_MTU_BYTES_TOTAL` | `1280` | Maximum total packet size in bytes. |
| `MAX_MTU_BYTES_PAYLOAD` | `1136` | Maximum payload bytes per packet (`1280 - 144`). |
| `PACKET_MTU_BYTES` | `1128` | Usable payload after 8-byte alignment (`1136 / 8 * 8`). |

### Fragment Flags

| Constant | Value | Description |
|---|---|---|
| `FRAG_FLAG_DATA` | `0x02` | Fragment contains data payload. |
| `FRAG_FLAG_ACKS` | `0x04` | Fragment contains acknowledgment information. |
| `FRAG_FLAG_LAST_FRAG` | `0x80` | This is the last fragment in a group. |

---

## FragmentHeader

Wire format header for each fragment sent over the network. Fixed size of 24 bytes.

```cpp
struct FragmentHeader {
    uint8_t  Flags;
    uint8_t  _reserved_0;
    uint8_t  Channel;
    uint8_t  _reserved_1;
    uint16_t Sequence;
    uint16_t AckSequence;
    uint64_t AckMask;
    uint32_t FragGroup;
    uint32_t FragIndex;
};
```

### Fields

| Field | Type | Offset | Description |
|---|---|---|---|
| `Flags` | `uint8_t` | 0 | Combination of `FRAG_FLAG_*` constants. |
| `_reserved_0` | `uint8_t` | 1 | Reserved. Must be zero. |
| `Channel` | `uint8_t` | 2 | Channel ID (1 = Game, 2 = Streaming). |
| `_reserved_1` | `uint8_t` | 3 | Reserved. Must be zero. |
| `Sequence` | `uint16_t` | 4 | Sender's sequence number for this fragment. |
| `AckSequence` | `uint16_t` | 6 | Highest sequence number the sender has received. |
| `AckMask` | `uint64_t` | 8 | Bitmask of received sequences relative to `AckSequence`. Bit N = `AckSequence - N - 1`. |
| `FragGroup` | `uint32_t` | 16 | Group identifier. All fragments in the same message share a group. |
| `FragIndex` | `uint32_t` | 20 | Zero-based index of this fragment within its group. |

**Size**: 24 bytes. Verified by static assertions on all field offsets.

---

## Fragment

A single network fragment, either queued for sending or received and awaiting reassembly. Intrusive linked-list node (has `Prev`/`Next` pointers for use with `LinkList`).

```cpp
struct Fragment {
    Fragment *Prev;
    Fragment *Next;
    FragmentGroup *Group;

    double SendTime;

    FragmentHeader Header;
    Data Data;
};
```

### Fields

| Field | Type | Description |
|---|---|---|
| `Prev` | `Fragment*` | Previous node in linked list. |
| `Next` | `Fragment*` | Next node in linked list. |
| `Group` | `FragmentGroup*` | The group this fragment belongs to. |
| `SendTime` | `double` | Timestamp when this fragment was sent. Used for RTT and timeout calculations. |
| `Header` | `FragmentHeader` | The wire-format header for this fragment. |
| `Data` | `Data` | The fragment's payload bytes. |

Non-copyable (copy constructor and assignment operator deleted).

---

## FragmentGroup

Represents a complete message that has been split into one or more fragments. Tracks delivery status of all constituent fragments. Intrusive linked-list node.

```cpp
struct FragmentGroup {
    FragmentGroup *Prev;
    FragmentGroup *Next;

    void *User;
    Data Data;

    std::optional<bool> WasLost;
    std::optional<bool> WasDelivered;

    uint32_t Group;
    uint32_t Count;

    uint8_t *Delivered;
    uint32_t DeliveredCount;
};
```

### Fields

| Field | Type | Description |
|---|---|---|
| `Prev` | `FragmentGroup*` | Previous node in linked list. |
| `Next` | `FragmentGroup*` | Next node in linked list. |
| `User` | `void*` | Opaque user data pointer. Passed back through `Platform::Lost` and `Platform::Delivered` callbacks. |
| `Data` | `Data` | The complete reassembled message data. |
| `WasLost` | `std::optional<bool>` | Set to `true` when the group is determined to be lost. |
| `WasDelivered` | `std::optional<bool>` | Set to `true` when all fragments have been acknowledged. |
| `Group` | `uint32_t` | Group identifier matching `FragmentHeader::FragGroup`. |
| `Count` | `uint32_t` | Total number of fragments in this group. |
| `Delivered` | `uint8_t*` | Per-fragment delivery status array (one byte per fragment). |
| `DeliveredCount` | `uint32_t` | Number of fragments confirmed delivered so far. |

### Methods

#### `IsDone`

```cpp
bool IsDone() const;
```

Returns `true` if the group's outcome has been determined -- either `WasLost` or `WasDelivered` has a value.

#### `SetDelivered`

```cpp
void SetDelivered(Fragment *fragment);
```

Marks the given fragment as delivered. Increments `DeliveredCount`. If all fragments in the group are now delivered, sets `WasDelivered = true`.

Non-copyable (copy constructor and assignment operator deleted).

---

## Channel

Represents a single logical communication channel within a connection. Each channel has independent send/receive state and can be configured as reliable or unreliable.

```cpp
struct Channel {
    uint8_t Id;
    bool Reliable;

    size_t MtuData;

    LinkList<FragmentGroup> Notify;

    uint32_t SendGroup;
    LinkList<Fragment> SendQueue;
    LinkList<Fragment> ResendQueue;

    uint32_t RecvGroup;
    LinkList<Fragment> RecvList;
};
```

### Fields

| Field | Type | Description |
|---|---|---|
| `Id` | `uint8_t` | Channel identifier (1 = Game, 2 = Streaming). |
| `Reliable` | `bool` | If `true`, lost fragments are retransmitted. |
| `MtuData` | `size_t` | Maximum payload bytes per fragment (`PACKET_MTU_BYTES - sizeof(FragmentHeader)`). |
| `Notify` | `LinkList<FragmentGroup>` | Groups pending delivery/loss notification. |
| `SendGroup` | `uint32_t` | Next group ID to assign for outgoing messages. |
| `SendQueue` | `LinkList<Fragment>` | Fragments waiting to be sent. |
| `ResendQueue` | `LinkList<Fragment>` | Fragments that need retransmission (reliable channels only). |
| `RecvGroup` | `uint32_t` | Next expected incoming group ID. |
| `RecvList` | `LinkList<Fragment>` | Received fragments awaiting reassembly. |

### Constructor

```cpp
Channel(uint8_t id, bool reliable);
```

Non-copyable.

---

## Connection

Manages the Notify protocol state for a single peer-to-peer link. Each connection has two built-in channels and handles sequence numbering, acknowledgments, and fragment delivery.

```cpp
class Connection {
public:
    Channel Game{1, false};
    Channel Streaming{2, true};
};
```

### Built-in Channels

| Field | Channel ID | Reliable | Description |
|---|---|---|---|
| `Game` | 1 | No | Unreliable delivery for state replication. Lost data is superseded by newer updates. |
| `Streaming` | 2 | Yes | Reliable ordered delivery for string heap data and other streams that require completeness. |

### Constructor

```cpp
explicit Connection(Platform &platform);
```

Creates a connection bound to a platform implementation for sending/receiving raw packets.

Non-copyable.

### Public Methods

#### `CanQueue`

```cpp
bool CanQueue(Channel &chan);
```

Returns `true` if the channel's send queue can accept more data. Prevents unbounded queue growth.

#### `Queue`

```cpp
bool Queue(Channel &chan, Data data, void *user);
```

Enqueues a message for sending on the specified channel. The message is fragmented into MTU-sized pieces and added to the channel's send queue. The `user` pointer is stored in the `FragmentGroup` and passed back through delivery/loss callbacks.

Returns `true` if the message was successfully queued.

#### `Send`

```cpp
void Send();
```

Transmits queued fragments. Iterates over both channels, sending fragments from `SendQueue` and `ResendQueue` via the `Platform::Send` callback. Each fragment is sent as a single packet with its `FragmentHeader` prepended.

#### `Update`

```cpp
void Update();
```

Performs periodic maintenance: processes delivery notifications, detects timed-out fragments, and triggers retransmission for reliable channels. Should be called once per frame.

#### `Receive`

```cpp
int Receive(Data data);
int Receive(Channel &chan, FragmentHeader header, Data data);
```

Processes an incoming packet.

The single-parameter overload parses the `FragmentHeader` from the data, validates the channel ID, processes acknowledgments, and delegates to the channel-specific overload.

The channel-specific overload handles fragment reassembly: adds the fragment to `RecvList`, and when all fragments of a group have arrived, delivers the reassembled message via `Platform::Recv`.

**Return values**:
- `RECV_RESULT_NONE` (0) -- Normal processing.
- `RECV_RESULT_DISCONNECT` (2) -- Connection should be terminated.

---

## Platform

Abstract interface that the Notify layer uses to interact with the underlying network transport. Engine integrations must provide a concrete implementation.

```cpp
class Platform {
public:
    virtual ~Platform() = default;

    virtual double Clock() = 0;
    virtual void Send(Connection *connection, Data data) = 0;
    virtual void Recv(Connection *connection, Channel &channel, Data data) = 0;
    virtual void Lost(Connection *connection, Channel &channel, void *user, Data data) = 0;
    virtual void Delivered(Connection *connection, Channel &channel, void *user, Data data) = 0;
};
```

### Methods

#### `Clock`

```cpp
virtual double Clock() = 0;
```

Returns the current time in seconds. Used for RTT measurement and timeout detection.

#### `Send`

```cpp
virtual void Send(Connection *connection, Data data) = 0;
```

Sends raw packet bytes over the network. Called by `Connection::Send` for each fragment. The implementation should transmit `data` to the remote peer associated with `connection`.

#### `Recv`

```cpp
virtual void Recv(Connection *connection, Channel &channel, Data data) = 0;
```

Called when a complete message has been reassembled from fragments. The implementation should process the decoded message data.

**Parameters**:
- `connection` -- The connection that received the message.
- `channel` -- The channel the message arrived on.
- `data` -- The complete reassembled message bytes.

#### `Lost`

```cpp
virtual void Lost(Connection *connection, Channel &channel, void *user, Data data) = 0;
```

Called when a message is determined to be lost (unreliable channel) or all retransmission attempts have been exhausted.

**Parameters**:
- `connection` -- The connection that lost the message.
- `channel` -- The channel the message was sent on.
- `user` -- The opaque pointer passed to `Connection::Queue`.
- `data` -- The original message data.

#### `Delivered`

```cpp
virtual void Delivered(Connection *connection, Channel &channel, void *user, Data data) = 0;
```

Called when all fragments of a message have been acknowledged by the remote peer.

**Parameters**:
- `connection` -- The connection that delivered the message.
- `channel` -- The channel the message was sent on.
- `user` -- The opaque pointer passed to `Connection::Queue`.
- `data` -- The original message data.

---

## Acknowledgment Model

The Notify protocol uses a sliding-window bitmask for acknowledgments:

1. Each outgoing fragment gets a monotonically increasing `Sequence` number.
2. Each fragment carries the sender's latest `AckSequence` and a 64-bit `AckMask`.
3. `AckSequence` is the highest sequence number the sender has received.
4. `AckMask` bit N represents whether sequence `AckSequence - N - 1` was received.
5. This provides acknowledgment coverage for up to 65 sequences in a single packet.

For **unreliable** channels, unacknowledged fragments are marked as lost and the `Platform::Lost` callback fires.

For **reliable** channels, unacknowledged fragments are moved to the `ResendQueue` and retransmitted until acknowledged.
