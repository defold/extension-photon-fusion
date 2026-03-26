# Area of Interest (AOI)

Area of Interest is a bandwidth optimization that limits which objects a client receives state updates for. Instead of replicating every object to every client, AOI uses interest keys to categorize objects and player subscriptions to control visibility. Clients only receive updates for objects whose interest key matches one of their subscribed keys.

## Interest Key Model

Unlike grid-based AOI systems, Fusion 3.0 uses a key-based model. Each object is assigned an interest key (a `uint64_t`), and each player subscribes to a set of keys. The server matches object keys against player subscriptions to determine which objects to replicate.

```
Object A  --[key: 42]-->  Server  <--[subscribed: 42, 99]--> Player 1
Object B  --[key: 99]-->  Server  <--[subscribed: 42]--> Player 2
Object C  --[global]-->   Server  (sent to everyone)

Player 1 receives: A, B, C
Player 2 receives: A, C
```

## InterestKeyType

Each object's interest key has a type that determines how it is handled:

```cpp
enum class InterestKeyType : uint8_t {
    Global = 0,   // Replicated to all clients regardless of subscriptions
    Area   = 1,   // Matched against area key subscriptions
    User   = 2    // Matched against user key subscriptions
};
```

| Type | Behavior | Use Case |
|------|----------|----------|
| `Global` | Sent to all clients. No subscription needed. | Game managers, scoreboards, UI state |
| `Area` | Matched against area subscriptions. Ephemeral (cleared each update). | Spatial regions, level zones |
| `User` | Matched against user subscriptions. Persistent until removed. | Teams, parties, private channels |

## Setting Object Interest Keys

Each object declares its interest key via one of three methods on `Client`:

### Global Interest

```cpp
void Client::SetGlobalInterestKey(Object* obj);
```

Makes the object visible to all clients. This is the default for objects that have not been assigned a key.

### Area Interest

```cpp
void Client::SetAreaInterestKey(Object* obj, uint64_t key);
```

Assigns an area-type interest key. Only clients subscribed to this key (via `SetAreaKeys()`) receive updates. The key value is application-defined -- it could represent a spatial region, a zone ID, or any grouping concept.

### User Interest

```cpp
void Client::SetUserInterestKey(Object* obj, uint64_t key);
```

Assigns a user-type interest key. Only clients subscribed to this key (via `AddUserKey()`) receive updates.

### Clearing

```cpp
void Client::ClearInterestKey(Object* obj);
```

Removes the interest key, reverting the object to default behavior.

### Querying

```cpp
bool Client::HasSetInterestKey(Object* obj);
InterestKeyType Client::GetInterestKeyType(Object* obj);
```

## ObjectTail Storage

The interest key is stored in the [ObjectTail](objects.md#objecttail) at the end of the Words buffer:

```cpp
struct ObjectTail {
    int32_t RequiredObjectsCount;
    uint64_t InterestKey;       // <-- 8 bytes (2 words)
    int32_t Destroyed;
    int32_t SendRate;
    int32_t Dummy;
};
```

The `SetGlobalInterestKey`, `SetAreaInterestKey`, and `SetUserInterestKey` methods write to this field. Do not write to the interest key directly -- always use the Client methods, which also track the key type and registration state.

## Player Subscriptions

Each client maintains two independent sets of key subscriptions: area keys and user keys.

### Area Keys

```cpp
void Client::SetAreaKeys(const std::vector<std::tuple<uint64_t, uint8_t>>& keys);
```

Sets the complete list of area key subscriptions. Each entry is a `(key, sendRate)` tuple:

- `key` -- the interest key value to subscribe to
- `sendRate` -- desired send rate for objects with this key (lower = faster updates)

Area keys are **ephemeral** -- they are replaced entirely each time `SetAreaKeys()` is called. This is designed for spatial interest that changes every frame (e.g., the player's current region and neighbors).

```cpp
// Subscribe to region 42 at full rate, and neighbor region 43 at half rate
std::vector<std::tuple<uint64_t, uint8_t>> areaKeys = {
    {42, 1},   // Full rate
    {43, 2},   // Half rate
};
client->SetAreaKeys(areaKeys);
```

When multiple area keys match the same object, the **minimum send rate** (fastest) is used.

### User Keys

```cpp
void Client::AddUserKey(uint64_t key, uint8_t sendRate = 0);
void Client::RemoveUserKey(uint64_t key);
```

User keys are **persistent** -- they remain active until explicitly removed. This is designed for stable subscriptions like team channels or party membership.

```cpp
// Subscribe to team channel
client->AddUserKey(teamId, 1);

// Later: leave team
client->RemoveUserKey(teamId);
```

### Clearing Subscriptions

```cpp
void Client::ClearAllKeys();    // Clear both area and user keys
void Client::ClearAreaKeys();   // Clear only area keys
void Client::ClearUserKeys();   // Clear only user keys
```

### Querying Subscriptions

```cpp
std::vector<std::tuple<uint64_t, uint8_t>> Client::GetAllAreaKeys() const;
std::vector<std::tuple<uint64_t, uint8_t>> Client::GetAllUserKeys() const;
std::map<uint64_t, uint8_t>& Client::GetInterestKeys();
```

## Interest Enter/Exit Callbacks

When an object enters or exits a client's interest set, the SDK fires:

```cpp
Broadcaster<void(ObjectRoot*)> OnInterestEnter;
Broadcaster<void(ObjectRoot*)> OnInterestExit;
```

### OnInterestEnter

Fires when an object becomes visible to the local client. This happens when:

- The object's interest key is added to the client's subscriptions.
- The object's interest key changes to one the client is subscribed to.
- A new object is created with a key the client is already subscribed to.

On enter, the SDK performs a **snap** -- the full object state is sent immediately rather than waiting for the next delta update. This ensures the client sees the object in its current state without interpolation artifacts.

### OnInterestExit

Fires when an object leaves the client's interest set. The client stops receiving updates for this object. The engine integration should typically:

- Hide or despawn the engine entity.
- Stop reading from the object's Words buffer (data will become stale).

```cpp
subs += client->OnInterestEnter.Subscribe([](SharedMode::ObjectRoot* obj) {
    // Object became visible -- instantiate or show engine entity
    spawn_or_show(obj);
});

subs += client->OnInterestExit.Subscribe([](SharedMode::ObjectRoot* obj) {
    // Object left interest -- hide or despawn engine entity
    hide_or_despawn(obj);
});
```

## Multiple Area Key Merging

When a client subscribes to multiple area keys and an object matches more than one subscription, the server uses the **minimum send rate** (fastest updates) among all matching keys:

```
Area keys: { (42, rate=4), (43, rate=1) }
Object with key 42: send rate = 4
Object with key 43: send rate = 1
Object matching both 42 and 43: send rate = min(4, 1) = 1
```

## Complete Example

```cpp
// === Object side: assign interest keys ===

// Player avatar: area-based interest
client->SetAreaInterestKey(playerObj, regionId);

// Scoreboard: global (visible to all)
client->SetGlobalInterestKey(scoreboardObj);

// Team chat relay: user key based
client->SetUserInterestKey(teamRelayObj, teamId);


// === Player side: manage subscriptions ===

// Subscribe to nearby regions (updated each frame)
auto nearbyRegions = calculate_nearby_regions(playerPosition);
std::vector<std::tuple<uint64_t, uint8_t>> areaKeys;
for (auto& [regionId, distance] : nearbyRegions) {
    uint8_t rate = distance == 0 ? 1 : 4;  // Close = fast, far = slow
    areaKeys.push_back({regionId, rate});
}
client->SetAreaKeys(areaKeys);

// Subscribe to team channel (once, persistent)
client->AddUserKey(myTeamId, 1);


// === React to enter/exit ===

subs += client->OnInterestEnter.Subscribe([](SharedMode::ObjectRoot* obj) {
    instantiate_visual(obj);
});

subs += client->OnInterestExit.Subscribe([](SharedMode::ObjectRoot* obj) {
    destroy_visual(obj);
});
```

## Interaction with Ownership

AOI and [ownership](ownership.md) are independent systems:

- An owner always receives updates for their own objects regardless of AOI.
- When an object leaves a non-owner's interest, the client stops receiving updates. Re-entering triggers a snap.
- Ownership transfer (`SetWantOwner`) works regardless of AOI -- a client can request ownership of an object outside its interest region.

## Common Mistakes

| Mistake | Symptom |
|---------|---------|
| Not calling `SetAreaKeys` each frame | Area subscriptions stale, objects disappear |
| Using area keys for persistent subscriptions | Keys cleared on next `SetAreaKeys` call |
| Not handling `OnInterestExit` | Stale engine entities with outdated state |
| Forgetting `SetGlobalInterestKey` on global objects | Objects only visible to subscribed clients |
| Setting interest key without player subscriptions | Object invisible to all clients |

## Related

- [Objects](objects.md) -- ObjectTail where interest key is stored
- [Ownership](ownership.md) -- Interaction between AOI and ownership
- [Object Creation](object-creation.md) -- Interest keys set after creation
- [Frame Loop](frame-loop.md) -- When interest callbacks fire
