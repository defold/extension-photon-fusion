# Fusion 3 C++ SDK Integration Guide

Engine-agnostic documentation for integrating the Photon Fusion 3 SharedMode C++ SDK into any game engine.

## Architecture Overview

The Fusion 3 SDK is a client-authoritative networking library built on top of Photon's real-time infrastructure. It provides state synchronization via fixed-layout Words buffers replicated per-object, object lifecycle management with three creation paths (spawned, scene, sub-objects), ownership and authority with configurable modes, remote procedure calls with player/object targeting, area of interest filtering via interest keys, scene management with deterministic sequencing, and string replication via a networked string heap with handle-based references. The SDK is single-threaded and requires a frame loop driven by the engine integration.

### Layered Architecture

```
┌─────────────────────────────────────────┐
│         Engine Integration              │  Your code: spawners, synchronizers,
│         (Spawner, Sync, Registry)       │  registry, frame loop
├─────────────────────────────────────────┤
│     SharedMode::Client                  │  Client.h — objects, sync, RPCs,
│                                         │  ownership, interest, scenes
├─────────────────────────────────────────┤
│     PhotonMatchmaking::RealtimeClient   │  RealtimeClient.h — connect, rooms,
│                                         │  regions, Task<Result<T>> async
├─────────────────────────────────────────┤
│     Notify::Connection                  │  Notify.h — reliable/unreliable
│                                         │  channels, fragmentation
├─────────────────────────────────────────┤
│     Photon Realtime SDK                 │  Exit Games transport layer
│                                         │  (TCP/UDP, encryption, keepalive)
└─────────────────────────────────────────┘
```

## Documentation Sections

### Integration Guides

Practical patterns for building an engine integration.

| Document | Description |
|----------|-------------|
| [Getting Started](integration/getting-started.md) | Minimal integration skeleton: logging, construction, connection, frame loop, object creation, sync, shutdown. Complete working code. |
| [Object Sync Patterns](integration/object-sync-patterns.md) | Sync loop, Words buffer layout, type-to-word mapping table, float bit-cast, 64-bit splitting, arrays, strings, shadow/dirty detection. |
| [Sub-Objects](integration/sub-objects.md) | ObjectChild creation flow (authority and remote), pending queue pattern, dual handle pattern, required objects, string heap delegation. |
| [Engine Binding](integration/engine-binding.md) | Object::Engine pointer, bidirectional registry, PackedObjectId, type hash convention, spawnable type registry, spawner/factory pattern, architecture diagram. |

### Pitfalls

| Document | Description |
|----------|-------------|
| [Pitfalls](pitfalls.md) | 14 critical gotchas with wrong/right code examples: ObjectTail writes, iteration order, header inclusion, string leaks, spawn data strings, frame loop order, threading, CRT linkage, alreadyPopulated, array capacity, sub-object strings, two-step creation, SubscriptionBag lifetime, Task error handling. |

## Header-to-Documentation Mapping

| SDK Header | Primary Documentation Pages |
|------------|----------------------------|
| `Client.h` | [Getting Started](integration/getting-started.md), [Object Sync Patterns](integration/object-sync-patterns.md), [Sub-Objects](integration/sub-objects.md), [Engine Binding](integration/engine-binding.md), [Pitfalls](pitfalls.md) |
| `Types.h` | [Object Sync Patterns](integration/object-sync-patterns.md), [Sub-Objects](integration/sub-objects.md), [Pitfalls](pitfalls.md) |
| `RealtimeClient.h` | [Getting Started](integration/getting-started.md), [Pitfalls](pitfalls.md) |
| `Broadcaster.h` | [Getting Started](integration/getting-started.md), [Pitfalls](pitfalls.md) |
| `SubscriptionBag.h` | [Getting Started](integration/getting-started.md), [Pitfalls](pitfalls.md) |
| `Task.h` | [Getting Started](integration/getting-started.md), [Pitfalls](pitfalls.md) |
| `Result.h` | [Getting Started](integration/getting-started.md), [Pitfalls](pitfalls.md) |
| `Buffers.h` | [Object Sync Patterns](integration/object-sync-patterns.md) |
| `StringHeap.h` | [Object Sync Patterns](integration/object-sync-patterns.md), [Sub-Objects](integration/sub-objects.md), [Pitfalls](pitfalls.md) |
| `Aliases.h` | [Object Sync Patterns](integration/object-sync-patterns.md), [Engine Binding](integration/engine-binding.md) |
| `Misc.h` | [Object Sync Patterns](integration/object-sync-patterns.md), [Engine Binding](integration/engine-binding.md) |
| `LogOutput.h` | [Getting Started](integration/getting-started.md) |
| `LogUtils.h` | [Getting Started](integration/getting-started.md) |
| `ClientConstructOptions.h` | [Getting Started](integration/getting-started.md) |
| `ConnectOptions.h` | [Getting Started](integration/getting-started.md) |
| `CreateRoomOptions.h` | [Getting Started](integration/getting-started.md) |
| `Notify.h` | (internal transport layer, not directly documented) |
| `StringType.h` | [Getting Started](integration/getting-started.md) |

## Recommended Reading Order

For newcomers integrating the Fusion 3 SDK into a new engine:

1. **[Getting Started](integration/getting-started.md)** -- End-to-end minimal working example: logging, client construction, connection via RealtimeClient, the frame loop, creating an object, syncing a property, and shutdown. Start here.

2. **[Object Sync Patterns](integration/object-sync-patterns.md)** -- Deep dive into the Words buffer: how to map types to words, compute offsets, write/read floats and vectors, handle arrays, and manage string properties via the StringHeap. Essential for any non-trivial integration.

3. **[Pitfalls](pitfalls.md)** -- The 14 most common mistakes. Read this before writing production code. Each pitfall includes wrong/right code examples and a clear prevention rule.

4. **[Engine Binding](integration/engine-binding.md)** -- How to connect SDK objects to engine objects: the bidirectional registry, type hash conventions, spawnable scene registration, and the spawner/factory pattern that ties it all together.

5. **[Sub-Objects](integration/sub-objects.md)** -- Advanced topic: creating child objects, handling the two-step creation flow, the pending queue for out-of-order arrivals, the dual handle pattern for authority checks, and required objects.

After completing this sequence, you will have a solid foundation for building a complete Fusion integration. Refer back to individual guides as needed during implementation.
