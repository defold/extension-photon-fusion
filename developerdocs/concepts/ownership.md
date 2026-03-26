# Ownership

Every networked object in Fusion has exactly one owner at any time. The owner is the client that has write authority over the object's [Words buffer](objects.md) -- only the owner's outbound state updates are accepted by the server. Ownership determines who can modify replicated properties, and the ownership model is selected per-object at creation time.

## Owner Modes

```cpp
enum class ObjectOwnerModes : uint8_t {
    Transaction    = 0,
    PlayerAttached = 1,
    Dynamic        = 2,
    MasterClient   = 3,
    GameGlobal     = 4,
};
```

| Mode | Behavior | Use Case |
|------|----------|----------|
| `Transaction` | Ownership requires explicit request/release. One owner at a time with cooldown. | Shared resources, vehicles, interactables |
| `PlayerAttached` | Permanently owned by the creating player. Cannot be transferred. | Player avatars, player-specific state |
| `Dynamic` | Any client can claim ownership instantly. Built-in cooldown prevents oscillation. | Physics objects, loose items |
| `MasterClient` | Always owned by the master client. Transfers automatically on master migration. | Game state, scoreboards, match logic |
| `GameGlobal` | Server/plugin-owned global object. No client can claim ownership. | Server-authoritative state |

### SanitizeOwnerMode

```cpp
ObjectOwnerModes Client::SanitizeOwnerMode(ObjectOwnerModes ownerMode) const;
```

Validates and normalizes an owner mode value. Call this if the mode comes from user input or configuration to ensure it maps to a valid enum value.

## Querying Ownership

```cpp
// Returns the PlayerId of the current owner
PlayerId Client::GetOwner(const Object* obj);

// Returns true if the local client is the owner
bool Client::IsOwner(const Object* obj);

// Returns true if the local client can write to the object
bool Client::CanModify(const Object* obj);

// Returns true if the object has any owner (not unowned)
bool Client::HasOwner(const Object* obj) const;
```

`IsOwner()` and `CanModify()` both check local authority, but `CanModify()` may account for additional conditions beyond basic ownership.

## Requesting Ownership

### Expressing Intent

```cpp
void Client::SetWantOwner(Object* obj);      // Signal desire to own
void Client::SetDontWantOwner(Object* obj);   // Release ownership intent
```

These methods set the object's `ObjectOwnerIntent`:

```cpp
enum class ObjectOwnerIntent : uint8_t {
    DontWantOwner = 0,
    WantOwner     = 1,
};
```

### Transaction Mode

In `Transaction` mode, ownership transfer is a coordinated process:

1. Client A calls `SetWantOwner(obj)`.
2. If the object is unowned, Client A becomes the owner immediately.
3. If Client B currently owns the object, the request is queued until Client B calls `SetDontWantOwner(obj)`.
4. After transfer, a cooldown prevents immediate re-transfer.

### Dynamic Mode

In `Dynamic` mode, any client can take ownership by calling `SetWantOwner()`. Unlike `Transaction`, the transfer happens without waiting for the current owner to release. The built-in cooldown prevents rapid ownership bouncing:

```cpp
static constexpr double DynamicOwnerCooldownTime = 1.0 / 3;  // ~333ms
```

### Clearing Cooldown

```cpp
void Client::ClearOwnerCooldown(Object* obj);
```

Resets the ownership transfer cooldown. The `OwnerIntentCooldown` field on `ObjectRoot` tracks the remaining time.

### MasterClient Mode

Objects with `MasterClient` mode are always owned by the master client:

```cpp
constexpr PlayerId MasterClientPlayerId = 0xFFFFFFFF;  // UINT32_MAX
```

When the master client disconnects, the Photon server assigns a new master. Objects with `MasterClient` mode automatically transfer to the new master.

## Send Rate Control

Ownership and send rate work together for bandwidth optimization:

### Per-Object Local Send Rate

```cpp
void Client::SetLocalSendRate(Object* obj, uint32_t sendRate);
```

Sets the local send rate divisor for an object. A value of `1` means the object sends every tick (highest rate). A value of `16` means it sends every 16th tick (lowest rate). This is a client-side optimization -- the object still exists on all clients but consumes less bandwidth when you are not the active authority.

### Server-Side Send Rate

```cpp
void Client::SetSendRate(const Object* obj, int32_t sendRate);
void Client::ResetSendRate(const Object* obj);
```

`SetSendRate` writes to the `SendRate` field in the [ObjectTail](objects.md#objecttail), which the server uses for bandwidth allocation. `ResetSendRate` clears it back to the default.

### Ownership + Send Rate Pattern

A common pattern when requesting/releasing ownership:

```cpp
// Requesting ownership: increase send rate
client->SetLocalSendRate(obj, 1);      // Send every tick
client->SetWantOwner(obj);

// Releasing ownership: decrease send rate
client->SetLocalSendRate(obj, 16);     // Send infrequently
client->SetDontWantOwner(obj);
```

This ensures that when you own an object, you send updates at full rate, and when you release it, you minimize bandwidth until someone else claims it.

## Ownership Callbacks

### OnObjectOwnerChanged

```cpp
Broadcaster<void(ObjectRoot*)> OnObjectOwnerChanged;
```

Fires when ownership of any object changes. The `ObjectRoot` has its `Owner` field already updated to the new owner's `PlayerId`. Integration layers typically:

1. Check if the local client is now the owner (`IsOwner(obj)`).
2. Enable or disable write access to the object's synchronizer.
3. Emit an engine-side signal so game logic can react.

### OnOwnerWasGiven

```cpp
Broadcaster<void(ObjectRoot*)> OnOwnerWasGiven;
```

Fires specifically on the client that *received* ownership. This is more targeted than `OnObjectOwnerChanged`, which fires on all clients. Use this to activate authority-specific behavior (e.g., enabling input processing, starting physics simulation).

### OnObjectPredictionOverride

```cpp
Broadcaster<void(ObjectRoot*)> OnObjectPredictionOverride;
```

Fires when the authoritative state from another client overrides local prediction. This happens when a client was writing to an object it does not own, and the true authority's state arrives and corrects it. Use this for visual correction or interpolation reset.

## Special Player IDs

```cpp
constexpr PlayerId MasterClientPlayerId  = 0xFFFFFFFF;      // Master client
constexpr PlayerId PluginPlayerId        = 0xFFFFFFFF - 1;   // Server plugin
constexpr PlayerId ObjectOwnerPlayerId   = 0xFFFFFFFF - 2;   // "Send to object owner"
```

## Sub-Object Authority

Sub-objects (`ObjectChild`) share their root object's authority. There is no independent ownership for children -- whoever owns the root owns all of its sub-objects:

```cpp
ObjectRoot* ObjectChild::Root() override;
```

Ownership queries on a child should go through the root:

```cpp
ObjectRoot* root = client->GetRoot(childObj);
bool owned = client->IsOwner(root);
```

## Common Mistakes

| Mistake | Symptom |
|---------|---------|
| Calling `SetWantOwner` on a `PlayerAttached` object | No effect; ownership cannot transfer |
| Writing Words without ownership | Data overwritten by authority on next update |
| Not adjusting send rate when taking ownership | Wasted bandwidth at low send rate |
| Ignoring `OnObjectPredictionOverride` | Visual snapping when authority corrects state |
| Not handling master client migration | `MasterClient`-mode objects stop updating |

## Related

- [Object Creation](object-creation.md) -- ObjectOwnerModes passed at creation time
- [Objects](objects.md) -- Object hierarchy and data model
- [AOI](aoi.md) -- Interest keys interact with ownership
- [Frame Loop](frame-loop.md) -- When ownership callbacks fire
