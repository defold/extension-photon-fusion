# Realtime API

`PhotonMatchmaking::RealtimeClient` -- the Photon Realtime networking layer. Handles connection, matchmaking, room management, messaging, and player state.

Header: `RealtimeClient.h`

---

## ClientConstructOptions

```cpp
struct ClientConstructOptions {
    PhotonCommon::StringType appId;                             // Photon App ID
    PhotonCommon::StringType appVersion;                        // Application version string
    ConnectionProtocol protocol = ConnectionProtocol::Default;  // Transport protocol
    bool useAlternativePorts = false;                           // Use alternative server ports
    RegionSelectionMode regionSelectionMode = RegionSelectionMode::Default;  // Region selection strategy
    bool autoLobbyStats = false;                                // Auto-request lobby statistics
    SerializationProtocol serialization = SerializationProtocol::Protocol1_8;  // Wire format
    std::optional<int> disconnectTimeoutMs;                     // Override disconnect timeout
    std::optional<int> pingIntervalMs;                          // Override ping interval
    std::optional<bool> enableCrc;                              // Enable CRC checks
    std::optional<int> sentCountAllowance;                      // Resend allowance
    std::optional<uint8_t> quickResendAttempts;                 // Quick resend count
    std::optional<int> limitOfUnreliableCommands;               // Cap on unreliable command queue
};
```

## ConnectOptions

```cpp
struct ConnectOptions {
    AuthenticationValues auth;                       // Authentication credentials
    PhotonCommon::StringType username;               // Display name
    ServerType serverType = ServerType::NameServer;  // Initial server to connect to
    PhotonCommon::StringType serverAddress;           // Custom server address (overrides default)
    bool tryUseDatagramEncryption = false;           // Attempt datagram encryption
    bool useBackgroundSendReceiveThread = true;      // Run network I/O on a background thread
};
```

## AuthenticationValues

```cpp
struct AuthenticationValues {
    PhotonCommon::StringType userId;                // Unique user identifier
    CustomAuthenticationType type = CustomAuthenticationType::None;  // Auth provider type
    PhotonCommon::StringType parameters;            // Auth query parameters
    std::variant<std::monostate, std::vector<uint8_t>, PhotonCommon::StringType, PropertyMap> data;  // Auth data payload
};
```

---

## Construction

```cpp
explicit RealtimeClient(const ClientConstructOptions& options);
~RealtimeClient();
```

Non-copyable, non-movable.

---

## Service Loop

```cpp
void Service(bool dispatchIncomingCommands = true);
void ServiceBasic();
bool SendOutgoingCommands();
bool SendAcksOnly();
bool DispatchIncomingCommands();
```

| Method | Description |
|--------|-------------|
| `Service(dispatch)` | Full service tick: send + optionally dispatch incoming. Call once per frame. |
| `ServiceBasic()` | Minimal service tick (acks and keep-alive only). |
| `SendOutgoingCommands()` | Flush the outgoing command queue. Returns `true` if data was sent. |
| `SendAcksOnly()` | Send only acknowledgment packets. |
| `DispatchIncomingCommands()` | Process and dispatch all queued incoming commands. |

---

## Connection

```cpp
Task<Result<void>> Connect();
Task<Result<void>> Connect(const ConnectOptions& options);
Task<Result<void>> Disconnect();
Task<Result<void>> Reconnect();
```

| Method | Description |
|--------|-------------|
| `Connect()` | Connect to Photon using default options. |
| `Connect(options)` | Connect with explicit authentication and server options. |
| `Disconnect()` | Gracefully disconnect from the server. |
| `Reconnect()` | Reconnect to the last server after a disconnect. |

## Connection State

```cpp
ConnectionState GetState() const noexcept;
bool IsConnected() const noexcept;
bool IsInRoom() const noexcept;
bool IsInLobby() const noexcept;
DisconnectCause GetDisconnectCause() const;
```

| Method | Description |
|--------|-------------|
| `GetState()` | Returns the current `ConnectionState` enum value. |
| `IsConnected()` | Returns `true` if connected to any Photon server. |
| `IsInRoom()` | Returns `true` if currently in a room. |
| `IsInLobby()` | Returns `true` if currently in a lobby. |
| `GetDisconnectCause()` | Returns the reason for the last disconnect. |

---

## Region Selection

```cpp
Task<Result<std::vector<RegionInfo>>> AvailableRegions();
Task<Result<void>> SelectRegion(PhotonCommon::StringViewType region);
PhotonCommon::StringType GetBestRegion() const;
```

| Method | Description |
|--------|-------------|
| `AvailableRegions()` | Fetch the list of available regions with ping results. |
| `SelectRegion(region)` | Select a specific region by code (e.g., `"us"`, `"eu"`). |
| `GetBestRegion()` | Returns the region code with the lowest ping. |

### RegionInfo

```cpp
struct RegionInfo {
    PhotonCommon::StringType code;    // Region code (e.g., "us", "eu", "asia")
    PhotonCommon::StringType server;  // Server address
    int pingMs = -1;                  // Measured ping in milliseconds (-1 = not pinged)
};
```

---

## Lobby

```cpp
Task<Result<void>> JoinLobby(PhotonCommon::StringViewType name = {}, LobbyType type = LobbyType::Default);
Task<Result<void>> LeaveLobby();
Task<Result<std::vector<LobbyStats>>> GetLobbyStats();
```

| Method | Description |
|--------|-------------|
| `JoinLobby(name, type)` | Join a named lobby. Empty name joins the default lobby. |
| `LeaveLobby()` | Leave the current lobby. |
| `GetLobbyStats()` | Fetch statistics for all lobbies. |

### LobbyStats

```cpp
struct LobbyStats {
    PhotonCommon::StringType name;               // Lobby name
    LobbyType type = LobbyType::Default;         // Lobby type
    int peerCount = 0;                           // Connected players
    int roomCount = 0;                           // Active rooms
};
```

---

## Room Operations

All room operations return `Task<Result<MutableRoomView>>` (or `Task<Result<void>>` for leave).

```cpp
Task<Result<MutableRoomView>> CreateRoom(PhotonCommon::StringViewType name = {},
    const CreateRoomOptions& createOptions = {});

Task<Result<MutableRoomView>> JoinRoom(PhotonCommon::StringViewType name,
    const JoinRoomOptions& joinOptions = {});

Task<Result<MutableRoomView>> JoinOrCreateRoom(PhotonCommon::StringViewType name,
    const CreateRoomOptions& createOptions = {},
    const JoinRoomOptions& joinOptions = {});

Task<Result<MutableRoomView>> JoinRandomRoom(
    const MatchmakingOptions& matchmakingOptions = {});

Task<Result<MutableRoomView>> JoinRandomOrCreateRoom(
    const CreateRoomOptions& createOptions = {},
    const MatchmakingOptions& matchmakingOptions = {});

Task<Result<void>> LeaveRoom(bool willComeBack = false, bool sendAuthCookie = false);

std::optional<MutableRoomView> GetCurrentRoom() const;
```

| Method | Description |
|--------|-------------|
| `CreateRoom(name, options)` | Create a new room. Empty name generates a unique ID. |
| `JoinRoom(name, options)` | Join an existing room by name. |
| `JoinOrCreateRoom(name, ...)` | Join a room or create it if it does not exist. |
| `JoinRandomRoom(options)` | Join a random room matching the filter criteria. |
| `JoinRandomOrCreateRoom(...)` | Join a random room or create one if no match is found. |
| `LeaveRoom(willComeBack, sendAuthCookie)` | Leave the current room. Set `willComeBack` for rejoin support. |
| `GetCurrentRoom()` | Returns the current room view, or `nullopt` if not in a room. |

### Room List

```cpp
Task<Result<std::vector<RoomListing>>> GetRoomList(
    PhotonCommon::StringViewType lobby, PhotonCommon::StringViewType sqlFilter);
const std::vector<RoomListing>& GetCachedRoomList() const;
```

### RoomListing

```cpp
struct RoomListing {
    PhotonCommon::StringType name;    // Room name
    int playerCount = 0;              // Current players
    uint8_t maxPlayers = 0;           // Max capacity
    bool isOpen = false;              // Joinable flag
    DirectMode directMode = DirectMode::None;  // Direct connection mode
    PropertyMap customProperties;     // Custom room properties
};
```

---

## CreateRoomOptions

```cpp
struct CreateRoomOptions {
    bool isVisible = true;                                  // Listed in lobby
    bool isOpen = true;                                     // Joinable
    uint8_t maxPlayers = 0;                                 // Max capacity (0 = unlimited)
    PropertyMap customProperties;                           // Custom properties
    std::vector<PhotonCommon::StringType> lobbyProperties;  // Keys visible in lobby listings
    PhotonCommon::StringType lobbyName;                     // Target lobby name
    LobbyType lobbyType = LobbyType::Default;               // Target lobby type
    int playerTtlMs = 0;                                    // Player reconnect window (ms)
    int emptyRoomTtlMs = 0;                                 // Empty room lifetime (ms)
    bool suppressRoomEvents = false;                        // Suppress join/leave events
    bool publishUserId = false;                             // Share user IDs with other players
    DirectMode directMode = DirectMode::None;               // Direct connection mode
    std::vector<PhotonCommon::StringType> plugins;          // Server plugins
    std::vector<PhotonCommon::StringType> expectedUsers;    // Slot reservation user IDs
};
```

## JoinRoomOptions

```cpp
struct JoinRoomOptions {
    bool rejoin = false;                                    // Rejoin a room left with willComeBack
    int cacheSliceIndex = 0;                                // Event cache slice to start from
    std::vector<PhotonCommon::StringType> expectedUsers;    // Slot reservation user IDs
};
```

## MatchmakingOptions

```cpp
struct MatchmakingOptions {
    PropertyMap filter;                                     // Property filter for matching
    uint8_t maxPlayers = 0;                                 // Max players filter
    MatchmakingMode mode = MatchmakingMode::FillRoom;       // Matchmaking strategy
    PhotonCommon::StringType lobbyName;                     // Target lobby name
    LobbyType lobbyType = LobbyType::Default;               // Target lobby type
    PhotonCommon::StringType sqlFilter;                     // SQL-style filter expression
    std::vector<PhotonCommon::StringType> expectedUsers;    // Slot reservation user IDs
};
```

---

## MutableRoomView

Read-write view of the current room. Returned by room join/create operations.

### Read Access

```cpp
const PhotonCommon::StringType& GetName() const noexcept;
int GetPlayerCount() const noexcept;
uint8_t GetMaxPlayers() const noexcept;
bool IsOpen() const noexcept;
bool IsVisible() const noexcept;
const PropertyMap& GetCustomProperties() const noexcept;
const std::vector<PlayerView>& GetPlayers() const noexcept;
int GetMasterClientId() const noexcept;
bool IsMasterClient() const noexcept;
bool IsMasterClient(int localPlayerNumber) const noexcept;
int GetPlayerTtlMs() const noexcept;
int GetEmptyRoomTtlMs() const noexcept;
bool GetPublishUserId() const noexcept;
DirectMode GetDirectMode() const noexcept;
const std::vector<PhotonCommon::StringType>& GetExpectedUsers() const noexcept;
const std::vector<PhotonCommon::StringType>& GetLobbyProperties() const noexcept;
bool GetSuppressRoomEvents() const noexcept;
const std::vector<PhotonCommon::StringType>& GetPlugins() const noexcept;
const PhotonCommon::StringType& GetLobbyName() const noexcept;
LobbyType GetLobbyType() const noexcept;
```

### Mutations

```cpp
bool SetOpen(bool isOpen, const WebFlags& webFlags = {});
bool SetVisible(bool isVisible, const WebFlags& webFlags = {});
bool SetMaxPlayers(uint8_t maxPlayers, const WebFlags& webFlags = {});
bool SetProperties(const PropertyMap& properties, const WebFlags& webFlags = {});
bool SetProperties(const PropertyMap& properties, const PropertyMap& expected, const WebFlags& webFlags = {});
bool RemoveProperties(const std::vector<PhotonCommon::StringType>& keys, const WebFlags& webFlags = {});
bool SetLobbyProperties(const std::vector<PhotonCommon::StringType>& props);
bool SetExpectedUsers(const std::vector<PhotonCommon::StringType>& userIds);
bool SetMasterClient(int playerNumber);

template<typename T>
bool SetProperty(PhotonCommon::StringViewType key, const T& value);
```

| Method | Description |
|--------|-------------|
| `SetOpen(isOpen)` | Set whether the room is joinable. |
| `SetVisible(isVisible)` | Set whether the room appears in lobby listings. |
| `SetMaxPlayers(maxPlayers)` | Set the maximum player capacity. |
| `SetProperties(props)` | Set custom room properties. |
| `SetProperties(props, expected)` | Set properties with CAS (compare-and-swap) check. |
| `RemoveProperties(keys)` | Remove custom properties by key. |
| `SetLobbyProperties(props)` | Set which property keys are visible in lobby listings. |
| `SetExpectedUsers(userIds)` | Reserve slots for specific users. |
| `SetMasterClient(playerNumber)` | Transfer master client role to another player. |
| `SetProperty(key, value)` | Set a single typed property (convenience template). |

---

## Player Management

```cpp
PlayerView GetLocalPlayer() const;
void SetPlayerName(PhotonCommon::StringViewType name, const WebFlags& webFlags = {});
bool SetPlayerProperties(const PropertyMap& properties, const WebFlags& webFlags = {});

template<typename T>
bool SetPlayerProperty(PhotonCommon::StringViewType key, const T& value);

bool RemovePlayerProperties(const std::vector<PhotonCommon::StringType>& keys, const WebFlags& webFlags = {});

PhotonCommon::StringType GetUserId() const;
```

| Method | Description |
|--------|-------------|
| `GetLocalPlayer()` | Returns the local player's `PlayerView`. |
| `SetPlayerName(name)` | Set the local player's display name. |
| `SetPlayerProperties(props)` | Set custom player properties. |
| `SetPlayerProperty(key, value)` | Set a single typed player property (convenience template). |
| `RemovePlayerProperties(keys)` | Remove player properties by key. |
| `GetUserId()` | Returns the authenticated user ID. |

### PlayerView

```cpp
struct PlayerView {
    int number = 0;                        // Actor number in the room
    PhotonCommon::StringType name;         // Display name
    PhotonCommon::StringType userId;       // Authenticated user ID
    PropertyMap customProperties;          // Custom player properties
    bool isInactive = false;               // True if the player left but may rejoin
    bool isMasterClient = false;           // True if this player is the master client
};
```

---

## Messaging

### SendEvent

```cpp
bool SendEvent(uint8_t eventCode, std::span<const uint8_t> data, const EventOptions& options = {});

template<typename T>
requires std::is_trivially_copyable_v<T>
bool SendEvent(uint8_t eventCode, const T& data, const EventOptions& options = {});
```

Send an event to players in the room. The template overload serializes any trivially-copyable struct as raw bytes.

### SendDirect

```cpp
int SendDirect(std::span<const uint8_t> data, const DirectMessageOptions& options = {});

template<typename T>
requires std::is_trivially_copyable_v<T>
int SendDirect(const T& data, const DirectMessageOptions& options = {});
```

Send a direct (peer-to-peer) message. Returns the number of targets reached.

### ChangeGroups

```cpp
bool ChangeGroups(const std::vector<uint8_t>& remove, const std::vector<uint8_t>& add);
```

Subscribe/unsubscribe from interest groups for event filtering.

### EventOptions

```cpp
struct EventOptions {
    bool reliable = true;                               // Use reliable delivery
    uint8_t channel = 0;                                // Channel number
    ReceiverGroup receiverGroup = ReceiverGroup::Others; // Target group
    std::vector<int> targetPlayers;                      // Specific player numbers (overrides receiverGroup)
    uint8_t interestGroup = 0;                          // Interest group filter
    EventCache caching = EventCache::DoNotCache;        // Event caching mode
    bool encrypt = false;                               // Encrypt the payload
    int cacheSliceIndex = 0;                            // Cache slice index
    WebFlags webFlags;                                  // Webhook flags
};
```

### DirectMessageOptions

```cpp
struct DirectMessageOptions {
    std::vector<int> targetPlayers;                       // Specific targets
    ReceiverGroup receiverGroup = ReceiverGroup::Others;  // Target group
    bool fallbackRelay = false;                           // Relay via server if direct fails
};
```

---

## Friends

```cpp
Task<Result<std::vector<FriendInfo>>> FindFriends(const std::vector<PhotonCommon::StringType>& userIds);
const std::vector<FriendInfo>& GetFriendList() const;
int GetFriendListAge() const;
```

### FriendInfo

```cpp
struct FriendInfo {
    PhotonCommon::StringType userId;  // User identifier
    bool isOnline = false;            // Currently connected
    PhotonCommon::StringType roomName; // Room name (if in a room)
    bool isInRoom = false;            // Currently in a room
};
```

---

## WebRPC

```cpp
Task<Result<WebRpcResponse>> WebRpc(PhotonCommon::StringViewType uriPath);
Task<Result<WebRpcResponse>> WebRpc(PhotonCommon::StringViewType uriPath,
    const PropertyMap& parameters, bool sendAuthCookie = false);
```

### WebRpcResponse

```cpp
struct WebRpcResponse {
    int resultCode = 0;                    // Server-returned result code
    PhotonCommon::StringType errorString;  // Error message (if any)
    PhotonCommon::StringType uriPath;      // Request URI path
    PropertyMap returnData;                // Response data
};
```

---

## Custom Operations

```cpp
bool SendCustomOperation(uint8_t operationCode, const PropertyMap& params,
    bool reliable = true, uint8_t channel = 0, bool encrypt = false);
bool SendCustomAuthData(const AuthenticationValues& auth);
```

| Method | Description |
|--------|-------------|
| `SendCustomOperation(...)` | Send a custom operation to the server. |
| `SendCustomAuthData(auth)` | Send additional authentication data after connect. |

---

## Stats and Configuration

```cpp
NetworkStats GetStats() const;
int GetServerTime() const;
void FetchServerTimestamp();
PhotonCommon::StringType GetMasterServerAddress() const;

void SetAutoJoinLobby(bool enabled);

void SetDisconnectTimeout(int ms);
int GetDisconnectTimeout() const;
void SetPingInterval(int ms);
int GetPingInterval() const;
void SetSentCountAllowance(int count);
int GetSentCountAllowance() const;
void SetQuickResendAttempts(uint8_t attempts);
uint8_t GetQuickResendAttempts() const;
void SetCrcEnabled(bool enabled);
bool GetCrcEnabled() const;
void SetLimitOfUnreliableCommands(int limit);
int GetLimitOfUnreliableCommands() const;
```

### Traffic Stats

```cpp
void SetTrafficStatsEnabled(bool enabled);
void ResetTrafficStats();
PhotonCommon::StringType GetVitalStatsToString(bool all = false) const;
TrafficStats GetTrafficStatsIncoming() const;
TrafficStats GetTrafficStatsOutgoing() const;
TrafficStatsGameLevel GetTrafficStatsGameLevel() const;
void ResetTrafficStatsMaximumCounters();
```

### NetworkStats

```cpp
struct NetworkStats {
    int roundTripTimeMs = 0;             // Current RTT
    int rttVarianceMs = 0;               // RTT variance
    int bytesIn = 0;                     // Total bytes received
    int bytesOut = 0;                    // Total bytes sent
    int bytesCurrentDispatch = 0;        // Bytes in current dispatch
    int bytesLastOperation = 0;          // Bytes of last operation
    int queuedIncomingCommands = 0;      // Pending incoming commands
    int queuedOutgoingCommands = 0;      // Pending outgoing commands
    int incomingReliableCommands = 0;    // Reliable commands received
    int resentReliableCommands = 0;      // Resent reliable commands
    int playersInGame = 0;               // Players in current game
    int gamesRunning = 0;               // Games running on server
    int playersOnline = 0;               // Total players online
    int serverTimeOffsetMs = 0;          // Clock offset from server
    int serverTimeMs = 0;               // Server timestamp
    int timestampLastReceive = 0;        // Timestamp of last receive
    int packetLossByCrc = 0;             // Packets dropped by CRC
    int sentCountAllowance = 0;          // Current resend allowance
    bool encryptionAvailable = false;    // Encryption supported
    bool payloadEncryptionAvailable = false;  // Payload encryption available
    short peerId = 0;                    // Peer identifier
    int disconnectTimeoutMs = 0;         // Current disconnect timeout
    int pingIntervalMs = 0;              // Current ping interval
    int trafficStatsElapsedMs = 0;       // Time since stats reset
    PhotonCommon::StringType masterServerAddress;  // Master server address
    int channelCountUserChannels = 0;    // User channel count
    uint8_t serializationProtocol = 0;   // Active serialization protocol
    short peerCount = 0;                 // Connected peer count
};
```

### TrafficStats

```cpp
struct TrafficStats {
    int packageHeaderSize = 0;           // Header overhead per packet
    int reliableCommandCount = 0;        // Reliable commands sent/received
    int unreliableCommandCount = 0;      // Unreliable commands sent/received
    int fragmentCommandCount = 0;        // Fragment commands sent/received
    int controlCommandCount = 0;         // Control commands sent/received
    int totalPacketCount = 0;            // Total packets
    int totalCommandsInPackets = 0;      // Total commands across all packets
    int reliableCommandBytes = 0;        // Bytes in reliable commands
    int unreliableCommandBytes = 0;      // Bytes in unreliable commands
    int fragmentCommandBytes = 0;        // Bytes in fragment commands
    int controlCommandBytes = 0;         // Bytes in control commands
    int totalCommandCount = 0;           // Total command count
    int totalCommandBytes = 0;           // Total command bytes
    int totalPacketBytes = 0;            // Total packet bytes
    int timestampOfLastAck = 0;          // Timestamp of last ack sent
    int timestampOfLastReliableCommand = 0;  // Timestamp of last reliable command
};
```

### TrafficStatsGameLevel

```cpp
struct TrafficStatsGameLevel {
    int operationByteCount = 0;          // Bytes sent in operations
    int operationCount = 0;              // Operations sent
    int resultByteCount = 0;             // Bytes received in results
    int resultCount = 0;                 // Results received
    int eventByteCount = 0;              // Bytes received in events
    int eventCount = 0;                  // Events received
    int longestOpResponseCallbackMs = 0; // Longest operation response callback
    uint8_t longestOpResponseCallbackOpCode = 0;  // OpCode of longest callback
    int longestEventCallbackMs = 0;      // Longest event callback
    uint8_t longestEventCallbackCode = 0; // Code of longest event callback
    int longestDeltaBetweenDispatchingMs = 0;  // Max gap between dispatches
    int longestDeltaBetweenSendingMs = 0;      // Max gap between sends
    int dispatchIncomingCommandsCalls = 0;     // Dispatch call count
    int sendOutgoingCommandsCalls = 0;         // Send call count
    int totalByteCount = 0;              // Total bytes (in + out)
    int totalMessageCount = 0;           // Total messages (in + out)
    int totalIncomingByteCount = 0;      // Total incoming bytes
    int totalIncomingMessageCount = 0;   // Total incoming messages
    int totalOutgoingByteCount = 0;      // Total outgoing bytes
    int totalOutgoingMessageCount = 0;   // Total outgoing messages
};
```

---

## Broadcasters

All broadcasters use `PhotonCommon::Broadcaster<Signature>`. Subscribe with `.Subscribe(callback)`.

```cpp
PhotonCommon::Broadcaster<void(DisconnectCause)> OnDisconnected;
```
Fired when disconnected from the server.

```cpp
PhotonCommon::Broadcaster<void(ErrorCode, PhotonCommon::StringViewType)> OnError;
```
Fired on errors with an error code and message.

```cpp
PhotonCommon::Broadcaster<void(const PropertyMap&)> OnRoomPropertiesChanged;
```
Fired when room properties are updated.

```cpp
PhotonCommon::Broadcaster<void(const std::vector<RoomListing>&)> OnRoomListUpdated;
```
Fired when the lobby room list is updated.

```cpp
PhotonCommon::Broadcaster<void(int newId, int oldId)> OnMasterClientChanged;
```
Fired when the master client changes.

```cpp
PhotonCommon::Broadcaster<void(const PlayerView&)> OnPlayerJoined;
```
Fired when a player joins the room.

```cpp
PhotonCommon::Broadcaster<void(int playerNumber, bool isInactive)> OnPlayerLeft;
```
Fired when a player leaves. `isInactive` is true if the player may rejoin.

```cpp
PhotonCommon::Broadcaster<void(int playerNumber, const PropertyMap&)> OnPlayerPropertiesChanged;
```
Fired when a player's custom properties change.

```cpp
PhotonCommon::Broadcaster<void(uint8_t eventCode, int senderId, std::span<const uint8_t> data)> OnEvent;
```
Fired when a custom event is received.

```cpp
PhotonCommon::Broadcaster<void(int senderId, std::span<const uint8_t> data, bool isRelay)> OnDirectMessage;
```
Fired when a direct message is received. `isRelay` is true if it was relayed via the server.

```cpp
PhotonCommon::Broadcaster<void(const std::vector<LobbyStats>&)> OnLobbyStats;
```
Fired when lobby statistics are updated.

```cpp
PhotonCommon::Broadcaster<void(const std::unordered_map<PhotonCommon::StringType, PhotonCommon::StringType>&)> OnCustomAuthStep;
```
Fired during custom authentication with additional data from the auth provider.

```cpp
PhotonCommon::Broadcaster<void()> OnAppStatsUpdated;
```
Fired when application-level statistics are updated.

```cpp
PhotonCommon::Broadcaster<void(int warningCode)> OnWarning;
```
Fired on non-fatal warnings.

```cpp
PhotonCommon::Broadcaster<void()> OnPropertiesChangeFailed;
```
Fired when a CAS property update fails.

```cpp
PhotonCommon::Broadcaster<void(int cacheSliceIndex)> OnCacheSliceChanged;
```
Fired when the event cache slice changes.

```cpp
PhotonCommon::Broadcaster<void(int remotePlayerId)> OnDirectConnectionEstablished;
```
Fired when a direct peer-to-peer connection is established.

```cpp
PhotonCommon::Broadcaster<void(int remotePlayerId)> OnDirectConnectionFailed;
```
Fired when a direct peer-to-peer connection attempt fails.

```cpp
PhotonCommon::Broadcaster<void(uint8_t opCode, int errorCode, PhotonCommon::StringViewType errorString, const PropertyMap& data)> OnCustomOperationResponse;
```
Fired when a custom operation response is received.

```cpp
PhotonCommon::Broadcaster<void()> OnRoomJoined;
```
Fired when the local player successfully joins a room.

```cpp
PhotonCommon::Broadcaster<void()> OnRoomLeft;
```
Fired when the local player leaves a room.

---

## Enums

### ConnectionState

```cpp
enum class ConnectionState : uint8_t {
    Disconnected,    // Not connected
    Connecting,      // Connection in progress
    Connected,       // Connected to server (not in room)
    JoiningRoom,     // Room join in progress
    InRoom,          // In a room
    LeavingRoom,     // Room leave in progress
    Disconnecting    // Disconnect in progress
};
```

### DisconnectCause

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

### ErrorCode

```cpp
enum class ErrorCode : int {
    Ok = 0,                      // Success
    Unknown = -1,                // Unknown error

    // Connection errors
    ConnectionFailed = 1,
    Disconnected = 2,
    Timeout = 3,
    ServerFull = 4,

    // Authentication errors
    InvalidAuthentication = 10,
    CustomAuthenticationFailed = 11,
    AuthenticationTicketExpired = 12,
    MaxCCUReached = 13,
    InvalidRegion = 14,
    ClientVersionTooOld = 15,

    // Room errors
    RoomFull = 20,
    RoomClosed = 21,
    RoomNotFound = 22,
    RoomAlreadyExists = 23,
    NoMatchFound = 24,
    AlreadyJoined = 25,
    SlotError = 26,

    // Operation errors
    OperationDenied = 30,
    OperationInvalid = 31,
    OperationLimitReached = 32,
    RateLimited = 33,

    // Server errors
    InternalServerError = 40,
    PluginError = 41,
    ServerDisconnect = 42,

    // State errors
    NotConnected = 50,
    NotInRoom = 51,
    InvalidState = 52
};
```

---

## PropertyValue and PropertyMap

```cpp
using PropertyValue = std::variant<
    bool, int8_t, int16_t, int32_t, int64_t,
    float, double,
    PhotonCommon::StringType,
    std::vector<uint8_t>,
    std::vector<int32_t>,
    std::vector<float>,
    std::vector<PhotonCommon::StringType>
>;

using PropertyMap = std::unordered_map<PhotonCommon::StringType, PropertyValue>;
```

## WebFlags

```cpp
struct WebFlags {
    bool httpForward = false;    // Forward to webhook
    bool sendAuthCookie = false; // Include auth cookie
    bool sendSync = false;       // Synchronous webhook call
    bool sendState = false;      // Include room state
};
```

---

## Result\<T\> and Task\<T\>

### Result\<T\>

Monadic result type wrapping either a success value or an `Error`.

```cpp
static Result Ok(T value);
static Result Err(ErrorCode code, PhotonCommon::StringType message = {});
static Result Err(Error error);

bool IsOk() const noexcept;
bool IsErr() const noexcept;
explicit operator bool() const noexcept;

const T& GetValue() const&;
T& GetValue() &;
T&& GetValue() &&;
const T* operator->() const;

const Error& GetError() const&;
Error&& GetError() &&;
ErrorCode GetErrorCode() const;

T ValueOr(T default_value) const&;
T ValueOr(T default_value) &&;

// Monadic combinators
auto Transform(F&& f) -> Result<U>;      // Map the success value
auto AndThen(F&& f) -> Result<U>;        // Chain a fallible operation
auto OrElse(F&& f) -> Result<T>;         // Recover from an error
auto TransformError(F&& f) -> Result<T>; // Map the error
```

### Result\<void\>

Specialization for operations with no return value. Same API minus `GetValue()` / `ValueOr()`.

### Task\<T\>

C++20 coroutine-based async task. Returned by all async `RealtimeClient` methods.

```cpp
bool IsReady() const noexcept;  // True when the coroutine has completed
T Get();                        // Block and retrieve the result (rethrows exceptions)

// co_await support
bool await_ready() const noexcept;
void await_suspend(std::coroutine_handle<> caller) noexcept;
T await_resume();
```

Move-only. Non-copyable.

---

## Supporting Enums

### ReceiverGroup

```cpp
enum class ReceiverGroup : uint8_t { Others = 0, All = 1, MasterClient = 2 };
```

### EventCache

```cpp
enum class EventCache : uint8_t {
    DoNotCache = 0, MergeCache = 1, ReplaceCache = 2, RemoveCache = 3,
    AddToRoomCache = 4, AddToRoomCacheGlobal = 5, RemoveFromRoomCache = 6,
    RemoveFromRoomCacheForActorsLeft = 7, SliceIncIndex = 10, SliceSetIndex = 11,
    SlicePurgeIndex = 12, SlicePurgeUpToIndex = 13
};
```

### MatchmakingMode

```cpp
enum class MatchmakingMode : uint8_t { FillRoom = 0, SerialMatching = 1, RandomMatching = 2 };
```

### LobbyType

```cpp
enum class LobbyType : uint8_t { Default = 0, SqlLobby = 2, AsyncRandomLobby = 3 };
```

### DirectMode

```cpp
enum class DirectMode : uint8_t { None = 0, AllToOthers = 1, MasterToOthers = 2, AllToAll = 3, MasterToAll = 4 };
```

### CustomAuthenticationType

```cpp
enum class CustomAuthenticationType : uint8_t {
    None = 255, Custom = 0, Steam = 1, Facebook = 2, Oculus = 3,
    PlayStation4 = 4, Xbox = 5, Viveport = 10, NintendoSwitch = 11,
    PlayStation5 = 12, Epic = 13, FacebookGaming = 15
};
```
