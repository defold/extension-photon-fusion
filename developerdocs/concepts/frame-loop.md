# Frame Loop

Fusion requires a strict 3-step call sequence every frame to function correctly. Missing a step or calling them out of order causes state corruption, connection drops, or silent data loss.

## The Mandatory Sequence

```
realtimeClient.Service()  -->  client.UpdateFrameEnd()  -->  client.UpdateFrameBegin(dt)
```

These three calls must happen **every frame**, on the **same thread**, in this exact order.

### Step 1: RealtimeClient::Service()

```cpp
void RealtimeClient::Service(bool dispatchIncomingCommands = true);
```

Pumps the Photon transport layer. Sends outgoing UDP/TCP packets, receives incoming data, handles keepalives, and advances the connection state machine. The `dispatchIncomingCommands` parameter controls whether received events are dispatched to callbacks immediately.

**Must be called even when not in a room.** Without `Service()`, the connection state machine stalls -- connect attempts never complete, keepalives stop, and the server disconnects the client.

```cpp
realtimeClient.Service(true);
```

There is also `ServiceBasic()` which performs a minimal service without dispatching, and `SendOutgoingCommands()` / `DispatchIncomingCommands()` for fine-grained control.

### Step 2: Client::UpdateFrameEnd()

```cpp
void Client::UpdateFrameEnd();
```

Packages and sends outgoing state. The SDK:

1. Iterates all objects the local client owns (or has authority over).
2. Compares each object's Words buffer against its Shadow buffer to detect changes.
3. Packs dirty words into a state packet.
4. Writes any queued RPCs into the outgoing packet.
5. Serializes StringHeap changes for objects with dirty strings.
6. Sends the packet via the Notify channel.
7. Updates ack tracking for delivery confirmation.

**Call this after your outbound sync** -- authority objects must have their Words buffers populated before `UpdateFrameEnd()` reads them.

### Step 3: Client::UpdateFrameBegin(dt)

```cpp
void Client::UpdateFrameBegin(double dt);
```

Processes incoming state. The SDK:

1. Reads state packets received since the last frame.
2. Updates Words buffers of remote objects with received data.
3. Fires `OnObjectReady` for newly ready remote objects.
4. Fires `OnSubObjectCreated` for newly created sub-objects.
5. Fires `OnObjectOwnerChanged` for ownership transfers.
6. Fires `OnOwnerWasGiven` when this client receives ownership.
7. Fires `OnObjectPredictionOverride` when authority state overrides local prediction.
8. Fires `OnObjectDestroyed` / `OnSubObjectDestroyed` for remotely destroyed objects.
9. Fires `OnInterestEnter` / `OnInterestExit` for AOI transitions.
10. Fires `OnRpc` for received RPCs.
11. Fires `OnSceneChange` for scene change notifications.
12. Advances internal timers by `dt`.

The `dt` parameter is the elapsed wall-clock time since the last call, in seconds. This drives [network time](time.md) synchronization.

**Call this before your inbound sync** -- after `UpdateFrameBegin()` returns, remote objects' Words buffers contain the latest received state, ready to be read by the integration layer.

## Complete Frame Sequence

Here is the recommended integration pattern for a single frame:

```cpp
void on_frame(double delta) {
    // 1. Pump transport
    realtimeClient->Service(true);

    // 2. Write local state to Fusion buffers (authority objects only)
    for (auto& [id, obj] : fusionClient->AllRootObjects()) {
        if (fusionClient->IsOwner(obj)) {
            write_to_words(obj);   // Your outbound sync
        }
    }

    // 3. Send outgoing state
    fusionClient->UpdateFrameEnd();

    // 4. Process incoming state and fire callbacks
    fusionClient->UpdateFrameBegin(delta);

    // 5. Read remote state from Fusion buffers (non-authority objects)
    for (auto& [id, obj] : fusionClient->AllRootObjects()) {
        read_from_words(obj);      // Your inbound sync
    }

    // 6. Engine game logic runs (physics, scripts, AI)
}
```

## Why End Before Begin?

The order `UpdateFrameEnd()` then `UpdateFrameBegin()` may seem counterintuitive. The rationale:

```
Frame N:

  [Write Words] --> [UpdateFrameEnd: SEND] --> [UpdateFrameBegin: RECV] --> [Read Words]
                         |                            ^
                         |   network transit          |
                         +----------------------------+
```

1. **Send first**: Outgoing state from the previous frame's writes is packaged and sent immediately, minimizing latency.
2. **Receive second**: Incoming state is then applied, ensuring the integration layer reads the freshest available data.
3. **Single-frame pipeline**: The authority writes -> send -> receive -> apply cycle completes in one frame.

If you reversed the order (Begin then End), authority state written this frame would not be sent until *next* frame's End call, adding one frame of latency.

## Timing and Send Rate

The SDK internally tracks a send rate (default: 30 Hz) via `_clientSendRate`. However, `UpdateFrameEnd()` handles rate limiting internally -- you should call it every frame and let the SDK decide when to actually transmit. Calling it faster than the send rate is safe; the SDK skips transmission on frames where it is not yet time to send.

The `dt` parameter to `UpdateFrameBegin()` should be the actual elapsed time. Do not pass a fixed timestep unless your frame rate is genuinely fixed. Inaccurate delta values cause [network time](time.md) drift.

## UpdateServiceOnly()

```cpp
void Client::UpdateServiceOnly();
```

A lightweight alternative that only pumps the socket layer without processing any Fusion state. Use this during loading screens or scene transitions when you need to keep the connection alive but are not ready to process state updates.

`UpdateServiceOnly()` replaces the full 3-step sequence temporarily. Resume the normal `Service() -> UpdateFrameEnd() -> UpdateFrameBegin()` sequence once loading completes.

```cpp
void on_loading_frame() {
    // Keep connection alive during scene load
    fusionClient->UpdateServiceOnly();
}
```

## StateUpdatesPause / StateUpdatesResume

```cpp
void Client::StateUpdatesPause();
void Client::StateUpdatesResume();
```

Temporarily pauses outgoing state replication without disconnecting. Objects stop sending updates while paused. Use this during scene transitions to prevent sending stale state. RPCs and connection management continue normally.

Call `StateUpdatesResume()` after the new scene is loaded and objects are re-bound.

```cpp
// Scene transition
fusionClient->StateUpdatesPause();
unload_old_scene();
load_new_scene();
register_scene_objects();
fusionClient->StateUpdatesResume();
```

## Common Mistakes

| Mistake | Symptom |
|---------|---------|
| Not calling `Service()` | Connection timeout, stuck in connecting state |
| Calling `UpdateFrameBegin()` before `UpdateFrameEnd()` | One frame of extra latency on all state |
| Not calling any update when in a room | Objects never replicate, RPCs never arrive |
| Passing 0 for `dt` | Network time stops advancing, interpolation breaks |
| Writing Words after `UpdateFrameEnd()` | Changes not sent until next frame |
| Calling `UpdateFrameEnd()` before `Start()` | Internal assertion; `_expectingEnd` guard |
| Not calling `Service()` before connection | `Connect()` task never completes |

## Related

- [Connection](connection.md) -- Connection lifecycle and `Service()` requirements
- [Time](time.md) -- How `dt` drives network time synchronization
- [Objects](objects.md) -- Words/Shadow buffers and dirty detection
- [Scene Management](scene-management.md) -- When to use `StateUpdatesPause()`
