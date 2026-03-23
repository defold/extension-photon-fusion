# Buffers API Reference

Binary serialization primitives for reading and writing protocol data. All buffer classes live in the `SharedMode` namespace and are defined in `Buffers.h`.

See also: [Serialization Concepts](../concepts/serialization.md)

---

## ReadBuffer

Sequential reader over a byte buffer. Advances an internal offset with each read operation. All fixed-width reads use `memcpy` for safe unaligned access.

### Constructor

```cpp
explicit ReadBuffer(Data data);
```

Wraps an existing `Data` block for reading. The buffer does not take ownership of the underlying memory.

### Fixed-Width Reads

| Method | Return Type | Size | Description |
|---|---|---|---|
| `Byte()` | `uint8_t` | 1 | Read unsigned 8-bit integer |
| `Sbyte()` | `int8_t` | 1 | Read signed 8-bit integer |
| `UShort()` | `uint16_t` | 2 | Read unsigned 16-bit integer |
| `Short()` | `int16_t` | 2 | Read signed 16-bit integer |
| `UInt()` | `uint32_t` | 4 | Read unsigned 32-bit integer |
| `Int()` | `int32_t` | 4 | Read signed 32-bit integer |
| `ULong()` | `uint64_t` | 8 | Read unsigned 64-bit integer |
| `Long()` | `int64_t` | 8 | Read signed 64-bit integer |
| `Float()` | `float` | 4 | Read 32-bit IEEE 754 float |
| `Double()` | `double` | 8 | Read 64-bit IEEE 754 double |

### Variable-Length Reads

Variable-length encoding uses LEB128 (unsigned) and ZigZag + LEB128 (signed) for compact representation of integers that are often small.

| Method | Return Type | Description |
|---|---|---|
| `ULongVar()` | `uint64_t` | Read LEB128-encoded unsigned 64-bit integer |
| `LongVar()` | `int64_t` | Read ZigZag-decoded signed 64-bit integer |
| `UIntVar()` | `uint32_t` | Read varint, cast to unsigned 32-bit |
| `IntVar()` | `int32_t` | Read varint, cast to signed 32-bit |
| `UShortVar()` | `uint16_t` | Read varint, cast to unsigned 16-bit |
| `ShortVar()` | `int16_t` | Read varint, cast to signed 16-bit |

### Composite Reads

#### `Bool`

```cpp
bool Bool();
```

Reads a single byte. Returns `true` if the byte equals `1`, `false` otherwise.

#### `Flags`

```cpp
uint8_t Flags();
```

Reads a single byte as a bitfield of flags. Alias for `Byte()`.

#### `Player`

```cpp
PlayerId Player();
```

Reads a `PlayerId` using varint encoding. Equivalent to `UIntVar()`.

#### `ObjectId`

```cpp
ObjectId ObjectId();
```

Reads an `ObjectId` by reading `Origin` (as `Player()`) followed by `Counter` (as `UIntVar()`).

#### `Versions`

```cpp
void Versions(int32_t &plugin_version, int32_t &client_version);
```

Reads plugin and client version numbers from the buffer. Used during object handshake.

#### `TimeBase`

```cpp
double TimeBase();
```

Reads and stores a time base value using `ClockQuantizeDecode`. Subsequent `Time()` calls produce values relative to this base. Should be called once per packet before any `Time()` reads.

#### `Time`

```cpp
double Time();
```

Reads a time value. If a time base has been set (via `TimeBase()`), reads a varint delta and adds it to the base. Otherwise reads an absolute quantized clock value.

### Data Reads

#### `DataAll`

```cpp
Data DataAll();
```

Returns a `Data` view of all remaining bytes in the buffer from the current offset to the end, without advancing the offset.

#### `Data`

```cpp
void Data(Data &data);
```

Reads a length-prefixed byte block. Reads the length as a varint, then copies that many bytes into `data`.

#### `Skip`

```cpp
void Skip(size_t length);
```

Advances the read offset by `length` bytes without returning any data.

---

## WriteBuffer

Sequential writer that appends data to a growable byte buffer. All fixed-width writes use `memcpy` for safe unaligned access.

### Fixed-Width Writes

| Method | Parameter Type | Size | Description |
|---|---|---|---|
| `Byte(value)` | `uint8_t` | 1 | Write unsigned 8-bit integer |
| `Sbyte(value)` | `int8_t` | 1 | Write signed 8-bit integer |
| `UShort(value)` | `uint16_t` | 2 | Write unsigned 16-bit integer |
| `Short(value)` | `int16_t` | 2 | Write signed 16-bit integer |
| `UInt(value)` | `uint32_t` | 4 | Write unsigned 32-bit integer |
| `Int(value)` | `int32_t` | 4 | Write signed 32-bit integer |
| `ULong(value)` | `uint64_t` | 8 | Write unsigned 64-bit integer |
| `Long(value)` | `int64_t` | 8 | Write signed 64-bit integer |
| `Float(value)` | `float` | 4 | Write 32-bit IEEE 754 float |
| `Double(value)` | `float` | 4 | Write as 64-bit (note: parameter type is `float` in the SDK) |

### Variable-Length Writes

| Method | Parameter Type | Description |
|---|---|---|
| `ULongVar(value)` | `uint64_t` | Write LEB128-encoded unsigned 64-bit integer |
| `LongVar(value)` | `int64_t` | Write ZigZag-encoded signed 64-bit integer |
| `UIntVar(value)` | `uint32_t` | Write varint from unsigned 32-bit |
| `IntVar(value)` | `int32_t` | Write varint from signed 32-bit |
| `UShortVar(value)` | `uint16_t` | Write varint from unsigned 16-bit |
| `ShortVar(value)` | `int16_t` | Write varint from signed 16-bit |

### Composite Writes

#### `Bool`

```cpp
bool Bool(bool value);
```

Writes a boolean as a single byte (`1` for true, `0` for false). Returns the written value.

#### `Flags`

```cpp
WriteFlags Flags();
```

Reserves one byte in the buffer and returns a `WriteFlags` handle. The byte is initialized to `0`. Use the returned handle to set flag bits after subsequent writes determine which flags apply. See [WriteFlags](#writeflags).

#### `Player`

```cpp
void Player(PlayerId id);
```

Writes a `PlayerId` using varint encoding. Equivalent to `UIntVar(id)`.

#### `ObjectId`

```cpp
void ObjectId(ObjectId id);
```

Writes an `ObjectId` by writing `Origin` (as `Player()`) followed by `Counter` (as `UIntVar()`).

#### `Versions`

```cpp
void Versions(int32_t plugin_version, int32_t client_version, int32_t client_base_version);
```

Writes plugin, client, and client base version numbers. Used during object handshake.

#### `TimeBase`

```cpp
void TimeBase(double time);
```

Writes and stores a time base value using `ClockQuantizeEncode`. Subsequent `Time()` calls will encode deltas relative to this base.

#### `Time`

```cpp
void Time(double time);
```

Writes a time value. If a time base has been set (via `TimeBase()`), writes a varint delta from the base. Otherwise writes an absolute quantized clock value.

### Data Writes

#### `Span`

```cpp
void Span(const BufferT<uint8_t> &data);
```

Writes raw bytes from a typed buffer into the write buffer without a length prefix.

#### `DataAll`

```cpp
void DataAll(const Data &data);
```

Writes all bytes from `data` into the write buffer without a length prefix.

#### `Data`

```cpp
void Data(const Data &data);
```

Writes a length-prefixed byte block. Writes the length as a varint, then copies the bytes.

### Buffer Management

#### `Take`

```cpp
Data Take();
```

Finalizes the buffer: truncates the internal `Data` to the current write offset, resets the writer to empty, and returns the finalized `Data` block. Ownership of the memory transfers to the caller.

**Precondition**: The underlying buffer must have been allocated with more capacity than the current offset (asserted).

#### `Empty`

```cpp
bool Empty() const;
```

Returns `true` if nothing has been written to the buffer (offset is `0`).

#### `Clear`

```cpp
void Clear() const;
```

Zeros out the entire underlying buffer memory without changing the offset.

#### `GetResetPoint`

```cpp
ResetPoint GetResetPoint();
```

Captures the current write offset as a `ResetPoint`. See [ResetPoint](#resetpoint).

---

## WriteFlags

A deferred flag-byte handle. Returned by `WriteBuffer::Flags()`, it allows you to set flag bits retroactively after writing the data that determines which flags are needed.

### Fields

| Field | Type | Description |
|---|---|---|
| `buffer` | `WriteBuffer*` | Pointer to the parent write buffer |
| `offset` | `size_t` | Byte offset of the reserved flag byte |

### Methods

#### `AddFlags`

```cpp
void AddFlags(uint8_t flag) const;
```

ORs the given flag bits into the reserved byte at `offset`. Can be called multiple times to accumulate flags.

### Usage Pattern

```cpp
WriteBuffer writer;
auto flags = writer.Flags();       // Reserve flag byte (initialized to 0)

if (hasData) {
    flags.AddFlags(FRAG_FLAG_DATA); // Set bit retroactively
    writer.Data(payload);
}
if (hasAcks) {
    flags.AddFlags(FRAG_FLAG_ACKS);
    writer.Data(ackData);
}
```

---

## ResetPoint

Captures a snapshot of a `WriteBuffer`'s write offset so that all data written after the snapshot can be discarded. Useful for speculative writes that may need to be rolled back.

### Constructor

```cpp
explicit ResetPoint(WriteBuffer *buffer);
```

Stores the current offset of `buffer`.

### Methods

#### `Use`

```cpp
void Use();
```

Resets the parent `WriteBuffer`'s offset back to the value captured at construction time. All data written after the `ResetPoint` was created is effectively discarded (the bytes remain in memory but will be overwritten by subsequent writes).

### Usage Pattern

```cpp
WriteBuffer writer;

// ... write some base data ...

auto reset = writer.GetResetPoint();

writer.UInt(speculativeValue);
writer.Data(speculativePayload);

if (shouldDiscard) {
    reset.Use();  // Roll back to before speculative writes
}
```
