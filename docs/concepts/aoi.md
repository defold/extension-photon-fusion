# Area of Interest (AOI)

Area of Interest is a bandwidth optimization that limits which objects a client receives state updates for. Instead of replicating every object to every client, AOI uses a 3D grid to determine proximity. Clients only receive updates for objects within their interest region.

## Grid Model

AOI divides the world into a uniform 3D grid of cubic cells. Each cell is identified by an `AOILocation`:

```cpp
struct AOILocation {
    int32_t X{0};
    int32_t Y{0};
    int32_t Z{0};

    AOILocation() = default;
    AOILocation(int32_t x, int32_t y, int32_t z);

    AOILocation GetNeighbour(int32_t xOffset = 0, int32_t yOffset = 0, int32_t zOffset = 0);
};
```

Coordinates are **cell indices**, not world positions. To convert world coordinates to cell indices, use:

```cpp
AOILocation Client::CalculateAreaOfInterestLocation(double x, double y, double z) const;
```

This divides each coordinate by the configured cell size and floors to the nearest integer.

### Cell Size

The cell size is configured at the room/session level and queried via:

```cpp
int32_t Client::AreaOfInterestCellSize() const;
```

A typical cell size might be 32, 64, or 128 world units. Smaller cells provide finer granularity but increase protocol overhead. The cell size is uniform across all three axes.

## Interest Modes

Each object declares its interest mode at creation time via `ObjectFlags`:

```cpp
enum class ObjectInterestModes : uint8_t {
    All      = 0,   // Replicated to all clients regardless of position
    Area     = 1,   // Replicated only to clients with overlapping interest
    Assigned = 2,   // Replicated only to explicitly assigned clients
};
```

| Mode | Behavior | Use Case |
|------|----------|----------|
| `All` | Every client receives updates. AOI has no effect. | Global state, UI objects, game managers |
| `Area` | Only clients whose interest region overlaps the object's cell receive updates. | Players, NPCs, world entities |
| `Assigned` | Updates are sent only to explicitly assigned clients (server plugin feature). | Private inventory, team-specific data |

The interest mode is set in the `ObjectFlags` passed to `CreateObject`, `CreateSceneObject`, or `CreateGlobalInstanceObject`:

```cpp
ObjectFlags flags(
    ObjectSettingsFlags::None,
    ObjectOwnerModes::Dynamic,
    ObjectInterestModes::Area     // <-- AOI-managed
);
```

## Setting Object Location

For objects using `ObjectInterestModes::Area`, the integration layer must update the object's AOI position each frame:

```cpp
void Client::SetAreaOfInterestLocation(const Object* obj, AOILocation location);
```

This writes the location into the object's [ObjectTail](serialization.md#objecttail) (the reserved 6-word area at the end of the Words buffer):

```cpp
struct ObjectTail {
    int32_t AOI_X;       // Cell X
    int32_t AOI_Y;       // Cell Y
    int32_t AOI_Z;       // Cell Z
    int32_t AOI_SET;     // 1 if location has been set
    int32_t Destroyed;
    int32_t Dummy;
};
```

If `AOI_SET` is never set to 1, the object behaves as if it has `InterestModes::All` until a location is provided.

### Location Calculation

```cpp
AOILocation loc = client->CalculateAreaOfInterestLocation(
    worldPosition.x,
    worldPosition.y,
    worldPosition.z
);
client->SetAreaOfInterestLocation(obj, loc);
```

## InterestBox

An `InterestBox` defines a client's interest region as an axis-aligned box of cells:

```cpp
struct InterestBox {
    AOILocation Center{};
    AOILocation Extents{};

    InterestBox() = default;
    InterestBox(AOILocation center, AOILocation extents);
};
```

The box spans from `Center - Extents` to `Center + Extents` in cell coordinates. An object is "in interest" if its cell falls within any subscribed client's interest box.

## Client Interest Locations

Each client maintains a set of AOI locations representing its subscribed cells:

```cpp
std::set<AOILocation>& Client::GetAreaOfInterestLocations();
```

The SDK computes which cells the client is interested in based on its interest box configuration. Objects whose `AOILocation` falls within these cells are replicated.

## Object Priority

When multiple objects compete for bandwidth, priority controls which objects are sent first:

```cpp
void Client::SetObjectPriority(ObjectId id, int32_t priority);
```

Higher priority objects are replicated more frequently when bandwidth is constrained. This is sent as an internal RPC (`RPC_InternalObjectPriority`).

Priority is useful for:
- Nearby objects (higher priority than distant ones)
- Objects the player is interacting with
- Critical game state objects

## Checking AOI Configuration

```cpp
bool Client::AreaOfInterestUsed() const;
```

Returns `true` if the room/session has AOI enabled. When AOI is not used, all objects are replicated to all clients regardless of `InterestModes` settings.

## Interaction with Ownership

AOI and [ownership](ownership.md) are independent systems, but they interact:

- An owner always receives updates for their own objects regardless of AOI.
- When an object leaves a non-owner client's interest region, the client stops receiving updates. If the object re-enters, the SDK resends the full state.
- Ownership transfer (`SetWantOwner`) works regardless of AOI -- a client can request ownership of an object outside its interest region.

## Performance Considerations

| Factor | Impact |
|--------|--------|
| Cell size too small | More cells to track, higher protocol overhead |
| Cell size too large | Less filtering, more unnecessary updates |
| Many `InterestModes::All` objects | Reduces AOI effectiveness |
| Not setting AOI location | Objects default to global visibility |
| Priority not set | All objects treated equally, bandwidth distribution is uniform |

## See Also

- [Object Creation](object-creation.md) -- `ObjectFlags` including `InterestModes`
- [Serialization](serialization.md) -- `ObjectTail` where AOI data is stored
- [Ownership](ownership.md) -- Interaction between AOI and ownership
- [Client API Reference](../reference/client-api.md) -- Full AOI method signatures
