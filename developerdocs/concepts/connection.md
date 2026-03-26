# Connection

Fusion uses the Photon Cloud (or a self-hosted Photon server) for transport. The connection lifecycle is managed through `PhotonMatchmaking::RealtimeClient`, which provides an async `Task<Result<T>>` API. The `SharedMode::Client` coordinates Fusion state on top of this connection.

## Two-Layer Construction

Fusion's networking is split into two objects that you construct independently:

```
 RealtimeClient   <--- Photon transport, rooms, matchmaking
       |
 SharedMode::Client  <--- Fusion state, objects, RPCs
```

### Step 1: RealtimeClient

```cpp
PhotonMatchmaking::ClientConstructOptions options;
options.appId      = PHOTON_STR("your-app-id");
options.appVersion = PHOTON_STR("1.0");

auto realtimeClient = std::make_unique<PhotonMatchmaking::RealtimeClient>(options);
```

#### ClientConstructOptions

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `appId` | `StringType` | -- | Photon application ID from the Dashboard |
| `appVersion` | `StringType` | -- | Client version for room isolation |
| `protocol` | `ConnectionProtocol` | `Default` | UDP or TCP |
| `useAlternativePorts` | `bool` | `false` | Use alternative port range |
| `regionSelectionMode` | `RegionSelectionMode` | `Default` | How region is chosen |
| `autoLobbyStats` | `bool` | `false` | Auto-request lobby statistics |
| `serialization` | `SerializationProtocol` | `Protocol1_8` | Wire format version |
| `disconnectTimeoutMs` | `optional<int>` | -- | Override disconnect timeout |
| `pingIntervalMs` | `optional<int>` | -- | Override ping interval |
| `enableCrc` | `optional<bool>` | -- | CRC checking |
| `sentCountAllowance` | `optional<int>` | -- | Outgoing command buffer |
| `quickResendAttempts` | `optional<uint8_t>` | -- | Fast retransmit count |
| `limitOfUnreliableCommands` | `optional<int>` | -- | Unreliable queue cap |

### Step 2: SharedMode::Client

```cpp
auto fusionClient = std::make_unique<SharedMode::Client>(*realtimeClient);
```

The `Client` constructor takes a reference to the `RealtimeClient` and internally subscribes to its events via `Broadcaster`. No network I/O happens until you connect.

## Connecting

### Async Flow with Task<Result<T>>

All connection and room operations return `Task<Result<T>>`, which is a C++20 coroutine type. Tasks complete asynchronously as `RealtimeClient::Service()` is called each frame.

```
Connect() --> Service() loop --> SelectRegion() --> Service() loop --> JoinOrCreateRoom()
```

#### ConnectOptions

```cpp
PhotonMatchmaking::ConnectOptions connectOpts;
connectOpts.auth.userId = PHOTON_STR("player-123");
connectOpts.username    = PHOTON_STR("PlayerName");
connectOpts.serverType  = PhotonMatchmaking::ServerType::NameServer;
```

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `auth` | `AuthenticationValues` | -- | User ID, custom auth type, parameters, data |
| `username` | `StringType` | -- | Display name |
| `serverType` | `ServerType` | `NameServer` | Which server to connect to |
| `serverAddress` | `StringType` | -- | Custom server address |
| `tryUseDatagramEncryption` | `bool` | `false` | Encrypt UDP packets |
| `useBackgroundSendReceiveThread` | `bool` | `true` | Background I/O thread |

#### AuthenticationValues

```cpp
struct AuthenticationValues {
    PhotonCommon::StringType userId;
    CustomAuthenticationType type;  // None, Custom, Steam, Facebook, etc.
    PhotonCommon::StringType parameters;
    std::variant<std::monostate, std::vector<uint8_t>, StringType, PropertyMap> data;
};
```

### Connection Sequence

```cpp
// 1. Connect to name server
auto connectResult = co_await realtimeClient->Connect(connectOpts);
if (connectResult.IsErr()) { /* handle error */ }

// 2. Select region (or use best region)
auto regionResult = co_await realtimeClient->SelectRegion(PHOTON_STR("us"));
if (regionResult.IsErr()) { /* handle error */ }

// 3. Join or create room
PhotonMatchmaking::CreateRoomOptions roomOpts;
roomOpts.maxPlayers = 8;
roomOpts.plugins    = { PHOTON_STR("Fusion3Plugin") };

auto roomResult = co_await realtimeClient->JoinOrCreateRoom(
    PHOTON_STR("my-room"), roomOpts);
if (roomResult.IsErr()) { /* handle error */ }

PhotonMatchmaking::MutableRoomView room = std::move(roomResult).GetValue();
```

If you are not using coroutines, you can poll `Task::IsReady()` in your frame loop:

```cpp
auto task = realtimeClient->Connect(connectOpts);

// In frame loop:
realtimeClient->Service(true);
if (task.IsReady()) {
    auto result = task.Get();
    // proceed...
}
```

## Room Operations

All room operations require `IsConnected() == true` and return `Task<Result<MutableRoomView>>` (except `LeaveRoom` which returns `Task<Result<void>>`).

| Method | Behavior |
|--------|----------|
| `CreateRoom(name, createOpts)` | Create a new room. Fails if name exists. |
| `JoinRoom(name, joinOpts)` | Join existing room by name. |
| `JoinOrCreateRoom(name, createOpts, joinOpts)` | Join if exists, otherwise create. Most common. |
| `JoinRandomRoom(matchmakingOpts)` | Join any available room matching filters. |
| `JoinRandomOrCreateRoom(createOpts, matchmakingOpts)` | Random join with create fallback. |
| `LeaveRoom(willComeBack, sendAuthCookie)` | Leave current room. |

### CreateRoomOptions

| Field | Type | Default |
|-------|------|---------|
| `isVisible` | `bool` | `true` |
| `isOpen` | `bool` | `true` |
| `maxPlayers` | `uint8_t` | `0` (unlimited) |
| `customProperties` | `PropertyMap` | `{}` |
| `lobbyProperties` | `vector<StringType>` | `{}` |
| `playerTtlMs` | `int` | `0` |
| `emptyRoomTtlMs` | `int` | `0` |
| `suppressRoomEvents` | `bool` | `false` |
| `publishUserId` | `bool` | `false` |
| `plugins` | `vector<StringType>` | `{}` |
| `expectedUsers` | `vector<StringType>` | `{}` |

### JoinRoomOptions

| Field | Type | Default |
|-------|------|---------|
| `rejoin` | `bool` | `false` |
| `cacheSliceIndex` | `int` | `0` |
| `expectedUsers` | `vector<StringType>` | `{}` |

### MatchmakingOptions

| Field | Type | Default |
|-------|------|---------|
| `filter` | `PropertyMap` | `{}` |
| `maxPlayers` | `uint8_t` | `0` |
| `mode` | `MatchmakingMode` | `FillRoom` |
| `sqlFilter` | `StringType` | `{}` |
| `expectedUsers` | `vector<StringType>` | `{}` |

### MutableRoomView

Once in a room, `MutableRoomView` provides read and write access to room state:

```cpp
auto room = realtimeClient->GetCurrentRoom();
if (room) {
    auto name = room->GetName();
    int count = room->GetPlayerCount();
    auto& players = room->GetPlayers();
    int master = room->GetMasterClientId();

    // Mutations
    room->SetOpen(false);
    room->SetMaxPlayers(4);
    room->SetProperties({{ PHOTON_STR("map"), PropertyValue(PHOTON_STR("arena")) }});
}
```

## ConnectionState

```cpp
enum class ConnectionState : uint8_t {
    Disconnected,
    Connecting,
    Connected,
    JoiningRoom,
    InRoom,
    LeavingRoom,
    Disconnecting
};
```

```
Disconnected
     |
     v
Connecting --[error]--> Disconnected
     |
     v
Connected
     |
     v
JoiningRoom --[error]--> Disconnected
     |
     v
InRoom --[leave]--> LeavingRoom --> Disconnected
     |
     +--[timeout/error]--> Disconnecting --> Disconnected
```

### State Queries

```cpp
ConnectionState realtimeClient->GetState();
bool realtimeClient->IsConnected();
bool realtimeClient->IsInRoom();
bool realtimeClient->IsInLobby();
DisconnectCause realtimeClient->GetDisconnectCause();
```

## DisconnectCause

```cpp
enum class DisconnectCause : int {
    None = 0,
    DisconnectByServerUserLimit = 1,
    ExceptionOnConnect = 2,
    DisconnectByServer = 3,
    DisconnectByServerLogic = 4,
    TimeoutDisconnect = 5,
    Exception = 6,
    InvalidAuthentication = 7,
    MaxCCUReached = 8,
    InvalidRegion = 9,
    OperationNotAllowedInCurrentState = 10,
    CustomAuthenticationFailed = 11,
    ClientVersionTooOld = 12,
    ClientVersionInvalid = 13,
    DashboardVersionInvalid = 14,
    AuthenticationTicketExpired = 15,
    DisconnectByOperationLimit = 16
};
```

## RealtimeClient Broadcasters

The `RealtimeClient` exposes broadcasters for connection lifecycle events. Subscribe using `Broadcaster::Subscribe()` and manage lifetime with `SubscriptionBag`:

```cpp
PhotonCommon::SubscriptionBag subs;

subs += realtimeClient->OnDisconnected.Subscribe([](DisconnectCause cause) {
    // Handle disconnect
});

subs += realtimeClient->OnPlayerJoined.Subscribe([](const PlayerView& player) {
    // Handle player join
});

subs += realtimeClient->OnPlayerLeft.Subscribe([](int playerNumber, bool isInactive) {
    // Handle player leave
});

subs += realtimeClient->OnMasterClientChanged.Subscribe([](int newId, int oldId) {
    // Handle master migration
});

subs += realtimeClient->OnError.Subscribe([](ErrorCode code, StringViewType msg) {
    // Handle error
});
```

Available broadcasters on `RealtimeClient`:

| Broadcaster | Signature |
|-------------|-----------|
| `OnDisconnected` | `void(DisconnectCause)` |
| `OnError` | `void(ErrorCode, StringViewType)` |
| `OnRoomPropertiesChanged` | `void(const PropertyMap&)` |
| `OnRoomListUpdated` | `void(const vector<RoomListing>&)` |
| `OnMasterClientChanged` | `void(int newId, int oldId)` |
| `OnPlayerJoined` | `void(const PlayerView&)` |
| `OnPlayerLeft` | `void(int playerNumber, bool isInactive)` |
| `OnPlayerPropertiesChanged` | `void(int playerNumber, const PropertyMap&)` |
| `OnEvent` | `void(uint8_t eventCode, int senderId, span<const uint8_t> data)` |
| `OnDirectMessage` | `void(int senderId, span<const uint8_t> data, bool isRelay)` |
| `OnRoomJoined` | `void()` |
| `OnRoomLeft` | `void()` |
| `OnWarning` | `void(int warningCode)` |

## Starting Fusion

After connecting and joining a room, call `Client::Start()` to initialize Fusion state replication:

```cpp
fusionClient->Start();
```

`Start()` creates the internal `Notify::Connection` and begins the Fusion protocol handshake with the server plugin. The `OnFusionStart` broadcaster fires when Fusion is ready:

```cpp
subs += fusionClient->OnFusionStart.Subscribe([&]() {
    // Fusion is ready -- create objects, load scene, etc.
});
```

### OnFusionStart vs OnRoomJoined

- `OnRoomJoined` (on `RealtimeClient`) fires when the Photon room is entered.
- `OnFusionStart` (on `Client`) fires when the Fusion protocol handshake completes and the server plugin config is received.

You must wait for `OnFusionStart` before creating objects or sending RPCs.

### OnForcedDisconnect

```cpp
fusionClient->OnForcedDisconnect.Subscribe([](std::string message) {
    // Server forced us out
});
```

Fires when the server plugin forcibly disconnects the client (e.g., version mismatch, ban).

## Fusion-Level Queries

Once Fusion is running:

```cpp
bool fusionClient->IsRunning();          // Has config AND connection active
bool fusionClient->IsMasterClient();     // Local client is room master
PlayerId fusionClient->LocalPlayerId();  // Local player ID
int32_t fusionClient->PlayerCount();     // Number of players
double fusionClient->GetRtt();           // Round-trip time (seconds)
SdkVersion fusionClient->GetSdkVersion(); // SDK version info
```

## Shutdown

```cpp
fusionClient->Shutdown();
```

Full teardown. Destroys all Fusion objects, closes the Notify connection, and releases internal resources. After `Shutdown()`, the `Client` instance should be destroyed. To disconnect from Photon cleanly, also call:

```cpp
co_await realtimeClient->Disconnect();
```

## Complete Connection Flow

```cpp
// 1. Construct
auto realtimeClient = std::make_unique<PhotonMatchmaking::RealtimeClient>(constructOpts);
auto fusionClient   = std::make_unique<SharedMode::Client>(*realtimeClient);

// 2. Subscribe to Fusion events
PhotonCommon::SubscriptionBag subs;
subs += fusionClient->OnFusionStart.Subscribe([&]() {
    // Ready to create objects
    create_scene_objects();
});
subs += fusionClient->OnObjectReady.Subscribe([](SharedMode::ObjectRoot* obj) {
    // Remote object ready
});

// 3. Connect + join room (coroutine or polling)
co_await realtimeClient->Connect(connectOpts);
co_await realtimeClient->SelectRegion(PHOTON_STR("us"));
co_await realtimeClient->JoinOrCreateRoom(PHOTON_STR("my-room"), roomOpts);

// 4. Start Fusion
fusionClient->Start();

// 5. Frame loop
while (running) {
    realtimeClient->Service(true);

    if (fusionClient->IsRunning()) {
        sync_outbound();
        fusionClient->UpdateFrameEnd();
        fusionClient->UpdateFrameBegin(delta);
        sync_inbound();
    }
}

// 6. Shutdown
fusionClient->Shutdown();
co_await realtimeClient->Disconnect();
```

## Related

- [Frame Loop](frame-loop.md) -- The mandatory 3-step frame sequence
- [Architecture](architecture.md) -- Layered design and namespaces
- [Objects](objects.md) -- Object creation after `OnFusionStart`
- [Scene Management](scene-management.md) -- Scene loading on connect
