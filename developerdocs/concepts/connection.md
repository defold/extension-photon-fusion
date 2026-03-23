# Connection

Fusion uses the Photon Cloud (or a local Photon server) for transport. The connection lifecycle is managed through the `Photon` class, which wraps the Photon LoadBalancing C++ SDK. The `Client` class coordinates Fusion state on top of this connection.

## Client Construction

```cpp
Client(const CharType* appId,
       const CharType* appVersion,
       const ExitGames::LoadBalancing::ClientConstructOptions& options = {});
```

| Parameter | Purpose |
|-----------|---------|
| `appId` | Photon application ID from the Photon Dashboard |
| `appVersion` | Version string for client compatibility matching (clients with different versions cannot join the same room) |
| `options` | Photon-level options (region selection mode, protocol, etc.) |

The `Client` constructor creates an internal `Photon` instance and a `Notify::Connection` for reliable state delivery. No network I/O happens until `ConnectCloud()` or `ConnectLocal()` is called.

Access the Photon layer via `Client::Photon()`:

```cpp
SharedMode::Photon& photon = client->Photon();
```

## Connecting

### Cloud Connection

```cpp
void Client::ConnectCloud(const CharType* region,
                          const CharType* userId,
                          const CharType* serverAddress);
```

Connects to the Photon Cloud. Parameters:

| Parameter | Purpose |
|-----------|---------|
| `region` | Photon region code (e.g., `"us"`, `"eu"`, `"asia"`) |
| `userId` | Unique user identifier for this client |
| `serverAddress` | Custom name server address, or empty string (`""`) for the default Photon name server |

The connection process is asynchronous. After calling `ConnectCloud()`, you **must** continue calling `Photon::Service(true)` every frame to pump the connection state machine. The status progresses through several states before reaching `Connected`.

### Local Connection

```cpp
void Client::ConnectLocal(const CharType* endpoint);
```

Connects to a local Photon server instance (e.g., for development or LAN play). The `endpoint` is typically `"localhost:5055"` or similar.

## Connection Status State Machine

```
None (0)
  |
  v
Connecting (1) --[error]--> Error (2)
  |                           |
  v                           v
Connected (4)            Disconnected (3)
  |
  v
JoiningRoom (5) --[error]--> Disconnected (3)
  |
  v
InRoom (6) --[leave/error]--> Disconnected (3)
  |
  |--[timeout]--> TimeOut (7)
  |--[invalid region]--> InvalidRegion (8)
```

### Status Constants

```cpp
constexpr int PhotonClient_StatusNone         = 0;
constexpr int PhotonClient_StatusConnecting   = 1;
constexpr int PhotonClient_StatusError        = 2;
constexpr int PhotonClient_StatusDisconnected = 3;
constexpr int PhotonClient_StatusConnected    = 4;
constexpr int PhotonClient_StatusJoiningRoom  = 5;
constexpr int PhotonClient_StatusInRoom       = 6;
constexpr int PhotonClient_StatusTimeOut      = 7;
constexpr int PhotonClient_StatusInvalidRegion = 8;
```

### Status Queries

```cpp
int  Photon::Status() const;           // Raw status code
bool Photon::IsConnected() const;      // Status >= Connected (4)
bool Photon::IsJoiningOrInRoom() const; // Status >= JoiningRoom (5)
bool Photon::IsInRoom() const;         // Status == InRoom (6)
```

### Region Selection Modes

```cpp
constexpr uint8_t PhotonClient_RegionSelectionMode_Default = 0;
constexpr uint8_t PhotonClient_RegionSelectionMode_Select  = 1;
constexpr uint8_t PhotonClient_RegionSelectionMode_Best    = 2;
```

Set via `ClientConstructOptions::setRegionSelectionMode()` before constructing the `Client`. Mode `Select` (1) allows you to specify the region directly in `ConnectCloud()`. Mode `Best` (2) auto-selects the lowest-latency region.

## Room Operations

All room operations require `IsConnected() == true`. They are asynchronous -- the status transitions to `JoiningRoom` and eventually `InRoom` (or back to `Disconnected` on failure).

### Join or Create

```cpp
void Photon::JoinOrCreateRoom(const CharType* room,
                               const ExitGames::LoadBalancing::RoomOptions& options = {});
```

Joins an existing room by name, or creates it if it does not exist. This is the most common room entry point.

### Join Existing

```cpp
void Photon::JoinRoom(const CharType* room);
void Photon::JoinRoomRandom();
void Photon::JoinOrCreateRoomRandom(const CharType* room,
                                     const ExitGames::LoadBalancing::RoomOptions& options = {});
```

- `JoinRoom` -- fails if the room does not exist.
- `JoinRoomRandom` -- joins any available room.
- `JoinOrCreateRoomRandom` -- hybrid: tries random join, falls back to create.

### Create

```cpp
void Photon::CreateRoom(const CharType* room,
                         const ExitGames::LoadBalancing::RoomOptions& options = {});
```

Creates a new room. Fails if a room with the same name already exists.

### Leave

```cpp
void Photon::LeaveRoom();
```

Leaves the current room. Triggers the `OnLeaveRoom` callback chain. All local objects are cleaned up by the SDK.

## Callbacks

The `Client` provides `std::function` callbacks for connection lifecycle events:

```cpp
// Room events
std::function<void()> OnRoomJoin;
std::function<void()> OnRoomLeave;

// Player events (on the Photon layer)
// Set via Photon's internal callbacks:
std::function<void(int)> OnPlayerJoinedCallback;
std::function<void(int)> OnPlayerLeftCallback;
```

`OnRoomJoin` fires after the client successfully enters a room (status transitions to `InRoom`). `OnRoomLeave` fires when the client exits a room for any reason (explicit leave, kick, disconnect).

These callbacks fire during `Photon::Service()` dispatch, which runs on the same thread as your frame loop.

## Fusion-Level State Queries

Once in a room, the `Client` provides:

```cpp
bool Client::IsRunning() const;        // Has config AND is in room
bool Client::IsMasterClient();         // This client is the room's master
PlayerId Client::LocalPlayerId();      // This client's PlayerId
int32_t Client::PlayerCount() const;   // Number of players in the room
double Client::GetRtt() const;         // Round-trip time in seconds
```

### Master Client

The master client is the authoritative peer for operations that require a single decision-maker (scene changes, global state). It is automatically assigned by the Photon server -- typically the first player to join the room. If the master disconnects, the server promotes another player.

```cpp
constexpr PlayerId MasterClientPlayerId = 0xFFFFFFFF;

int32_t Photon::MasterClient();  // Returns the master's player number
```

## Disconnect and Shutdown

### Graceful Disconnect

```cpp
bool Photon::Disconnect();
```

Disconnects from the Photon server. Triggers `OnLeaveRoom` (if in a room), then `OnDisconnected`. The client can reconnect by calling `ConnectCloud()` or `ConnectLocal()` again.

### Shutdown

```cpp
void Client::Shutdown(bool development);
```

Full teardown. Destroys all objects, closes the connection, and releases internal resources. After `Shutdown()`, the `Client` instance should be deleted. The `development` flag controls whether additional diagnostic logging is emitted.

## Typical Connection Flow

```cpp
// 1. Construct
auto* client = new SharedMode::Client(appId, appVersion, options);

// 2. Set up callbacks
client->OnRoomJoin = [&]() { /* ready to create objects */ };
client->OnRoomLeave = [&]() { /* cleanup */ };

// 3. Connect
client->ConnectCloud(region, userId, FUSION_STR(""));

// 4. Frame loop: must call Service() every frame
while (running) {
    client->Photon().Service(true);

    if (client->Photon().IsConnected() && !joinedRoom) {
        client->Photon().JoinOrCreateRoom(roomName);
        joinedRoom = true;
    }

    if (client->IsRunning()) {
        // Full frame loop: outbound -> End -> Begin -> inbound
        sync_outbound();
        client->UpdateFrameEnd();
        client->UpdateFrameBegin(delta);
        sync_inbound();
    }
}

// 5. Shutdown
client->Shutdown(false);
delete client;
```

## Related

- [Frame Loop](frame-loop.md) -- The mandatory 3-step frame sequence
- [Architecture](architecture.md) -- Layered design and Photon transport
- [Client API](../reference/client-api.md) -- Full method reference
- [Photon API](../reference/photon-api.md) -- Photon class reference
