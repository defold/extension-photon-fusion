# Notify API

Custom reliable/unreliable transport layer with fragmentation, acknowledgment tracking, and delivery callbacks. All types live in the `SharedMode::Notify` namespace.

Header: `Notify.h`

---

## Connection

Manages two channels (Game and Streaming), handles fragmentation, acknowledgments, and delivery notifications.

### Constructor

```cpp
explicit Connection(Platform &platform);
```

Create a connection bound to a platform implementation. Non-copyable.

### Public Fields

```cpp
Channel Game{1, false};       // Channel 1: unreliable, used for state updates
Channel Streaming{2, true};   // Channel 2: reliable, used for RPCs and guaranteed delivery
```

### Public Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `CanQueue` | `bool CanQueue(Channel &chan)` | Returns `true` if the channel can accept more data for queuing. |
| `Queue` | `bool Queue(Channel &chan, Data data, void *user)` | Queue data for sending on a channel. `user` is an opaque pointer passed to delivery/loss callbacks. Returns `true` on success. |
| `Send` | `void Send()` | Flush all queued fragments across both channels. Calls `Platform::Send()` for each outgoing packet. |
| `Update` | `void Update()` | Process pending acknowledgments and detect lost packets. Call once per frame. |
| `Receive` | `int Receive(Data data)` | Process an incoming packet. Returns `RECV_RESULT_NONE` on success or `RECV_RESULT_DISCONNECT` on fatal error. |
| `Receive` | `int Receive(Channel &chan, FragmentHeader header, Data data)` | Process an incoming fragment for a specific channel. |

---

## Channel

Represents a single communication channel (reliable or unreliable) with send/receive queues.

```cpp
struct Channel {
    uint8_t Id{0};            // Channel identifier (1 = Game, 2 = Streaming)
    bool Reliable{false};     // True for reliable delivery with resends

    size_t MtuData;           // Maximum payload per fragment (PACKET_MTU_BYTES - sizeof(FragmentHeader))

    LinkList<FragmentGroup> Notify{};   // Pending delivery notifications
    uint32_t SendGroup{0};              // Next send group counter
    LinkList<Fragment> SendQueue{};     // Fragments waiting to be sent
    LinkList<Fragment> ResendQueue{};   // Fragments pending resend (reliable only)
    uint32_t RecvGroup{0};              // Next expected receive group
    LinkList<Fragment> RecvList{};      // Received fragments awaiting reassembly
};
```

### Constructor

```cpp
Channel(uint8_t id, bool reliable);
```

Non-copyable.

---

## FragmentHeader

Wire format header prepended to every fragment.

```cpp
struct FragmentHeader {
    uint8_t Flags;           // Fragment flags (FRAG_FLAG_DATA, FRAG_FLAG_ACKS, FRAG_FLAG_LAST_FRAG)
    uint8_t _reserved_0;    // Reserved
    uint8_t Channel;         // Channel ID
    uint8_t _reserved_1;    // Reserved
    uint16_t Sequence;       // Packet sequence number
    uint16_t AckSequence;    // Last received remote sequence number
    uint64_t AckMask;        // Bitmask of received packets before AckSequence
    uint32_t FragGroup;      // Fragment group identifier
    uint32_t FragIndex;      // Index within the fragment group
};

static_assert(sizeof(FragmentHeader) == 24);
```

| Field | Offset | Size | Description |
|-------|--------|------|-------------|
| `Flags` | 0 | 1 | Bitfield controlling fragment contents. |
| `Channel` | 2 | 1 | Channel this fragment belongs to. |
| `Sequence` | 4 | 2 | Monotonically increasing send sequence. |
| `AckSequence` | 6 | 2 | Highest remote sequence acknowledged. |
| `AckMask` | 8 | 8 | Bitmask of the 64 packets preceding `AckSequence`. |
| `FragGroup` | 16 | 4 | Groups fragments that form a single logical message. |
| `FragIndex` | 20 | 4 | Position of this fragment within the group. |

---

## Fragment

A single fragment in a send or receive queue.

```cpp
struct Fragment {
    Fragment *Prev{nullptr};       // LinkList previous pointer
    Fragment *Next{nullptr};       // LinkList next pointer
    FragmentGroup *Group{nullptr}; // Parent group

    double SendTime{0};            // Timestamp when the fragment was sent

    FragmentHeader Header{};       // Wire header
    Data Data{};                   // Payload bytes
};
```

Non-copyable.

---

## FragmentGroup

Groups multiple fragments into a single logical message. Tracks delivery status.

```cpp
struct FragmentGroup {
    FragmentGroup *Prev{nullptr};       // LinkList previous pointer
    FragmentGroup *Next{nullptr};       // LinkList next pointer

    void *User{nullptr};                // Opaque pointer passed to delivery/loss callbacks
    Data Data{};                        // Reassembled payload (populated on full delivery)

    std::optional<bool> WasLost{};      // Set to true if any fragment was lost
    std::optional<bool> WasDelivered{}; // Set to true if all fragments were delivered

    uint32_t Group{0};                  // Group identifier
    uint32_t Count{0};                  // Total fragment count in the group

    uint8_t *Delivered{nullptr};        // Per-fragment delivery bitmap
    uint32_t DeliveredCount{0};         // Number of fragments confirmed delivered
};
```

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `IsDone` | `bool IsDone() const` | Returns `true` if the group outcome (lost or delivered) has been determined. |
| `SetDelivered` | `void SetDelivered(Fragment *fragment)` | Mark a fragment as delivered. When all fragments are delivered, sets `WasDelivered`. |

Non-copyable.

---

## Platform

Abstract interface that the transport implementation must provide. The `Connection` calls these methods during its operation.

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

| Method | Description |
|--------|-------------|
| `Clock()` | Return the current time in seconds (monotonic). Used for RTT calculation and resend timing. |
| `Send(connection, data)` | Transmit a raw packet over the network. |
| `Recv(connection, channel, data)` | Called when a complete message is reassembled from fragments. |
| `Lost(connection, channel, user, data)` | Called when a message is determined to be lost (unreliable) or all resend attempts are exhausted (reliable). `user` is the opaque pointer from `Queue()`. |
| `Delivered(connection, channel, user, data)` | Called when a message is confirmed delivered by the remote end. `user` is the opaque pointer from `Queue()`. |

---

## Constants

### MTU and Payload

```cpp
constexpr int DEFAULT_HEADERS = 40 + 8 + 96;                          // IP + UDP + Photon overhead (144 bytes)
constexpr int MAX_MTU_BYTES_TOTAL = 1280;                              // Maximum packet size (IPv6 minimum MTU)
constexpr int MAX_MTU_BYTES_PAYLOAD = MAX_MTU_BYTES_TOTAL - DEFAULT_HEADERS;  // Usable payload (1136 bytes)
constexpr int PACKET_MTU_BYTES = MAX_MTU_BYTES_PAYLOAD / 8 * 8;       // Payload rounded down to 8-byte alignment (1136 bytes)
```

### Receive Result Codes

```cpp
constexpr int RECV_RESULT_NONE = 0;          // Packet processed successfully
constexpr int RECV_RESULT_DISCONNECT = 2;    // Fatal error, connection should be dropped
```

### Fragment Flags

```cpp
constexpr uint8_t FRAG_FLAG_DATA      = 1 << 1;   // Fragment carries payload data
constexpr uint8_t FRAG_FLAG_ACKS      = 1 << 2;   // Fragment carries acknowledgment data
constexpr uint8_t FRAG_FLAG_LAST_FRAG = 1 << 7;   // Last fragment in the group
```
