# Fusion 3 C++ SDK Documentation

Engine-agnostic reference documentation for the Photon Fusion 3 SharedMode C++ SDK.

## Architecture Overview

The Fusion 3 SDK is a client-authoritative networking library built on top of Photon's real-time infrastructure. It provides:

- **State synchronization** via fixed-layout Words buffers replicated per-object
- **Object lifecycle management** with three creation paths (spawned, scene, sub-objects)
- **Ownership and authority** with configurable modes (Transaction, Dynamic, MasterClient)
- **Remote Procedure Calls** with player/object targeting
- **Area of Interest** for spatial relevance filtering
- **Scene management** with deterministic scene sequencing
- **String replication** via a networked string heap with handle-based references

The SDK is single-threaded and requires a frame loop driven by the engine integration.

### Layered Architecture

```
┌─────────────────────────────────────┐
│         Engine Integration          │  Your code
├─────────────────────────────────────┤
│     SharedMode::Client              │  Client.h — objects, sync, RPCs, scenes
├─────────────────────────────────────┤
│     SharedMode::Photon              │  Photon.h — connection, rooms, events
├─────────────────────────────────────┤
│     Notify::Connection              │  Notify.h — reliable/unreliable channels
├─────────────────────────────────────┤
│     Photon LoadBalancing SDK        │  Exit Games transport layer
└─────────────────────────────────────┘
```

## Documentation Sections

### Concept Guides

Foundational explanations of how the SDK works.

| Document | Description |
|----------|-------------|
| [Architecture](concepts/architecture.md) | Namespaces, threading, memory model, fundamental types |
| [Frame Loop](concepts/frame-loop.md) | The mandatory `Service()` / `UpdateFrameEnd()` / `UpdateFrameBegin()` sequence |
| [Connection](concepts/connection.md) | Client construction, connect, rooms, disconnect, state queries |
| [Objects](concepts/objects.md) | Object hierarchy, Words/Shadow buffers, ObjectTail, lifecycle states |
| [Object Creation](concepts/object-creation.md) | Three creation paths: spawned, scene, sub-objects |
| [Ownership](concepts/ownership.md) | Ownership modes, requesting/releasing, cooldowns, master client |
| [Serialization](concepts/serialization.md) | ReadBuffer/WriteBuffer, Words layout, type-to-word mapping |
| [String Heap](concepts/string-heap.md) | NetworkedStringHeap, StringHandle lifecycle, spawn data exception |
| [RPCs](concepts/rpcs.md) | Rpc structure, CreateUserRpc/SendUserRpc, OnRpc routing |
| [Scene Management](concepts/scene-management.md) | Scene sequences, ChangeScene, StateUpdatesPause/Resume |
| [Area of Interest](concepts/aoi.md) | Grid-based AOI, AOILocation, InterestBox, cell size |
| [Time](concepts/time.md) | NetworkTime, time scale, time diff, per-object time |

### Integration Guides

Practical patterns for building an engine integration.

| Document | Description |
|----------|-------------|
| [Getting Started](integration/getting-started.md) | Minimal integration skeleton with complete code |
| [Object Sync Patterns](integration/object-sync-patterns.md) | Sync loop, word offsets, type mapping, arrays, strings |
| [Sub-Objects](integration/sub-objects.md) | Authority/remote creation flows, pending queue, dual handle pattern |
| [Engine Binding](integration/engine-binding.md) | Object::Engine pointer, registry, type descriptors, spawner pattern |

### API Reference

Complete per-class method documentation.

| Document | Source Header | Description |
|----------|--------------|-------------|
| [Client API](reference/client-api.md) | `Client.h` | Full `SharedMode::Client` reference |
| [Object API](reference/object-api.md) | `Types.h` | `Object`, `ObjectRoot`, `ObjectChild`, `ObjectTail` |
| [Photon API](reference/photon-api.md) | `Photon.h` | `SharedMode::Photon` connection layer |
| [Buffers API](reference/buffers-api.md) | `Buffers.h` | `ReadBuffer`, `WriteBuffer`, `WriteFlags`, `ResetPoint` |
| [StringHeap API](reference/stringheap-api.md) | `StringHeap.h` | `NetworkedStringHeap`, `StringHandle`, `StringMessage` |
| [Types API](reference/types-api.md) | `Types.h`, `Aliases.h`, `Misc.h` | Enums, structs, aliases, utility functions |
| [Notify API](reference/notify-api.md) | `Notify.h` | `Notify::Connection`, `Channel`, `Platform`, `Fragment` |

### Pitfalls

| Document | Description |
|----------|-------------|
| [Pitfalls](pitfalls.md) | 12 critical gotchas that every integration must handle |

## Header-to-Documentation Mapping

| SDK Header | Primary Documentation Pages |
|------------|----------------------------|
| `Client.h` | [Client API](reference/client-api.md), [Frame Loop](concepts/frame-loop.md), [Connection](concepts/connection.md), [Object Creation](concepts/object-creation.md), [Ownership](concepts/ownership.md), [RPCs](concepts/rpcs.md), [AOI](concepts/aoi.md), [Scene Management](concepts/scene-management.md) |
| `Types.h` | [Types API](reference/types-api.md), [Objects](concepts/objects.md), [Object API](reference/object-api.md), [Ownership](concepts/ownership.md), [RPCs](concepts/rpcs.md) |
| `Buffers.h` | [Buffers API](reference/buffers-api.md), [Serialization](concepts/serialization.md) |
| `Photon.h` | [Photon API](reference/photon-api.md), [Connection](concepts/connection.md) |
| `StringHeap.h` | [StringHeap API](reference/stringheap-api.md), [String Heap](concepts/string-heap.md) |
| `Notify.h` | [Notify API](reference/notify-api.md) |
| `Aliases.h` | [Types API](reference/types-api.md), [Architecture](concepts/architecture.md) |
| `Misc.h` | [Types API](reference/types-api.md), [Architecture](concepts/architecture.md) |
| `LogOutput.h` | [Getting Started](integration/getting-started.md) |
| `LogUtils.h` | [Getting Started](integration/getting-started.md) |
| `StringType.h` | [Types API](reference/types-api.md), [Architecture](concepts/architecture.md) |

## Reading Order

For newcomers, follow this path:

1. [Architecture](concepts/architecture.md) — foundational types and memory model
2. [Frame Loop](concepts/frame-loop.md) — the mandatory update sequence
3. [Connection](concepts/connection.md) — connecting and joining rooms
4. [Objects](concepts/objects.md) — the object model
5. [Getting Started](integration/getting-started.md) — put it all together
6. [Object Sync Patterns](integration/object-sync-patterns.md) — property replication in practice
7. [Pitfalls](pitfalls.md) — avoid the common mistakes

Then explore topics as needed: [Object Creation](concepts/object-creation.md), [Ownership](concepts/ownership.md), [RPCs](concepts/rpcs.md), [Scene Management](concepts/scene-management.md), [Sub-Objects](integration/sub-objects.md), [AOI](concepts/aoi.md).
