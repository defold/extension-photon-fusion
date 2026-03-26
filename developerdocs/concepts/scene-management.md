# Scene Management

Fusion's scene management coordinates level/map transitions across all connected clients. The master client initiates scene changes, and the SDK ensures all clients load the correct scene and register their scene objects with consistent state.

## Scene Sequence Model

Scenes are identified by a monotonically increasing **sequence number** rather than a path or index. This prevents stale scene change messages from being processed out of order -- any scene change with a sequence number less than or equal to the current sequence is discarded.

```
Sequence: 0 (no scene) -> 1 (lobby) -> 2 (level_1) -> 3 (level_2)
```

The sequence number is a `uint32_t` that starts at 0 (no scene loaded) and increments with each `ChangeScene()` call. It never resets during a session.

## ChangeScene

Only the master client calls `ChangeScene()`:

```cpp
void Client::ChangeScene(
    uint32_t            index,    // Application-defined scene index
    uint32_t            sequence, // Monotonically increasing sequence number
    const CharType*     data      // Scene data (typically the scene path as UTF-8)
);
```

| Parameter | Description |
|-----------|-------------|
| `index` | Application-defined scene index. The SDK passes this through without interpretation. |
| `sequence` | Must be strictly greater than the previous sequence. Stale values are ignored. |
| `data` | Opaque payload, typically a UTF-8 scene path (e.g., `"res://levels/arena.tscn"`). |

`ChangeScene()` broadcasts an internal RPC (`RPC_InternalSceneChange`) to all clients.

## OnSceneChange Broadcaster

All clients (including the master client) receive scene changes through:

```cpp
Broadcaster<void(uint32_t index, uint32_t sequence, Data&)> OnSceneChange;
```

| Parameter | Description |
|-----------|-------------|
| `index` | The scene index from `ChangeScene()` |
| `sequence` | The new scene sequence number |
| `data` | The scene data blob (same bytes passed to `ChangeScene()`) |

The integration layer should:

1. Compare `sequence` against the current sequence. Discard if not newer.
2. Update the local scene sequence.
3. Pause state updates.
4. Unload the old scene.
5. Load the scene identified by `data`.
6. Register scene objects.
7. Resume state updates.

## StateUpdatesPause / StateUpdatesResume

During scene transitions, state updates should be paused to prevent the SDK from processing object updates for a scene that is being unloaded:

```cpp
void Client::StateUpdatesPause();
void Client::StateUpdatesResume();
```

### Typical Transition Flow

```cpp
subs += fusionClient->OnSceneChange.Subscribe(
    [](uint32_t index, uint32_t sequence, SharedMode::Data& data) {
        // 1. Pause state updates
        fusionClient->StateUpdatesPause();

        // 2. Unload old scene, destroy old scene objects
        destroy_current_scene_objects();
        unload_current_scene();

        // 3. Load new scene
        auto scenePath = extract_path(data);
        load_scene(scenePath);

        // 4. Register new scene objects
        register_scene_objects(sequence);

        // 5. Resume
        fusionClient->StateUpdatesResume();
    }
);
```

The SDK continues pumping the connection while paused (you still call `Service()` and the update loop). Only state replication is suspended.

## Scene Object Registration

After the scene is loaded, each synchronized object in the scene must be registered with the SDK via `CreateSceneObject()`.

### Deterministic Object IDs

All clients loading the same scene must produce the same object ID for the same entity. A common approach:

```cpp
uint32_t objectId = CRC64(rootNodeName, strlen(rootNodeName));
```

Because scene files define fixed node names, the hash is deterministic across all clients.

### Registration Flow

```cpp
for (auto* node : scene_synchronized_nodes) {
    bool alreadyPopulated = false;

    ObjectRoot* obj = fusionClient->CreateSceneObject(
        alreadyPopulated,
        wordCount + Object::ExtraTailWords,
        typeRef,
        header, headerLength,
        currentSceneSequence,
        deterministicObjectId,
        ownerMode
    );

    if (alreadyPopulated) {
        // Network data exists: deserialize Words -> engine state
        read_from_words(obj, node);
    } else {
        // First client: serialize engine defaults -> Words
        write_to_words(obj, node);
    }

    obj->SetSendUpdates(true);
    obj->SetHasValidData(true);
}
```

### The alreadyPopulated Bidirectional Flow

```
Master Client (first to register):
  alreadyPopulated = false
  Engine defaults --[write]--> Words buffer --[replicate]--> Network

Late Joiner / Other Clients:
  alreadyPopulated = true
  Network --[replicate]--> Words buffer --[read]--> Engine state
```

This eliminates the need for separate "spawn data" exchange for scene objects. The same `CreateSceneObject` call handles both directions.

## OnDestroyedMapActor

When a scene object was destroyed before a late-joining client connects, that client needs to know which objects should not be instantiated:

```cpp
Broadcaster<void(uint32_t sceneIndex, ObjectId id)> OnDestroyedMapActor;
```

This fires for each pre-destroyed scene object during the join process. The integration layer should skip instantiation or immediately destroy the engine entity for the given `ObjectId`.

```cpp
subs += fusionClient->OnDestroyedMapActor.Subscribe(
    [](uint32_t sceneIndex, SharedMode::ObjectId id) {
        // This scene object was already destroyed before we joined
        mark_as_pre_destroyed(sceneIndex, id);
    }
);
```

## Scene Object Lifetime

Scene objects exist for the duration of their scene. When a new `ChangeScene()` occurs:

- Objects from the old scene are destroyed with `DestroyModes::SceneChange`.
- The new scene's objects are registered fresh.

Each scene object is tagged with the `scene` parameter (the scene sequence at creation time). The SDK uses this to associate objects with their scene.

## Global Instance Objects

Objects that should persist across scene changes use `CreateGlobalInstanceObject()`:

```cpp
ObjectRoot* fusionClient->CreateGlobalInstanceObject(
    alreadyPopulated,
    words, type, header, headerLength,
    scene, id, ownerMode
);
```

These objects survive `SceneChange` destruction and maintain their state across transitions. They follow the same `alreadyPopulated` pattern as scene objects.

## Late Joiners

When a client joins mid-session, it receives the current scene through the room state. The joining client:

1. Receives `OnSceneChange` with the current scene data.
2. Loads the scene identified by the current sequence.
3. Receives `OnDestroyedMapActor` for any pre-destroyed scene objects.
4. Calls `CreateSceneObject()` for each synchronized object.
5. Gets `alreadyPopulated = true` for all objects (the master already populated them).
6. Deserializes the existing network state into its local scene.

## Spawned Objects and Scenes

[Dynamic objects](object-creation.md) also carry a `scene` parameter. This associates them with a specific scene sequence, so the SDK can clean them up during scene transitions. Objects created with `scene = 0` are not associated with any scene and persist until explicitly destroyed.

## Common Mistakes

| Mistake | Symptom |
|---------|---------|
| Not pausing state updates during transition | Stale state applied to wrong scene |
| Non-deterministic scene object IDs | Objects mismatch between clients |
| Forgetting to call `StateUpdatesResume()` | No replication after scene load |
| Not handling `OnDestroyedMapActor` | Ghost objects for late joiners |
| Reusing sequence numbers | Scene change silently ignored |

## Related

- [Object Creation](object-creation.md) -- `CreateSceneObject`, `CreateGlobalInstanceObject`
- [Frame Loop](frame-loop.md) -- `UpdateServiceOnly()` during loading
- [RPCs](rpcs.md) -- Internal RPC used for scene change broadcast
- [Objects](objects.md) -- Object lifecycle and destruction modes
