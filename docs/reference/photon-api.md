# SharedMode::Photon API Reference

`SharedMode::Photon` is the transport layer wrapper around the Photon Realtime SDK (`ExitGames::LoadBalancing::Client`). It manages the connection lifecycle, room operations, event sending/receiving, and player queries. Access it via `Client::Photon()`.

**Header**: `Photon.h`

---

## Table of Contents

- [Construction](#construction)
- [Connection](#connection)
- [Room Operations](#room-operations)
- [Status Queries](#status-queries)
- [Player Queries](#player-queries)
- [Network Operations](#network-operations)
- [Callback Registration](#callback-registration)
- [Configuration](#configuration)
- [Status Constants](#status-constants)
- [Region Selection Mode Constants](#region-selection-mode-constants)
- [Callbacks (Internal)](#callbacks-internal)

---

## Construction

### Constructor

```cpp
Photon(const CharType* appId, const CharType* appVersion,
       const ExitGames::LoadBalancing::ClientConstructOptions& clientConstructOptions = ClientConstructOptions());
```

Creates a new Photon transport instance. This is called internally by `Client`'s constructor. Engine integrations do not construct `Photon` directly.

| Parameter | Type | Description |
|-----------|------|-------------|
| `appId` | `const CharType*` | Photon application ID. |
| `appVersion` | `const CharType*` | Application version string for client matching. |
| `clientConstructOptions` | `const ClientConstructOptions&` | Optional construction options (e.g., region selection mode). |

---

### Destructor

```cpp
~Photon();
```

Destroys the Photon instance and releases the underlying `ExitGames::LoadBalancing::Client`.

---

## Connection

### ConnectCloud

```cpp
void ConnectCloud(const CharType* region, const CharType* userId, const CharType* serverAddress);
```

Connects to the Photon Cloud. The connection is asynchronous; poll `Status()` or `IsConnected()` to track progress.

| Parameter | Type | Description |
|-----------|------|-------------|
| `region` | `const CharType*` | Target region code (e.g., `"us"`, `"eu"`, `"asia"`). Active when region selection mode is Select (1). |
| `userId` | `const CharType*` | Unique user ID for this client. |
| `serverAddress` | `const CharType*` | Custom server address, or empty string for default. **Must not be `nullptr`**. |

**Preconditions**: Not already connected.

**Notes**:
- Call `Service(true)` every frame after initiating connection, otherwise the state machine will not advance.
- Status transitions: `None` -> `Connecting` -> `Connected`.

---

### ConnectLocal

```cpp
void ConnectLocal(const CharType* address);
```

Connects to a local Photon server (for development or LAN).

| Parameter | Type | Description |
|-----------|------|-------------|
| `address` | `const CharType*` | Local server address (e.g., `"localhost:5055"`). |

---

### Disconnect

```cpp
bool Disconnect();
```

Disconnects from the Photon server. If in a room, leaves the room first.

**Returns**: `true` if disconnect was initiated.

**Notes**:
- After disconnection, `Status()` transitions to `PhotonClient_StatusDisconnected`.
- The `OnDisconnectedCallback` fires when disconnection completes.

---

## Room Operations

### JoinRoom

```cpp
void JoinRoom(const CharType* room);
```

Joins an existing room by name.

| Parameter | Type | Description |
|-----------|------|-------------|
| `room` | `const CharType*` | Room name to join. |

**Preconditions**: Must be connected (`IsConnected() == true`).

**Notes**:
- Status transitions: `Connected` -> `JoiningRoom` -> `InRoom`.
- Fires `OnJoinRoomCallbacks` when the join completes.
- Fails if the room does not exist.

---

### JoinRoomRandom

```cpp
void JoinRoomRandom();
```

Joins a random available room.

**Preconditions**: Must be connected.

---

### JoinOrCreateRoom

```cpp
void JoinOrCreateRoom(const CharType* room,
                      const ExitGames::LoadBalancing::RoomOptions& options = RoomOptions());
```

Joins an existing room or creates it if it does not exist.

| Parameter | Type | Description |
|-----------|------|-------------|
| `room` | `const CharType*` | Room name. |
| `options` | `const RoomOptions&` | Room creation options (max players, custom properties, etc.). Only used if the room is created. |

**Preconditions**: Must be connected.

**Notes**:
- This is the most commonly used room operation. It provides atomic join-or-create semantics.

---

### JoinOrCreateRoomRandom

```cpp
void JoinOrCreateRoomRandom(const CharType* room,
                            const ExitGames::LoadBalancing::RoomOptions& options = RoomOptions());
```

Attempts to join a random room; if none are available, creates a new room with the given name.

| Parameter | Type | Description |
|-----------|------|-------------|
| `room` | `const CharType*` | Fallback room name if creation is needed. |
| `options` | `const RoomOptions&` | Room creation options. |

**Preconditions**: Must be connected.

---

### CreateRoom

```cpp
void CreateRoom(const CharType* room,
                const ExitGames::LoadBalancing::RoomOptions& options = RoomOptions());
```

Creates a new room. Fails if a room with the given name already exists.

| Parameter | Type | Description |
|-----------|------|-------------|
| `room` | `const CharType*` | Room name. |
| `options` | `const RoomOptions&` | Room configuration options. |

**Preconditions**: Must be connected.

---

### LeaveRoom

```cpp
void LeaveRoom();
```

Leaves the current room. The client remains connected to the Photon Cloud after leaving.

**Preconditions**: Must be in a room (`IsInRoom() == true`).

**Notes**:
- Fires `OnLeaveRoomCallbacks` when the leave completes.
- Status transitions: `InRoom` -> `Connected`.

---

## Status Queries

### Status

```cpp
int Status() const;
```

Returns the current connection/room status as an integer code.

**Returns**: One of the `PhotonClient_Status*` constants (see [Status Constants](#status-constants)).

**Notes**:
- Returns `0` (`PhotonClient_StatusNone`) if the underlying client has not been initialized.

---

### IsConnected

```cpp
bool IsConnected() const;
```

Returns `true` if the client is connected to the Photon server (status >= `Connected`). This includes being in a room.

**Returns**: `true` if connected (status is `Connected`, `JoiningRoom`, or `InRoom`).

---

### IsJoiningOrInRoom

```cpp
bool IsJoiningOrInRoom() const;
```

Returns `true` if the client is currently joining a room or is already in one.

**Returns**: `true` if status is `JoiningRoom` or `InRoom`.

---

### IsInRoom

```cpp
bool IsInRoom() const;
```

Returns `true` if the client is currently in a room and fully joined.

**Returns**: `true` if status is exactly `InRoom`.

---

### LoadBalancingClient

```cpp
ExitGames::LoadBalancing::Client& LoadBalancingClient() const;
```

Returns a reference to the underlying Photon Realtime `LoadBalancing::Client` for advanced operations not exposed by the `Photon` wrapper.

**Returns**: Reference to the internal `ExitGames::LoadBalancing::Client`.

**Notes**:
- Use with caution. Direct manipulation of the underlying client can interfere with Fusion's state management.

---

## Player Queries

### LocalPlayer

```cpp
int32_t LocalPlayer();
```

Returns the Photon player number of this client.

**Returns**: Local player number as `int32_t`.

---

### MasterClient

```cpp
int32_t MasterClient();
```

Returns the Photon player number of the current room's master client.

**Returns**: Master client player number as `int32_t`.

**Notes**:
- The master client role automatically transfers when the current master disconnects.

---

## Network Operations

### Service

```cpp
void Service(bool dispatch);
```

Drives the Photon network layer. Must be called every frame to process incoming/outgoing messages, keepalives, and connection state transitions.

| Parameter | Type | Description |
|-----------|------|-------------|
| `dispatch` | `bool` | If `true`, dispatches incoming messages (triggers callbacks). If `false`, only sends outgoing data without processing incoming messages. |

**Notes**:
- This is the most important call in the entire transport layer. Failing to call `Service(true)` every frame will cause the connection to time out and eventually drop.
- Typically called at the start of the frame loop, before `Client::UpdateFrameBegin()`.

---

### SendEvent

```cpp
void SendEvent(nByte code, nByte* data, size_t length, bool reliable);
```

Sends a raw Photon event to all players in the room.

| Parameter | Type | Description |
|-----------|------|-------------|
| `code` | `nByte` | Event code identifier. |
| `data` | `nByte*` | Pointer to the event payload. |
| `length` | `size_t` | Payload length in bytes. |
| `reliable` | `bool` | If `true`, the event is sent reliably (guaranteed delivery). If `false`, unreliable (may be dropped). |

**Notes**:
- This is the low-level event API used internally by the Fusion SDK for state replication and RPCs.
- Engine integrations typically use `Client::SendUserRpc()` instead of sending raw events.

---

## Callback Registration

### PushOnJoinedCallback

```cpp
void PushOnJoinedCallback(const std::function<void()>& callback);
```

Pushes a callback onto the join-room callback stack. The most recently pushed callback fires when the client joins a room.

| Parameter | Type | Description |
|-----------|------|-------------|
| `callback` | `const std::function<void()>&` | Function to call on room join. |

**Notes**:
- Uses a stack model: `PushOnJoinedCallback` adds to the top, `PopOnJoinedCallback` removes from the top.
- The Fusion `Client` sets its own join callback internally. Engine integrations can push additional callbacks on top.

---

### PopOnJoinedCallback

```cpp
void PopOnJoinedCallback();
```

Removes the most recently pushed join-room callback from the stack.

---

## Configuration

### SetLogLevel

```cpp
void SetLogLevel(const int level) const;
```

Sets the Photon debug output level for the underlying LoadBalancing client.

| Parameter | Type | Description |
|-----------|------|-------------|
| `level` | `int` | Photon debug level (higher = more verbose). |

---

## Status Constants

```cpp
constexpr int PhotonClient_StatusNone         = 0;  // Initial state, not connected
constexpr int PhotonClient_StatusConnecting    = 1;  // Connection in progress
constexpr int PhotonClient_StatusError         = 2;  // Connection failed
constexpr int PhotonClient_StatusDisconnected  = 3;  // Disconnected from server
constexpr int PhotonClient_StatusConnected     = 4;  // Connected to server, not in a room
constexpr int PhotonClient_StatusJoiningRoom   = 5;  // Join room operation in progress
constexpr int PhotonClient_StatusInRoom        = 6;  // Successfully joined a room
constexpr int PhotonClient_StatusTimeOut       = 7;  // Connection timed out
constexpr int PhotonClient_StatusInvalidRegion = 8;  // Specified region is invalid
```

**Status lifecycle**:

```
None (0) --> Connecting (1) --> Connected (4) --> JoiningRoom (5) --> InRoom (6)
                  |                                                       |
                  v                                                       v
              Error (2)                                              LeaveRoom
              TimeOut (7)                                                |
              InvalidRegion (8)                                         v
                  |                                                Connected (4)
                  v                                                       |
           Disconnected (3) <--------- Disconnect() ---------------------|
```

---

## Region Selection Mode Constants

```cpp
constexpr uint8_t PhotonClient_RegionSelectionMode_Default = 0;  // Use Photon's default region selection
constexpr uint8_t PhotonClient_RegionSelectionMode_Select  = 1;  // Use the region specified in ConnectCloud()
constexpr uint8_t PhotonClient_RegionSelectionMode_Best    = 2;  // Auto-select the best region by ping
```

Set via `ExitGames::LoadBalancing::ClientConstructOptions::setRegionSelectionMode()` before constructing the `Client`.

---

## Callbacks (Internal)

These callbacks are set by the Fusion `Client` class internally and are not typically used by engine integrations directly. They are documented here for completeness.

### OnJoinRoomInternalCallback

```cpp
std::function<void()> OnJoinRoomInternalCallback;
```

Internal callback fired when the client joins a room. Set by `Client` to initialize Fusion session state.

---

### OnLeaveRoomInternalCallback

```cpp
std::function<void()> OnLeaveRoomInternalCallback;
```

Internal callback fired when the client leaves a room. Set by `Client` to clean up Fusion session state.

---

### OnDisconnectedCallback

```cpp
std::function<void()> OnDisconnectedCallback;
```

Fires when the client is fully disconnected from the server.

---

### OnPlayerJoinedCallback

```cpp
std::function<void(int)> OnPlayerJoinedCallback;
```

Fires when a player joins the room. The parameter is the player number.

---

### OnPlayerLeftCallback

```cpp
std::function<void(int)> OnPlayerLeftCallback;
```

Fires when a player leaves the room. The parameter is the player number.

---

### OnDataReceivedInternalCallback

```cpp
std::function<void(uint8_t, Data)> OnDataReceivedInternalCallback;
```

Internal callback for raw Photon events. The `Client` uses this to route state packets, RPCs, and scene changes. The first parameter is the event code; the second is the raw payload data.
