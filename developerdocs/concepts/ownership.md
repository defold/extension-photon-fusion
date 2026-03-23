# Ownership

Every networked object in Fusion has exactly one owner at any time. The owner is the client that has write authority over the object's [Words buffer](objects.md) -- only the owner's outbound state updates are accepted by the SDK. Ownership determines who can modify replicated properties, and the ownership model is selected per-object at creation time via `ObjectFlags`.

## Owner Modes

```cpp
enum class ObjectOwnerModes : uint8_t {
    Transaction  = 0,
    Dynamic      = 1,
    MasterClient = 2,
};
```

| Mode | Behavior | Use Case |
|------|----------|----------|
| `Transaction` | Ownership transfers require explicit request/release. Only one client owns at a time. Transfer has a cooldown. | Shared resources, vehicles, interactable items |
| `Dynamic` | Any client can claim ownership instantly by expressing intent. Built-in cooldown (1/3 second) prevents rapid oscillation. | Physics objects, first-come objects |
| `MasterClient` | The master client always owns the object. Ownership cannot be transferred. | Game state, scoreboard, authoritative logic |

## Querying Ownership

```cpp
// Returns the PlayerId of the current owner
PlayerId Client::GetOwner(const Object* obj);

// Returns true if the local client is the owner
bool Client::IsOwner(const Object* obj);

// Returns true if the local client can write to the object's Words buffer
bool Client::CanModify(const Object* obj);

// Returns true if the object has any owner (not unowned)
bool Client::HasOwner(const Object* obj) const;
```

`IsOwner()` and `CanModify()` both check local authority, but `CanModify()` may account for additional conditions beyond ownership in future SDK versions.

## Requesting Ownership (Transaction Mode)

In `Transaction` mode, ownership transfer is a two-step process:

### Expressing Intent

```cpp
void Client::SetWantOwner(Object* obj);    // Signal desire to own
void Client::SetDontWantOwner(Object* obj); // Release ownership intent
```

These methods set the object's `ObjectOwnerIntent`:

```cpp
enum class ObjectOwnerIntent : uint8_t {
    DontWantOwner = 0,
    WantOwner     = 1,
};
```

When a client calls `SetWantOwner()`:
1. If the object is unowned, the requesting client becomes the owner.
2. If another client currently owns the object, the request is queued until the current owner calls `SetDontWantOwner()`.

### Releasing Ownership

Call `SetDontWantOwner()` to release ownership. If another client has a pending request, ownership transfers to them automatically.

### Cooldown

After an ownership transfer, a cooldown period prevents the same object from being transferred again immediately. Clear the cooldown explicitly if needed:

```cpp
void Client::ClearOwnerCooldown(Object* obj);
```

In `Dynamic` mode, the built-in cooldown is `DynamicOwnerCooldownTime`:

```cpp
static constexpr double DynamicOwnerCooldownTime = 1.0 / 3;  // ~333ms
```

## Dynamic Mode

In `Dynamic` mode, any client can take ownership by calling `SetWantOwner()`. Unlike `Transaction`, the transfer happens immediately without waiting for the current owner to release. The cooldown prevents rapid ownership bouncing between clients.

## MasterClient Mode

Objects with `MasterClient` mode are always owned by the master client. The `MasterClientPlayerId` constant identifies this special player:

```cpp
constexpr PlayerId MasterClientPlayerId = 0xFFFFFFFF;  // UINT32_MAX
```

When the master client disconnects, the Photon server assigns a new master client. Objects with `MasterClient` mode automatically transfer to the new master.

## Owner Changed Callback

When ownership of any object changes, the SDK fires:

```cpp
std::function<void(ObjectRoot*)> OnObjectOwnerChanged;
```

The callback receives the `ObjectRoot` with its `Owner` field already updated to the new owner's `PlayerId`. Integration layers typically:

1. Check if the local client is now the owner (`IsOwner(obj)`).
2. Enable or disable write access to the object's synchronizer.
3. Emit an engine-side signal so game logic can react.

## ObjectSettingsFlags

The `OwnerLeavesOwnerToNone` flag controls behavior when the owning client disconnects:

```cpp
enum class ObjectSettingsFlags : uint8_t {
    None                    = 0,
    OwnerLeavesOwnerToNone  = 1 << 0,   // Object becomes unowned on disconnect
    IsGlobalInstance        = 1 << 1,   // Survives scene transitions
};
```

| Flag | On Owner Disconnect |
|------|-------------------|
| Not set | Object is destroyed |
| `OwnerLeavesOwnerToNone` | Object becomes unowned; another client can claim it |

## Special Player IDs

```cpp
constexpr PlayerId MasterClientPlayerId  = 0xFFFFFFFF;      // Master client
constexpr PlayerId PluginPlayerId        = 0xFFFFFFFF - 1;   // Server plugin
constexpr PlayerId ObjectOwnerPlayerId   = 0xFFFFFFFF - 2;   // "Send to object owner" target
```

## Sub-Object Authority

Sub-objects (`ObjectChild`) share their root object's authority. There is no independent ownership for children -- whoever owns the root owns all of its sub-objects. The `Root()` method navigates from any child to its root for authority checks:

```cpp
ObjectRoot* ObjectChild::Root() override;
```

## See Also

- [Object Creation](object-creation.md) -- `ObjectFlags` and creation paths
- [Objects](objects.md) -- Object hierarchy and data model
- [Area of Interest](aoi.md) -- Interest modes that interact with ownership
- [Client API Reference](../reference/client-api.md) -- Full method signatures
