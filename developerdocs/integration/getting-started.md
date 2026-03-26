# Getting Started: Minimal Integration

This guide walks through building a minimal Fusion 3 SDK integration from scratch. By the end you will have a working skeleton that connects to Photon Cloud, joins a room, runs the frame loop, creates a networked object, and synchronizes properties.

## Prerequisites

| Item | Notes |
|------|-------|
| Fusion SDK headers | `Client.h`, `Types.h`, `RealtimeClient.h`, `Buffers.h`, `LogOutput.h`, `LogUtils.h`, `Misc.h`, `Aliases.h`, `StringHeap.h`, `Task.h`, `Result.h` |
| Fusion static library | Platform-specific: `fusion_windows_x64_release.lib`, `libfusion_linux_x64_release.a`, etc. |
| Photon Realtime SDK | Included with Fusion in `deps/realtime/` |
| CRT linkage | **Dynamic CRT** (`/MD` on MSVC). The Fusion library is compiled with `/MD`. Mixing `/MT` and `/MD` causes heap corruption at runtime. |
| C++ standard | C++20 required (coroutines for `Task<>`, `std::span`, `char8_t`) |

## Header Inclusion Order

The Fusion SDK pulls in Photon headers that define a `Dictionary` type. This collides with identically-named types in many engines (Godot, Unreal). To avoid ambiguity, **always include Fusion headers before engine headers**:

```cpp
// === CORRECT: Fusion first ===
#include "Client.h"
#include "LogOutput.h"
#include "LogUtils.h"

// Engine headers after
#include <my_engine/node.hpp>
// #include "CoreMinimal.h"  // Unreal example
```

If you reverse this order, you will get compile errors about ambiguous `Dictionary` references. This applies to every `.cpp` file that uses both Fusion and engine types.

## Step 1: Set Up Logging

Before creating any SDK objects, wire up logging so you can see what the SDK is doing. Implement the `PhotonCommon::LogOutput` interface:

```cpp
#include "LogOutput.h"
#include "LogUtils.h"

class MyLogOutput : public PhotonCommon::LogOutput {
public:
    void LogTrace(const PhotonCommon::CharType* message) override {
        printf("[Fusion TRACE] %s\n", reinterpret_cast<const char*>(message));
    }
    void LogDebug(const PhotonCommon::CharType* message) override {
        printf("[Fusion DEBUG] %s\n", reinterpret_cast<const char*>(message));
    }
    void LogInfo(const PhotonCommon::CharType* message) override {
        printf("[Fusion INFO] %s\n", reinterpret_cast<const char*>(message));
    }
    void LogWarning(const PhotonCommon::CharType* message) override {
        printf("[Fusion WARN] %s\n", reinterpret_cast<const char*>(message));
    }
    void LogError(const PhotonCommon::CharType* message) override {
        printf("[Fusion ERROR] %s\n", reinterpret_cast<const char*>(message));
    }
};
```

Register it and configure log levels:

```cpp
static MyLogOutput* g_log_output = nullptr;

void InitLogging() {
    g_log_output = new MyLogOutput();
    PhotonCommon::AddLogOutput(g_log_output);

    // Enable desired log levels via bitmask
    // Trace=1, Debug=2, Info=4, Warning=8, Error=16
    PhotonCommon::SetLogLevelsFromBitmask(
        PhotonCommon::Info |
        PhotonCommon::Warning |
        PhotonCommon::Error
    );
}
```

**Logging API reference:**

| Function | Signature |
|----------|-----------|
| `AddLogOutput` | `void AddLogOutput(LogOutput* logOutput)` |
| `RemoveLogOutput` | `bool RemoveLogOutput(LogOutput* logOutput)` |
| `SetLogLevelsFromBitmask` | `void SetLogLevelsFromBitmask(uint8_t logLevelMask)` |
| `LogEnable` | `void LogEnable(LogLevel logLevel)` |
| `LogDisable` | `void LogDisable(LogLevel logLevel)` |
| `IsLogEnabled` | `bool IsLogEnabled(LogLevel logLevel)` |

All logging functions are in the `PhotonCommon` namespace and declared in `LogUtils.h`.

## Step 2: Construct RealtimeClient and Fusion Client

The new SDK uses a two-layer construction: first create a `PhotonMatchmaking::RealtimeClient` for transport, then pass it to `SharedMode::Client` for Fusion state sync.

```cpp
#include "Client.h"
#include "RealtimeClient.h"
#include "ClientConstructOptions.h"

static PhotonMatchmaking::RealtimeClient* g_realtime = nullptr;
static SharedMode::Client* g_client = nullptr;

void InitClient(const char* appId, const char* appVersion) {
    InitLogging();

    // 1. Configure the RealtimeClient
    PhotonMatchmaking::ClientConstructOptions options;
    options.appId = reinterpret_cast<const PhotonCommon::CharType*>(appId);
    options.appVersion = reinterpret_cast<const PhotonCommon::CharType*>(appVersion);

    // 2. Create the RealtimeClient (transport layer)
    g_realtime = new PhotonMatchmaking::RealtimeClient(options);

    // 3. Create the Fusion Client (state sync layer)
    g_client = new SharedMode::Client(*g_realtime);
}
```

**`ClientConstructOptions` fields:**

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `appId` | `StringType` | (required) | Your Photon App ID |
| `appVersion` | `StringType` | `""` | Client version for matchmaking isolation |
| `protocol` | `ConnectionProtocol` | `Default` | Transport protocol |
| `regionSelectionMode` | `RegionSelectionMode` | `Default` | How regions are selected |
| `disconnectTimeoutMs` | `optional<int>` | none | Override disconnect timeout |

## Step 3: Subscribe to Broadcasters

The `Client` exposes `PhotonCommon::Broadcaster<>` members for all major events. Subscribe using a `SubscriptionBag` to manage subscription lifetimes:

```cpp
#include "SubscriptionBag.h"

static PhotonCommon::SubscriptionBag g_subscriptions;

void SetupCallbacks() {
    // Room is ready for Fusion state sync
    g_subscriptions += g_client->OnFusionStart.Subscribe([]() {
        printf("Fusion started! Room is ready.\n");
    });

    // Remote object is ready (fully created and has valid data)
    g_subscriptions += g_client->OnObjectReady.Subscribe(
        [](SharedMode::ObjectRoot* obj) {
            printf("Object ready: origin=%u counter=%u\n",
                   obj->Id.Origin, obj->Id.Counter);
            // Instantiate your engine-side representation here
        }
    );

    // Object destroyed (local or remote)
    g_subscriptions += g_client->OnObjectDestroyed.Subscribe(
        [](const SharedMode::ObjectRoot* obj, SharedMode::DestroyModes mode) {
            printf("Object destroyed: mode=%d\n", static_cast<int>(mode));
            // Clean up engine-side representation
        }
    );

    // Sub-object created by remote client
    g_subscriptions += g_client->OnSubObjectCreated.Subscribe(
        [](SharedMode::ObjectChild* child) {
            printf("Sub-object created under parent: origin=%u counter=%u\n",
                   child->Parent.Origin, child->Parent.Counter);
        }
    );

    // Ownership changed
    g_subscriptions += g_client->OnObjectOwnerChanged.Subscribe(
        [](SharedMode::ObjectRoot* obj) {
            printf("Owner changed: new owner=%u\n", obj->Owner);
        }
    );

    // Ownership was given to us by the server
    g_subscriptions += g_client->OnOwnerWasGiven.Subscribe(
        [](SharedMode::ObjectRoot* obj) {
            printf("We were given ownership of object: origin=%u counter=%u\n",
                   obj->Id.Origin, obj->Id.Counter);
        }
    );

    // RPC received
    g_subscriptions += g_client->OnRpc.Subscribe(
        [](SharedMode::Rpc& rpc) {
            printf("RPC received: id=%llu\n",
                   static_cast<unsigned long long>(rpc.Id));
        }
    );

    // Scene change
    g_subscriptions += g_client->OnSceneChange.Subscribe(
        [](uint32_t index, uint32_t sequence, SharedMode::Data& data) {
            printf("Scene change: index=%u sequence=%u\n", index, sequence);
        }
    );

    // Interest events
    g_subscriptions += g_client->OnInterestEnter.Subscribe(
        [](SharedMode::ObjectRoot* obj) {
            printf("Object entered interest: origin=%u counter=%u\n",
                   obj->Id.Origin, obj->Id.Counter);
        }
    );

    g_subscriptions += g_client->OnInterestExit.Subscribe(
        [](SharedMode::ObjectRoot* obj) {
            printf("Object left interest: origin=%u counter=%u\n",
                   obj->Id.Origin, obj->Id.Counter);
        }
    );

    // Forced disconnect
    g_subscriptions += g_client->OnForcedDisconnect.Subscribe(
        [](std::string message) {
            printf("Forced disconnect: %s\n", message.c_str());
        }
    );

    // Destroyed map actor notification
    g_subscriptions += g_client->OnDestroyedMapActor.Subscribe(
        [](uint32_t sceneIndex, SharedMode::ObjectId id) {
            printf("Map actor destroyed: scene=%u origin=%u counter=%u\n",
                   sceneIndex, id.Origin, id.Counter);
        }
    );
}
```

**All broadcasters on `Client`:**

| Broadcaster | Signature | When Fired |
|-------------|-----------|------------|
| `OnFusionStart` | `void()` | Room joined and Fusion protocol ready |
| `OnForcedDisconnect` | `void(std::string message)` | Server forced a disconnect |
| `OnRpc` | `void(Rpc&)` | RPC received |
| `OnSceneChange` | `void(uint32_t index, uint32_t sequence, Data&)` | Remote scene change |
| `OnObjectOwnerChanged` | `void(ObjectRoot*)` | Object ownership transferred |
| `OnOwnerWasGiven` | `void(ObjectRoot*)` | Server gave us ownership |
| `OnObjectPredictionOverride` | `void(ObjectRoot*)` | Prediction state override |
| `OnObjectReady` | `void(ObjectRoot*)` | Remote object fully ready |
| `OnSubObjectCreated` | `void(ObjectChild*)` | Remote sub-object created |
| `OnObjectDestroyed` | `void(const ObjectRoot*, DestroyModes)` | Object destroyed |
| `OnSubObjectDestroyed` | `void(ObjectChild*, DestroyModes)` | Sub-object destroyed |
| `OnInterestEnter` | `void(ObjectRoot*)` | Object entered our interest set |
| `OnInterestExit` | `void(ObjectRoot*)` | Object left our interest set |
| `OnDestroyedMapActor` | `void(uint32_t, ObjectId)` | Map actor was destroyed |

**SubscriptionBag lifetime**: The `SubscriptionBag` unsubscribes all held subscriptions when destroyed. Keep it alive as long as you need the callbacks. See [Pitfalls](../pitfalls.md#13-subscriptionbag-lifetime) for details.

## Step 4: Connect and Join a Room

Connection uses `Task<Result<T>>` coroutine-based async operations on `RealtimeClient`. Each step must complete before the next begins. Between async steps, keep calling `Service()` to pump the network.

### Using Coroutines (Recommended)

If your integration supports C++20 coroutines:

```cpp
Task<Result<void>> ConnectAndJoin(const char* region, const char* roomName) {
    auto toU8 = [](const char* s) -> PhotonCommon::StringViewType {
        return reinterpret_cast<const PhotonCommon::CharType*>(s);
    };

    // 1. Connect to Photon Name Server
    PhotonMatchmaking::ConnectOptions connectOpts;
    connectOpts.auth = {};
    Result<void> connectResult = co_await g_realtime->Connect(connectOpts);
    if (connectResult.IsErr()) {
        printf("Connect failed: %d\n",
               static_cast<int>(connectResult.GetErrorCode()));
        co_return connectResult;
    }

    // 2. Select region
    Result<void> regionResult = co_await g_realtime->SelectRegion(toU8(region));
    if (regionResult.IsErr()) {
        printf("Region select failed\n");
        co_return regionResult;
    }

    // 3. Join or create room
    PhotonMatchmaking::CreateRoomOptions roomOpts;
    roomOpts.maxPlayers = 8;
    roomOpts.plugins = {u8"Fusion3"};

    Result<MutableRoomView> roomResult = co_await g_realtime->JoinOrCreateRoom(
        toU8(roomName), roomOpts);
    if (roomResult.IsErr()) {
        printf("Join room failed: %d\n",
               static_cast<int>(roomResult.GetErrorCode()));
        co_return Result<void>::Err(roomResult.GetError());
    }

    printf("In room! Starting Fusion...\n");
    // OnFusionStart fires automatically once the server plugin responds
    co_return Result<void>::Ok();
}
```

### Polling Pattern (No Coroutines)

If coroutines are not available, store the `Task` and poll `IsReady()`:

```cpp
enum class ConnectionPhase { Idle, Connecting, SelectingRegion, JoiningRoom, Ready };

static ConnectionPhase g_phase = ConnectionPhase::Idle;
static Task<Result<void>> g_connectTask;
static Task<Result<void>> g_regionTask;
static Task<Result<MutableRoomView>> g_joinTask;

void StartConnect(const char* region, const char* roomName) {
    PhotonMatchmaking::ConnectOptions opts;
    g_connectTask = g_realtime->Connect(opts);
    g_phase = ConnectionPhase::Connecting;
    // Store region and roomName for later phases
}

void PollConnection() {
    switch (g_phase) {
    case ConnectionPhase::Connecting:
        if (g_connectTask.IsReady()) {
            auto result = g_connectTask.Get();
            if (result.IsErr()) {
                printf("Connect failed\n");
                g_phase = ConnectionPhase::Idle;
                return;
            }
            g_regionTask = g_realtime->SelectRegion(u8"us");
            g_phase = ConnectionPhase::SelectingRegion;
        }
        break;

    case ConnectionPhase::SelectingRegion:
        if (g_regionTask.IsReady()) {
            auto result = g_regionTask.Get();
            if (result.IsErr()) {
                printf("Region select failed\n");
                g_phase = ConnectionPhase::Idle;
                return;
            }
            PhotonMatchmaking::CreateRoomOptions roomOpts;
            roomOpts.maxPlayers = 8;
            roomOpts.plugins = {u8"Fusion3"};
            g_joinTask = g_realtime->JoinOrCreateRoom(u8"my_room", roomOpts);
            g_phase = ConnectionPhase::JoiningRoom;
        }
        break;

    case ConnectionPhase::JoiningRoom:
        if (g_joinTask.IsReady()) {
            auto result = g_joinTask.Get();
            if (result.IsErr()) {
                printf("Join room failed\n");
                g_phase = ConnectionPhase::Idle;
                return;
            }
            printf("In room! Waiting for OnFusionStart...\n");
            g_phase = ConnectionPhase::Ready;
        }
        break;

    default:
        break;
    }
}
```

**Key `RealtimeClient` connection methods:**

| Method | Signature | Purpose |
|--------|-----------|---------|
| `Connect()` | `Task<Result<void>>` | Connect to name server |
| `Connect(options)` | `Task<Result<void>>` | Connect with auth/options |
| `SelectRegion(region)` | `Task<Result<void>>` | Select a Photon region |
| `JoinOrCreateRoom(name, createOpts, joinOpts)` | `Task<Result<MutableRoomView>>` | Join or create a room |
| `CreateRoom(name, createOpts)` | `Task<Result<MutableRoomView>>` | Create a new room |
| `JoinRoom(name, joinOpts)` | `Task<Result<MutableRoomView>>` | Join an existing room |
| `LeaveRoom()` | `Task<Result<void>>` | Leave the current room |
| `Disconnect()` | `Task<Result<void>>` | Disconnect from server |

**Connection status:**

| Method | Returns |
|--------|---------|
| `GetState()` | `ConnectionState` enum |
| `IsConnected()` | `bool` |
| `IsInRoom()` | `bool` |
| `IsInLobby()` | `bool` |

## Step 5: The Frame Loop

The frame loop is the core of any Fusion integration. It must run every frame to keep the connection alive and synchronize objects.

```cpp
void FrameUpdate(double deltaTime) {
    if (!g_realtime || !g_client) return;

    // 1. Service the transport layer (send/receive packets, dispatch events)
    //    Must ALWAYS be called, even when not in a room.
    g_realtime->Service();

    // 2. If still connecting, poll the connection state machine
    if (!g_realtime->IsInRoom()) {
        PollConnection();
        return;
    }

    // 3. Only run Fusion sync when the client is running
    if (!g_client->IsRunning()) return;

    // 4. Write local state to Words buffers (authority objects only)
    SyncOutbound();

    // 5. End frame: packages and sends outgoing state to other clients
    g_client->UpdateFrameEnd();

    // 6. Begin frame: processes incoming packets, fires callbacks
    g_client->UpdateFrameBegin(deltaTime);

    // 7. Read remote state from Words buffers (non-authority objects)
    SyncInbound();
}
```

**Critical ordering:**

```
realtimeClient.Service()   Process network I/O
        |
        v
   sync_outbound()         Authority writes engine state to Words buffers
        |
        v
 client.UpdateFrameEnd()   Queue outbound state/RPC packets
        |
        v
client.UpdateFrameBegin()  Process inbound packets, fire callbacks
        |
        v
   sync_inbound()          Non-authority reads Words to engine state
```

- `Service()` must always be called first to process network I/O.
- `UpdateFrameEnd()` must come before `UpdateFrameBegin()`. The SDK tracks this with an internal `_expectingEnd` flag.
- `sync_outbound` writes authority data before it is sent; `sync_inbound` reads remote data after it is received.
- When not yet in a room, call only `Service()` to keep the connection progressing.

## Step 6: Create an Object

Objects carry a fixed-size `Words` buffer (array of `int32_t`) that Fusion replicates. You specify the buffer size at creation time. The last 6 words are reserved for the `ObjectTail`.

```cpp
SharedMode::ObjectRoot* CreateNetworkedObject(
    size_t userWordCount,
    uint64_t typeHash,
    SharedMode::ObjectOwnerModes ownerMode)
{
    // Account for the tail area
    size_t totalWords = userWordCount + SharedMode::Object::ExtraTailWords;

    SharedMode::TypeRef typeRef;
    typeRef.Hash = typeHash;
    typeRef.WordCount = static_cast<uint32_t>(totalWords);

    SharedMode::ObjectRoot* obj = g_client->CreateObject(
        totalWords,     // total word count including tail
        typeRef,        // type reference (hash + word count)
        nullptr,        // header data (spawn payload), or pointer to data
        0,              // header length in bytes
        0,              // scene index (0 for dynamic objects)
        ownerMode       // ObjectOwnerModes enum value
    );

    if (obj) {
        // Disable sending until spawn data is written
        obj->SetSendUpdates(false);
    }

    return obj;
}
```

After creation, copy your initial state into `obj->Words.Ptr`, then enable sending:

```cpp
void FinalizeObject(SharedMode::ObjectRoot* obj,
                    const int32_t* spawnData,
                    size_t userWordCount)
{
    if (!obj || !obj->Words.IsValid() || !spawnData) return;

    memcpy(obj->Words.Ptr, spawnData, userWordCount * sizeof(int32_t));

    obj->SetSendUpdates(true);
    obj->SetHasValidData(true);
}
```

**`ObjectOwnerModes` values:**

| Mode | Value | Behavior |
|------|-------|----------|
| `Transaction` | 0 | Ownership via explicit request/release |
| `PlayerAttached` | 1 | Owned by creating player, destroyed on leave |
| `Dynamic` | 2 | Ownership transfers dynamically with cooldown |
| `MasterClient` | 3 | Always owned by the master client |
| `GameGlobal` | 4 | Global object, no specific owner |

**`CreateObject` signature:**

```cpp
ObjectRoot* CreateObject(
    size_t words,                     // total word count (user + tail)
    const TypeRef& type,              // type hash + word count
    const PhotonCommon::CharType* header,  // spawn data bytes (nullable)
    size_t headerLength,              // spawn data byte count
    uint32_t scene,                   // scene index
    ObjectOwnerModes ownerMode,       // ownership mode
    int32_t requiredObjectsCount = 0  // number of required sub-objects
);
```

See [Object Sync Patterns](object-sync-patterns.md) for the full write/read cycle.

## Step 7: Sync Properties

Authority clients write properties to the Words buffer; non-authority clients read from it. Properties are serialized as `int32_t` words using `memcpy` bit-casting:

```cpp
void WriteFloat(SharedMode::Object* obj, int offset, float value) {
    int32_t word;
    memcpy(&word, &value, sizeof(float));
    obj->Words.Ptr[offset] = word;
}

float ReadFloat(const SharedMode::Object* obj, int offset) {
    float value;
    memcpy(&value, &obj->Words.Ptr[offset], sizeof(float));
    return value;
}
```

The Words buffer layout is fixed. Properties are written at fixed offsets without type markers or delimiters. Both writer and reader must agree on the layout. See [Object Sync Patterns](object-sync-patterns.md) for the complete type-to-word mapping.

**Tail area:** The last 6 words of the Words buffer are reserved for `ObjectTail` (RequiredObjectsCount, InterestKey, Destroyed, SendRate, Dummy). Never write user data to this region.

## Step 8: Shutdown

```cpp
void Shutdown() {
    // 1. Unsubscribe all callbacks
    g_subscriptions.UnsubscribeAll();

    // 2. Stop the Fusion client
    if (g_client) {
        g_client->Stop();
        g_client->Shutdown();
        delete g_client;
        g_client = nullptr;
    }

    // 3. Disconnect and destroy the RealtimeClient
    if (g_realtime) {
        // Note: If still connected, you may want to await Disconnect() first
        delete g_realtime;
        g_realtime = nullptr;
    }

    // 4. Clean up logging
    if (g_log_output) {
        PhotonCommon::RemoveLogOutput(g_log_output);
        delete g_log_output;
        g_log_output = nullptr;
    }
}
```

**Shutdown order matters:**
1. Unsubscribe callbacks first (prevents use-after-free in broadcaster callbacks)
2. `Stop()` halts the Fusion state sync
3. `Shutdown()` cleans up internal Fusion resources
4. Delete the Fusion client
5. Delete the RealtimeClient
6. Remove log outputs last

## Complete Minimal Example

```cpp
#include "Client.h"
#include "RealtimeClient.h"
#include "ClientConstructOptions.h"
#include "ConnectOptions.h"
#include "CreateRoomOptions.h"
#include "LogOutput.h"
#include "LogUtils.h"
#include "SubscriptionBag.h"
#include "Result.h"
#include "Task.h"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

class MyLogOutput : public PhotonCommon::LogOutput {
public:
    void LogTrace(const PhotonCommon::CharType* msg) override {
        printf("[TRACE] %s\n", reinterpret_cast<const char*>(msg));
    }
    void LogDebug(const PhotonCommon::CharType* msg) override {
        printf("[DEBUG] %s\n", reinterpret_cast<const char*>(msg));
    }
    void LogInfo(const PhotonCommon::CharType* msg) override {
        printf("[INFO]  %s\n", reinterpret_cast<const char*>(msg));
    }
    void LogWarning(const PhotonCommon::CharType* msg) override {
        printf("[WARN]  %s\n", reinterpret_cast<const char*>(msg));
    }
    void LogError(const PhotonCommon::CharType* msg) override {
        printf("[ERROR] %s\n", reinterpret_cast<const char*>(msg));
    }
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static MyLogOutput* g_log = nullptr;
static PhotonMatchmaking::RealtimeClient* g_realtime = nullptr;
static SharedMode::Client* g_client = nullptr;
static PhotonCommon::SubscriptionBag g_subs;
static SharedMode::ObjectRoot* g_myObject = nullptr;
static bool g_fusionReady = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static auto toU8(const char* s) {
    return reinterpret_cast<const PhotonCommon::CharType*>(s);
}

static int32_t floatWord(float v) {
    int32_t w;
    memcpy(&w, &v, sizeof(float));
    return w;
}

static float wordFloat(int32_t w) {
    float v;
    memcpy(&v, &w, sizeof(float));
    return v;
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

void Init(const char* appId, const char* appVersion) {
    // Logging
    g_log = new MyLogOutput();
    PhotonCommon::AddLogOutput(g_log);
    PhotonCommon::SetLogLevelsFromBitmask(
        PhotonCommon::Info | PhotonCommon::Warning | PhotonCommon::Error);

    // RealtimeClient
    PhotonMatchmaking::ClientConstructOptions opts;
    opts.appId = toU8(appId);
    opts.appVersion = toU8(appVersion);
    g_realtime = new PhotonMatchmaking::RealtimeClient(opts);

    // Fusion Client
    g_client = new SharedMode::Client(*g_realtime);

    // Callbacks
    g_subs += g_client->OnFusionStart.Subscribe([]() {
        printf("Fusion started!\n");
        g_fusionReady = true;

        // Create a test object: 3 user words (float x, float y, float z)
        size_t totalWords = 3 + SharedMode::Object::ExtraTailWords;
        SharedMode::TypeRef type{0x12345678, static_cast<uint32_t>(totalWords)};

        g_myObject = g_client->CreateObject(
            totalWords, type, nullptr, 0, 0,
            SharedMode::ObjectOwnerModes::Transaction);

        if (g_myObject) {
            g_myObject->SetSendUpdates(true);
            g_myObject->SetHasValidData(true);
        }
    });

    g_subs += g_client->OnObjectReady.Subscribe(
        [](SharedMode::ObjectRoot* obj) {
            printf("Remote object ready: origin=%u counter=%u\n",
                   obj->Id.Origin, obj->Id.Counter);
            obj->Engine = nullptr; // Store your engine pointer here
        }
    );

    g_subs += g_client->OnObjectDestroyed.Subscribe(
        [](const SharedMode::ObjectRoot* obj, SharedMode::DestroyModes mode) {
            printf("Object destroyed: mode=%d\n", static_cast<int>(mode));
        }
    );

    g_subs += g_realtime->OnDisconnected.Subscribe(
        [](PhotonMatchmaking::DisconnectCause cause) {
            printf("Disconnected: cause=%d\n", static_cast<int>(cause));
            g_fusionReady = false;
        }
    );
}

// ---------------------------------------------------------------------------
// Connection (polling pattern)
// ---------------------------------------------------------------------------

enum class Phase { Idle, Connecting, Region, Joining, Ready };
static Phase g_phase = Phase::Idle;
static PhotonMatchmaking::Task<PhotonMatchmaking::Result<void>> g_connectTask;
static PhotonMatchmaking::Task<PhotonMatchmaking::Result<void>> g_regionTask;
static PhotonMatchmaking::Task<PhotonMatchmaking::Result<PhotonMatchmaking::MutableRoomView>> g_joinTask;

void StartConnect(const char* region, const char* roomName) {
    // Store these for use across phases (simplified here)
    PhotonMatchmaking::ConnectOptions connectOpts;
    g_connectTask = g_realtime->Connect(connectOpts);
    g_phase = Phase::Connecting;
}

void PollConnection() {
    switch (g_phase) {
    case Phase::Connecting:
        if (g_connectTask.IsReady()) {
            auto result = g_connectTask.Get();
            if (result.IsErr()) { g_phase = Phase::Idle; return; }
            g_regionTask = g_realtime->SelectRegion(u8"us");
            g_phase = Phase::Region;
        }
        break;
    case Phase::Region:
        if (g_regionTask.IsReady()) {
            auto result = g_regionTask.Get();
            if (result.IsErr()) { g_phase = Phase::Idle; return; }
            PhotonMatchmaking::CreateRoomOptions roomOpts;
            roomOpts.maxPlayers = 8;
            roomOpts.plugins = {u8"Fusion3"};
            g_joinTask = g_realtime->JoinOrCreateRoom(u8"test_room", roomOpts);
            g_phase = Phase::Joining;
        }
        break;
    case Phase::Joining:
        if (g_joinTask.IsReady()) {
            auto result = g_joinTask.Get();
            if (result.IsErr()) { g_phase = Phase::Idle; return; }
            printf("In room, waiting for OnFusionStart...\n");
            g_phase = Phase::Ready;
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Frame Loop
// ---------------------------------------------------------------------------

void Tick(double dt) {
    if (!g_realtime || !g_client) return;

    // Always service transport
    g_realtime->Service();

    // Connection state machine
    if (g_phase != Phase::Ready) {
        PollConnection();
        return;
    }

    if (!g_client->IsRunning()) return;

    // --- sync_outbound: write authority state ---
    if (g_myObject && g_client->IsOwner(g_myObject)) {
        float x = 1.0f, y = 2.0f, z = 3.0f;
        g_myObject->Words.Ptr[0] = floatWord(x);
        g_myObject->Words.Ptr[1] = floatWord(y);
        g_myObject->Words.Ptr[2] = floatWord(z);
    }

    // Frame end: send outbound
    g_client->UpdateFrameEnd();

    // Frame begin: receive inbound
    g_client->UpdateFrameBegin(dt);

    // --- sync_inbound: read remote state ---
    for (auto& [id, obj] : g_client->AllRootObjects()) {
        if (!g_client->IsOwner(obj)) {
            float x = wordFloat(obj->Words.Ptr[0]);
            float y = wordFloat(obj->Words.Ptr[1]);
            float z = wordFloat(obj->Words.Ptr[2]);
            // Apply x, y, z to your engine representation via obj->Engine
            (void)x; (void)y; (void)z;
        }
    }
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void Cleanup() {
    g_subs.UnsubscribeAll();

    if (g_client) {
        g_client->Stop();
        g_client->Shutdown();
        delete g_client;
        g_client = nullptr;
    }
    if (g_realtime) {
        delete g_realtime;
        g_realtime = nullptr;
    }
    if (g_log) {
        PhotonCommon::RemoveLogOutput(g_log);
        delete g_log;
        g_log = nullptr;
    }
}
```

## Next Steps

- [Object Sync Patterns](object-sync-patterns.md) -- Word buffer layout, type mapping, arrays, strings
- [Sub-Objects](sub-objects.md) -- Child object creation and lifecycle
- [Engine Binding](engine-binding.md) -- Registry, type descriptors, spawner pattern
- [Pitfalls](../pitfalls.md) -- Critical gotchas to avoid
