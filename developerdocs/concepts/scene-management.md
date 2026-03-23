# Scene Management

Fusion's scene management coordinates level/map transitions across all connected clients. The master client initiates scene changes, and the SDK ensures all clients load the correct scene and register their scene objects with consistent state.

## Scene Sequence Model

Scenes are identified by a monotonically increasing **sequence number** rather than a path or index. This prevents stale scene change messages from being processed out of order -- any scene change with a sequence number less than or equal to the current sequence is discarded.

```
Sequence: 0 (no scene) -> 1 (lobby) -> 2 (level_1) -> 3 (level_2)
```

The sequence number is a `uint32_t` that starts at 0 (no scene loaded) and increments by 1 with each `ChangeScene()` call. It never resets during a session.

## ChangeScene

Only the master client calls `ChangeScene()`:

```cpp
void Client::ChangeScene(
    uint32_t     index,    // Scene index (engine-specific, e.g., 0)
    uint32_t     sequence, // Monotonically increasing sequence number
    const CharType* data   // Scene data (typically the scene path as UTF-8)
);
```

| Parameter | Description |
|-----------|-------------|
| `index` | Application-defined scene index. The SDK passes this through without interpretation. |
| `sequence` | Must be strictly greater than the previous sequence. Stale values are ignored. |
| `data` | Opaque payload, typically a UTF-8 scene path (e.g., `"res://levels/arena.tscn"`). |

`ChangeScene()` broadcasts an internal RPC (`RPC_InternalSceneChange`) to all clients. The master client does **not** receive its own `OnSceneChange` callback -- it must handle the local scene load separately.

## OnSceneChange Callback

Non-master clients receive:

```cpp
std::function<void(uint32_t index, uint32_t sequence, Data& data)> OnSceneChange;
```

| Parameter | Description |
|-----------|-------------|
| `index` | The scene index from `ChangeScene()` |
| `sequence` | The new scene sequence number |
| `data` | The scene data blob (same bytes passed to `ChangeScene()`) |

The integration layer should:

1. Compare `sequence` against the current sequence. Discard if not newer.
2. Update the local scene sequence.
3. Load the scene identified by `data`.
4. Call the scene registration flow once the scene is ready.

## State Updates Pause/Resume

During scene transitions, state updates should be paused to prevent the SDK from processing object updates for a scene that is being unloaded:

```cpp
void Client::StateUpdatesPause();
void Client::StateUpdatesResume();
```

Typical flow:

1. Master calls `ChangeScene()`.
2. Integration calls `StateUpdatesPause()`.
3. Unload old scene, destroy old scene objects.
4. Load new scene.
5. Register new scene objects (see below).
6. Call `StateUpdatesResume()`.

The SDK automatically resumes state updates on room join, so explicit calls are only needed for mid-session transitions.

## Scene Object Registration

After the scene is loaded, each synchronized object in the scene must be registered with the SDK via [`CreateSceneObject()`](object-creation.md#scene-objects-createsceneobject).

### Deterministic Object IDs

All clients loading the same scene must produce the same object ID for the same entity. A common approach:

```cpp
uint32_t objectId = hash(rootNodeName);
```

Because scene files define fixed node names, the hash is deterministic across all clients.

### Registration Flow

For each synchronized object in the loaded scene:

```cpp
bool alreadyPopulated = false;

ObjectRoot* obj = client->CreateSceneObject(
    alreadyPopulated,
    wordCount + Object::ExtraTailWords,
    typeRef,
    header, headerLength,
    currentSceneSequence,
    deterministicObjectId,
    objectFlags
);
```

The `alreadyPopulated` output parameter drives the initial data direction:

| Value | Meaning | Action |
|-------|---------|--------|
| `false` | First client to register this object | Serialize engine defaults into `Words`, then `memcpy` to buffer |
| `true` | Object already exists on the network | Deserialize `Words` buffer into engine state |

After registration:

```cpp
obj->SetSendUpdates(true);
obj->SetHasValidData(true);
```

## Scene Object Lifetime

Scene objects exist for the duration of their scene. When a new `ChangeScene()` occurs:

- Objects from the old scene are destroyed with `DestroyModes::SceneChange`.
- The new scene's objects are registered fresh.

Each scene object is tagged with the `scene` parameter (the scene sequence at creation time). The SDK uses this to associate objects with their scene.

## Global Instance Objects

Objects that should persist across scene changes use `CreateGlobalInstanceObject()` with the `ObjectSettingsFlags::IsGlobalInstance` flag:

```cpp
ObjectRoot* Client::CreateGlobalInstanceObject(
    bool&           alreadyPopulated,
    size_t          words,
    const TypeRef&  type,
    const CharType* header,
    size_t          headerLength,
    uint32_t        scene,
    uint32_t        id,
    ObjectFlags     objectFlags
);
```

These objects survive `SceneChange` destruction and maintain their state across transitions.

## Late Joiners

When a client joins mid-session, it receives the current scene sequence and path through the room state. The joining client:

1. Loads the scene identified by the current sequence.
2. Calls `CreateSceneObject()` for each synchronized object.
3. Gets `alreadyPopulated = true` for all objects (the master already populated them).
4. Deserializes the existing network state into its local scene.

This flow is identical to receiving an `OnSceneChange` -- there is no special join-time path.

## Spawned Objects and Scenes

[Spawned objects](object-creation.md#spawned-objects-createobject) also carry a `scene` parameter. This associates them with a specific scene sequence, so the SDK can clean them up during scene transitions. Objects spawned with `scene = 0` are not associated with any scene and persist until explicitly destroyed.

## See Also

- [Object Creation](object-creation.md) -- `CreateSceneObject`, `CreateGlobalInstanceObject`
- [Architecture](architecture.md) -- Frame loop and callback timing
- [RPCs](rpcs.md) -- Internal RPC used for scene change broadcast
- [Client API Reference](../reference/client-api.md) -- Full method signatures
