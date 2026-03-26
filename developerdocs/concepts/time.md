# Network Time

Fusion maintains a synchronized clock across all clients in a room. This clock is derived from the Photon server's authoritative time and adjusted locally to account for network latency and jitter.

## Time Model

Each client tracks several internal values to maintain clock synchronization:

```
Server Clock (authoritative)
       |
       |  time update packet
       v
+-------------------------------+
| _serverClock  (last received) |
| _localClock   (accumulated dt)|
| _timeDiff     (server - local)|
| _serverClockScale  (rate adj) |
+-------------------------------+
       |
       v
NetworkTime() = _localClock + _timeDiff
```

| Field | Type | Description |
|-------|------|-------------|
| `_localClock` | `double` | Accumulated local time from `UpdateFrameBegin(dt)` calls |
| `_serverClock` | `double` | Last received server time, smoothly adjusted |
| `_timeDiff` | `double` | Offset: `_serverClock - _localClock` |
| `_serverClockScale` | `double` | Rate multiplier for smooth convergence |

## Client API

### NetworkTime()

```cpp
double Client::NetworkTime() const;
```

Returns the current synchronized network time in seconds. This is the primary time value for gameplay synchronization. All clients in the same room converge to approximately the same `NetworkTime()` value, accounting for their individual latencies.

Use `NetworkTime()` for:

- Synchronized game events (countdowns, round starts)
- Interpolation timestamps
- Time-based gameplay logic that must agree across clients

### NetworkTimeScale()

```cpp
double Client::NetworkTimeScale() const;
```

Returns the current time scale factor. The SDK adjusts this to smoothly converge the local clock toward the server clock:

- Under normal conditions, the scale is close to `1.0`.
- When the local clock is behind, the scale increases slightly (speeds up).
- When the local clock is ahead, the scale decreases (slows down).

Engine integrations can use this to scale gameplay speed in sync with the network clock, though this is optional.

### NetworkTimeDiff()

```cpp
double Client::NetworkTimeDiff() const;
```

Returns the raw offset between server time and local time (`_timeDiff`). A positive value means the server clock is ahead of the local clock. Useful for diagnostics and latency estimation, but rarely needed in gameplay code.

### GetTime(obj)

```cpp
double Client::GetTime(const Object* obj);
```

Returns the synchronized time for a specific object. For an `ObjectRoot`, this reads the `Time` field which is updated each frame by the SDK when remote state arrives. The object's time may differ from `NetworkTime()` due to per-object timing from the authority client.

### Per-Object Time

```cpp
class ObjectRoot : public Object {
public:
    double Time;  // Network-synchronized time for this object
};
```

This value is set during `UpdateFrameBegin()` when state updates arrive for the object. It represents the timestamp at which the authority client generated the state snapshot. Use this for per-object interpolation or extrapolation.

## Tick Counters

In addition to continuous time, the SDK maintains discrete tick counters:

### GetSendTick()

```cpp
int Client::GetSendTick() const;
```

Returns the current outgoing tick counter (`_sendTick`). This is a monotonically increasing `uint32_t` that increments each time the SDK sends a state packet. Used internally for change detection and ack tracking.

### GetReceivedCounter()

```cpp
int Client::GetReceivedCounter() const;
```

Returns the last received tick counter from the server (`_receiveCounter`). This tells you how many state updates the client has received total. Useful for detecting stalls or measuring update frequency.

### Per-Object Ticks

Each object tracks its own send/receive ticks:

```cpp
class Object {
    Tick RemoteTickSent;    // Last tick we sent for this object
    Tick RemoteTickAcked;   // Last tick the server acknowledged
};
```

## Time Encoding

The SDK uses quantized encoding to transmit time values efficiently over the wire:

```cpp
int64_t ClockQuantizeEncode(double clock);
double  ClockQuantizeDecode(int64_t clock);
```

These functions convert between `double` seconds and a compact `int64_t` representation suitable for network transmission via `ReadBuffer`/`WriteBuffer`:

```cpp
// Writing time in a packet
void WriteBuffer::Time(double time);
void WriteBuffer::TimeBase(double time);

// Reading time from a packet
double ReadBuffer::Time();
double ReadBuffer::TimeBase();
```

`TimeBase` is written once per packet as an absolute reference. Subsequent `Time` values within the same packet are encoded as deltas relative to the base, reducing bandwidth.

## How Time Synchronization Works

```
1. Server sends time
   Server --[authoritative clock]--> Client

2. SDK receives time (during UpdateFrameBegin)
   ServerTimeReceived(serverTime)

3. Offset computed
   _timeDiff = serverTime - _localClock

4. Smooth adjustment
   _serverClockScale adjusted to converge gradually
   (prevents visible jitter in gameplay)

5. Local advancement (each frame)
   _localClock += dt * _serverClockScale
```

The SDK never jumps the local clock to the server time directly. Instead, it adjusts the rate at which the local clock advances. This produces smooth, jitter-free time progression while still converging to the authoritative server clock.

## The dt Parameter

The `dt` parameter to `UpdateFrameBegin(double dt)` is critical for time synchronization:

```cpp
// Correct: real elapsed time
fusionClient->UpdateFrameBegin(delta_from_engine);

// Incorrect: fixed timestep (unless truly fixed framerate)
fusionClient->UpdateFrameBegin(1.0 / 60.0);  // Causes drift

// Incorrect: zero
fusionClient->UpdateFrameBegin(0.0);  // Time stops advancing
```

The SDK uses `dt` to advance `_localClock`. Inaccurate values cause the local clock to drift from the server, forcing larger rate corrections and potential gameplay issues.

## RTT (Round-Trip Time)

```cpp
double Client::GetRtt() const;
```

Returns the round-trip time in seconds. This is the measured latency between the client and the Photon server. Useful for:

- Displaying ping to the player
- Adjusting interpolation buffer size
- Compensating for network delay in gameplay

## Integration Guidelines

| Guideline | Rationale |
|-----------|-----------|
| Always pass real delta time to `UpdateFrameBegin()` | Prevents clock drift |
| Do not cache `NetworkTime()` across frames | Value changes every frame |
| Use `NetworkTime()` for gameplay, not wall-clock time | Wall-clock diverges across clients |
| Use `GetTime(obj)` for per-object interpolation | More accurate than global time |
| Use `GetSendTick()` for frame-level comparisons | Integer, no floating-point issues |

## Related

- [Frame Loop](frame-loop.md) -- How `dt` feeds into time synchronization
- [Objects](objects.md) -- `ObjectRoot::Time` field
- [Serialization](serialization.md) -- Time encoding in ReadBuffer/WriteBuffer
