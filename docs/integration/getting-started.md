# Getting Started: Minimal Integration

This guide walks through building a minimal Fusion SDK integration from scratch. By the end you will have a working skeleton that connects to Photon Cloud, joins a room, runs the frame loop, creates a networked object, and synchronizes properties.

See [Architecture](../concepts/architecture.md) for the conceptual foundation behind these steps.

## Prerequisites

| Item | Notes |
|------|-------|
| Fusion SDK headers | `Client.h`, `Types.h`, `Photon.h`, `Buffers.h`, `LogOutput.h`, `LogUtils.h`, `Misc.h`, `Aliases.h`, `StringHeap.h` |
| Fusion static library | `SharedMode.lib` (Windows) / `libSharedMode.a` (Linux/macOS) |
| Photon SDK | Included with Fusion -- `LoadBalancing-cpp`, `Common-cpp`, `Photon-cpp` |
| CRT linkage | **Dynamic CRT** (`/MD` on MSVC). The Fusion library is compiled with `/MD`. Mixing `/MT` and `/MD` causes linker errors or runtime crashes. |

## Header Inclusion Order

Fusion SDK defines a `Dictionary` type that collides with identically-named types in many engines (Godot, Unreal). To avoid ambiguity, **always include Fusion headers before engine headers**:

```cpp
// === CORRECT: Fusion first ===
#include "Client.h"
#include "LogOutput.h"
#include "LogUtils.h"

// Engine headers after
#include <godot_cpp/classes/node.hpp>   // Godot example
// #include "CoreMinimal.h"             // Unreal example
```

If you reverse this order, you will get compile errors about ambiguous `Dictionary` references.

## Step 1: Set Up Logging

Before creating the `Client`, wire up logging so you can see what the SDK is doing. Implement the `SharedMode::Logging::LogOutput` interface:

```cpp
#include "LogOutput.h"
#include "LogUtils.h"

class MyLogOutput : public SharedMode::Logging::LogOutput {
public:
    void LogTrace(const SharedMode::CharType* message) override {
        printf("[Fusion TRACE] %s\n", reinterpret_cast<const char*>(message));
    }
    void LogDebug(const SharedMode::CharType* message) override {
        printf("[Fusion DEBUG] %s\n", reinterpret_cast<const char*>(message));
    }
    void LogInfo(const SharedMode::CharType* message) override {
        printf("[Fusion INFO] %s\n", reinterpret_cast<const char*>(message));
    }
    void LogWarning(const SharedMode::CharType* message) override {
        printf("[Fusion WARN] %s\n", reinterpret_cast<const char*>(message));
    }
    void LogError(const SharedMode::CharType* message) override {
        printf("[Fusion ERROR] %s\n", reinterpret_cast<const char*>(message));
    }
};
```

Register it and configure log levels:

```cpp
static MyLogOutput* g_log_output = nullptr;

void InitLogging() {
    g_log_output = new MyLogOutput();
    SharedMode::Logging::AddLogOutput(g_log_output);

    // Enable desired log levels via bitmask
    // Trace=1, Debug=2, Info=4, Warning=8, Error=16
    SharedMode::Logging::SetLogLevelsFromBitmask(
        SharedMode::Logging::Info |
        SharedMode::Logging::Warning |
        SharedMode::Logging::Error
    );
}
```

**API reference:**

| Function | Signature |
|----------|-----------|
| `AddLogOutput` | `void AddLogOutput(LogOutput* logOutput)` |
| `RemoveLogOutput` | `bool RemoveLogOutput(LogOutput* logOutput)` |
| `SetLogLevelsFromBitmask` | `void SetLogLevelsFromBitmask(uint8_t logLevelMask)` |
| `LogEnable` | `void LogEnable(LogLevel logLevel)` |
| `LogDisable` | `void LogDisable(LogLevel logLevel)` |
| `IsLogEnabled` | `bool IsLogEnabled(LogLevel logLevel)` |

See [LogOutput Reference](../reference/client-api.md) for details.

## Step 2: Construct the Client

```cpp
#include "Client.h"

SharedMode::Client* g_client = nullptr;

void InitClient(const char* appId, const char* appVersion) {
    InitLogging();

    // Configure region selection mode
    ExitGames::LoadBalancing::ClientConstructOptions options;
    options.setRegionSelectionMode(1); // 1 = Select (specify region in ConnectCloud)

    g_client = new SharedMode::Client(
        reinterpret_cast<const SharedMode::CharType*>(appId),
        reinterpret_cast<const SharedMode::CharType*>(appVersion),
        options
    );
}
```

`Client` constructor signature:

```cpp
Client(const CharType* appId,
       const CharType* appVersion,
       const ExitGames::LoadBalancing::ClientConstructOptions& options = {});
```

`CharType` is `char8_t` (UTF-8). Cast `const char*` strings with `reinterpret_cast<const SharedMode::CharType*>(str)`.

## Step 3: Set Up Callbacks

The `Client` exposes `std::function` callbacks for all major events. Set them up immediately after construction:

```cpp
void SetupCallbacks() {
    g_client->OnRoomJoin = []() {
        printf("Joined room!\n");
        g_client->StateUpdatesResume(); // Required: resume state sync
    };

    g_client->OnRoomLeave = []() {
        printf("Left room.\n");
    };

    // Remote object creation (not called for locally-created objects)
    g_client->OnObjectCreated = [](SharedMode::ObjectRoot* obj) {
        printf("Remote object created: origin=%u counter=%u\n",
               obj->Id.Origin, obj->Id.Counter);
        // Instantiate your engine-side representation here
    };

    g_client->OnObjectDestroyed = [](const SharedMode::ObjectRoot* obj,
                                     SharedMode::DestroyModes mode) {
        printf("Object destroyed: mode=%d\n", static_cast<int>(mode));
        // Clean up engine-side representation
    };

    g_client->OnSubObjectCreated = [](SharedMode::ObjectChild* child) {
        printf("Sub-object created under parent: origin=%u counter=%u\n",
               child->Parent.Origin, child->Parent.Counter);
    };

    g_client->OnObjectOwnerChanged = [](SharedMode::ObjectRoot* obj) {
        printf("Owner changed for object: new owner=%u\n", obj->Owner);
    };

    g_client->OnRpc = [](SharedMode::Rpc& rpc) {
        printf("RPC received: id=%llu\n", rpc.Id);
    };

    g_client->OnSceneChange = [](uint32_t index, uint32_t sequence,
                                  SharedMode::Data& data) {
        printf("Scene change: index=%u sequence=%u\n", index, sequence);
    };
}
```

**All callbacks:**

| Callback | Signature | When Fired |
|----------|-----------|------------|
| `OnRoomJoin` | `std::function<void()>` | Local client joined a room |
| `OnRoomLeave` | `std::function<void()>` | Local client left a room |
| `OnObjectCreated` | `std::function<void(ObjectRoot*)>` | Remote client created an object |
| `OnObjectDestroyed` | `std::function<void(const ObjectRoot*, DestroyModes)>` | Any object destroyed |
| `OnSubObjectCreated` | `std::function<void(ObjectChild*)>` | Remote sub-object created |
| `OnObjectOwnerChanged` | `std::function<void(ObjectRoot*)>` | Object ownership transferred |
| `OnObjectPredictionOverride` | `std::function<void(ObjectRoot*)>` | Prediction state override |
| `OnRpc` | `std::function<void(Rpc&)>` | RPC received |
| `OnSceneChange` | `std::function<void(uint32_t, uint32_t, Data&)>` | Remote scene change |

## Step 4: Connect and Join

```cpp
void Connect(const char* region, const char* userId) {
    // ConnectCloud initiates the Photon connection handshake.
    // Service() must be called every frame for the connection to proceed.
    g_client->ConnectCloud(
        reinterpret_cast<const SharedMode::CharType*>(region),
        reinterpret_cast<const SharedMode::CharType*>(userId),
        reinterpret_cast<const SharedMode::CharType*>("")  // serverAddress: empty for cloud
    );
}

void JoinRoom(const char* roomName) {
    // Only call after Photon().IsConnected() returns true
    g_client->Photon().JoinOrCreateRoom(
        reinterpret_cast<const SharedMode::CharType*>(roomName)
    );
}
```

**Connection methods on `Photon`:**

| Method | Purpose |
|--------|---------|
| `ConnectCloud(region, userId, serverAddress)` | Connect to Photon Cloud |
| `ConnectLocal(address)` | Connect to local server |
| `JoinRoom(name)` | Join existing room |
| `JoinOrCreateRoom(name, options)` | Join or create room |
| `CreateRoom(name, options)` | Create room |
| `LeaveRoom()` | Leave current room |
| `Disconnect()` | Disconnect from server |

**Connection status polling:**

| Method | Returns |
|--------|---------|
| `Photon().Status()` | `int` status code (see constants below) |
| `Photon().IsConnected()` | `bool` -- connected to name server or better |
| `Photon().IsInRoom()` | `bool` -- in a game room |
| `Photon().IsJoiningOrInRoom()` | `bool` -- joining or in room |

Status constants (from `Photon.h`):

| Constant | Value |
|----------|-------|
| `PhotonClient_StatusNone` | 0 |
| `PhotonClient_StatusConnecting` | 1 |
| `PhotonClient_StatusError` | 2 |
| `PhotonClient_StatusDisconnected` | 3 |
| `PhotonClient_StatusConnected` | 4 |
| `PhotonClient_StatusJoiningRoom` | 5 |
| `PhotonClient_StatusInRoom` | 6 |

## Step 5: The Frame Loop

The frame loop is the core of any Fusion integration. It must run every frame to keep the connection alive and synchronize objects.

```cpp
void FrameUpdate(double deltaTime) {
    if (!g_client) return;

    SharedMode::Photon& photon = g_client->Photon();

    // 1. Service the Photon transport layer (TCP/UDP, keepalives, dispatch)
    //    Must ALWAYS be called -- even when not in a room.
    photon.Service(true);

    // 2. Only run sync logic when in a room
    if (!photon.IsInRoom()) return;

    // 3. Write local state to Words buffers (authority objects only)
    SyncOutbound();

    // 4. End frame: packages and sends outgoing state to other clients
    g_client->UpdateFrameEnd();

    // 5. Begin frame: processes incoming packets, fires callbacks
    g_client->UpdateFrameBegin(deltaTime);

    // 6. Read remote state from Words buffers (non-authority objects)
    SyncInbound();
}
```

**Critical ordering:**

1. `Service()` -- always, keeps connection alive
2. Write to Words buffers -- authority pushes local state
3. `UpdateFrameEnd()` -- sends outbound packets
4. `UpdateFrameBegin(dt)` -- processes inbound packets, triggers callbacks
5. Read from Words buffers -- non-authority applies remote state

Both the Godot and Unreal integrations follow this exact ordering. The Godot integration calls `sync_outbound()` before `UpdateFrameEnd()` and `sync_inbound()` after `UpdateFrameBegin()`. See the [Frame Loop](../concepts/frame-loop.md) concept doc for why this order matters.

## Step 6: Create an Object

Objects carry a fixed-size `Words` buffer (array of `int32_t`) that Fusion replicates. You specify the buffer size at creation time.

```cpp
SharedMode::ObjectRoot* CreateNetworkedObject(size_t userWordCount, uint64_t typeHash) {
    // The SDK appends an internal tail (ObjectTail = 6 words) for AOI and Destroyed flag.
    // You must account for this in the total allocation.
    constexpr size_t tailWords = sizeof(SharedMode::ObjectTail) / sizeof(int32_t); // = 6
    size_t totalWords = userWordCount + tailWords;

    SharedMode::TypeRef typeRef;
    typeRef.Hash = typeHash;
    typeRef.WordCount = static_cast<uint32_t>(totalWords);

    SharedMode::ObjectFlags flags(
        SharedMode::ObjectSettingsFlags::None,
        SharedMode::ObjectOwnerModes::Transaction,  // Ownership mode
        SharedMode::ObjectInterestModes::All         // Visible to all clients
    );

    SharedMode::ObjectRoot* obj = g_client->CreateObject(
        totalWords,
        typeRef,
        nullptr,  // header data (spawn payload)
        0,        // header length
        0,        // scene index
        flags
    );

    if (obj) {
        // Disable sending until spawn data is written to Words buffer
        obj->SetSendUpdates(false);
    }

    return obj;
}
```

After creation, copy your initial state into `obj->Words.Ptr`, then enable sending:

```cpp
void FinalizeObject(SharedMode::ObjectRoot* obj, const int32_t* spawnData, size_t wordCount) {
    if (obj && obj->Words.IsValid() && spawnData) {
        memcpy(obj->Words.Ptr, spawnData, wordCount * sizeof(int32_t));
    }
    obj->SetSendUpdates(true);
    obj->SetHasValidData(true);
}
```

See [Object Sync Patterns](object-sync-patterns.md) for the full write/read cycle.

## Step 7: Sync Properties

Authority clients write properties to the Words buffer; non-authority clients read from it. Properties are serialized as `int32_t` words using `memcpy` bit-casting:

```cpp
// Write a float at word offset 0
void WriteFloat(SharedMode::Object* obj, int offset, float value) {
    int32_t word;
    memcpy(&word, &value, sizeof(float));
    obj->Words.Ptr[offset] = word;
}

// Read a float from word offset 0
float ReadFloat(const SharedMode::Object* obj, int offset) {
    float value;
    memcpy(&value, &obj->Words.Ptr[offset], sizeof(float));
    return value;
}
```

The Words buffer layout is fixed -- properties are written at fixed offsets without any type markers or delimiters. Both writer and reader must agree on the layout. See [Object Sync Patterns](object-sync-patterns.md) for the complete type-to-word mapping.

**Tail area:** The last 6 words of the Words buffer are reserved for `ObjectTail` (AOI coordinates, Destroyed flag). Never write to this area -- overwriting the `Destroyed` field will silently kill the object.

## Step 8: Shutdown

```cpp
void Shutdown() {
    if (g_client) {
        g_client->Shutdown(false); // false = production, true = development mode
        delete g_client;
        g_client = nullptr;
    }

    // Clean up logging
    if (g_log_output) {
        SharedMode::Logging::RemoveLogOutput(g_log_output);
        delete g_log_output;
        g_log_output = nullptr;
    }
}
```

## Complete Minimal Example

```cpp
#include "Client.h"
#include "LogOutput.h"
#include "LogUtils.h"

// ... (MyLogOutput class from Step 1) ...

static SharedMode::Client* g_client = nullptr;
static MyLogOutput* g_log = nullptr;
static SharedMode::ObjectRoot* g_myObject = nullptr;

void Init(const char* appId, const char* appVersion) {
    g_log = new MyLogOutput();
    SharedMode::Logging::AddLogOutput(g_log);
    SharedMode::Logging::SetLogLevelsFromBitmask(0x1C); // Info|Warning|Error

    ExitGames::LoadBalancing::ClientConstructOptions opts;
    opts.setRegionSelectionMode(1);

    auto cast = [](const char* s) {
        return reinterpret_cast<const SharedMode::CharType*>(s);
    };

    g_client = new SharedMode::Client(cast(appId), cast(appVersion), opts);

    g_client->OnRoomJoin = [&]() {
        g_client->StateUpdatesResume();

        // Create a test object with 3 user words (float x, float y, float z)
        constexpr size_t tailWords = sizeof(SharedMode::ObjectTail) / sizeof(int32_t);
        size_t total = 3 + tailWords;

        SharedMode::TypeRef type{0x12345678, static_cast<uint32_t>(total)};
        SharedMode::ObjectFlags flags(
            SharedMode::ObjectSettingsFlags::None,
            SharedMode::ObjectOwnerModes::Transaction,
            SharedMode::ObjectInterestModes::All);

        g_myObject = g_client->CreateObject(total, type, nullptr, 0, 0, flags);
        if (g_myObject) {
            g_myObject->SetSendUpdates(true);
            g_myObject->SetHasValidData(true);
        }
    };

    g_client->OnObjectCreated = [](SharedMode::ObjectRoot* obj) {
        obj->Engine = nullptr; // Store your engine pointer here
    };

    g_client->OnObjectDestroyed = [](const SharedMode::ObjectRoot* obj,
                                     SharedMode::DestroyModes mode) {
        // Clean up
    };
}

void Connect(const char* region, const char* userId, const char* room) {
    auto cast = [](const char* s) {
        return reinterpret_cast<const SharedMode::CharType*>(s);
    };

    g_client->ConnectCloud(cast(region), cast(userId), cast(""));

    // In practice, poll Photon().IsConnected() before joining
    g_client->Photon().JoinOrCreateRoom(cast(room));
}

void Tick(double dt) {
    if (!g_client) return;

    g_client->Photon().Service(true);

    if (!g_client->Photon().IsInRoom()) return;

    // Write local state
    if (g_myObject && g_client->IsOwner(g_myObject)) {
        float x = 1.0f, y = 2.0f, z = 3.0f;
        memcpy(&g_myObject->Words.Ptr[0], &x, 4);
        memcpy(&g_myObject->Words.Ptr[1], &y, 4);
        memcpy(&g_myObject->Words.Ptr[2], &z, 4);
    }

    g_client->UpdateFrameEnd();
    g_client->UpdateFrameBegin(dt);

    // Read remote state from all non-owned objects
    for (auto& [id, obj] : g_client->AllObjects()) {
        if (!g_client->IsOwner(obj)) {
            float x, y, z;
            memcpy(&x, &obj->Words.Ptr[0], 4);
            memcpy(&y, &obj->Words.Ptr[1], 4);
            memcpy(&z, &obj->Words.Ptr[2], 4);
            // Apply to engine representation...
        }
    }
}

void Cleanup() {
    if (g_client) {
        g_client->Shutdown(false);
        delete g_client;
        g_client = nullptr;
    }
    if (g_log) {
        SharedMode::Logging::RemoveLogOutput(g_log);
        delete g_log;
        g_log = nullptr;
    }
}
```

## Integration Examples

**Godot integration** (`fusion_client.cpp`):
- Logging: `GodotFusionLogOutput` subclass forwards to `UtilityFunctions::print()`
- Frame loop: `process_frame()` calls `Service()` -> `sync_outbound()` -> `UpdateFrameEnd()` -> `UpdateFrameBegin()` -> `sync_inbound()`
- Callbacks set in `_setup_callbacks()` using lambdas that capture `this`

**Unreal integration** (`FusionClient.cpp`):
- Logging: Forwards to UE_LOG
- Frame loop: `Tick()` method on an Actor/Component
- Type descriptors registered for spawnable classes

## Next Steps

- [Object Sync Patterns](object-sync-patterns.md) -- Word buffer layout, type mapping, arrays, strings
- [Sub-Objects](sub-objects.md) -- Child object creation and lifecycle
- [Engine Binding](engine-binding.md) -- Registry, type descriptors, spawner pattern
- [Objects](../concepts/objects.md) -- Conceptual overview of Object, ObjectRoot, ObjectChild
- [Frame Loop](../concepts/frame-loop.md) -- Why the frame ordering matters
