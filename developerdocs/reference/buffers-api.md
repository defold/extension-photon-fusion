# Buffers API

Binary serialization primitives for reading and writing network packets. All classes live in the `SharedMode` namespace.

Header: `Buffers.h`

---

## ReadBuffer

Sequential binary reader over a `Data` buffer.

### Constructor

```cpp
explicit ReadBuffer(Data data);
```

Construct a reader over the given data. Reading starts at offset 0.

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Offset` | `size_t Offset() const` | Returns the current read position in bytes. |
| `Byte` | `uint8_t Byte()` | Read one unsigned byte. |
| `Sbyte` | `int8_t Sbyte()` | Read one signed byte. |
| `UShort` | `uint16_t UShort()` | Read a little-endian unsigned 16-bit integer. |
| `Short` | `int16_t Short()` | Read a little-endian signed 16-bit integer. |
| `UInt` | `uint32_t UInt()` | Read a little-endian unsigned 32-bit integer. |
| `Int` | `int32_t Int()` | Read a little-endian signed 32-bit integer. |
| `ULong` | `uint64_t ULong()` | Read a little-endian unsigned 64-bit integer. |
| `Long` | `int64_t Long()` | Read a little-endian signed 64-bit integer. |
| `Float` | `float Float()` | Read a 32-bit IEEE float. |
| `Double` | `double Double()` | Read a 64-bit IEEE double. |
| `ULongVar` | `uint64_t ULongVar()` | Read a variable-length encoded unsigned 64-bit integer. |
| `LongVar` | `int64_t LongVar()` | Read a variable-length encoded signed 64-bit integer (ZigZag decoded). |
| `UIntVar` | `uint32_t UIntVar()` | Read a variable-length encoded unsigned 32-bit integer. |
| `IntVar` | `int32_t IntVar()` | Read a variable-length encoded signed 32-bit integer. |
| `UShortVar` | `uint16_t UShortVar()` | Read a variable-length encoded unsigned 16-bit integer. |
| `ShortVar` | `int16_t ShortVar()` | Read a variable-length encoded signed 16-bit integer. |
| `Bool` | `bool Bool()` | Read a boolean (1 byte, true if `== 1`). |
| `Flags` | `uint8_t Flags()` | Read a flags byte (alias for `Byte()`). |
| `Player` | `PlayerId Player()` | Read a variable-length player ID. |
| `ObjectId` | `ObjectId ObjectId()` | Read an `ObjectId` (player origin + variable counter). |
| `Data` | `void Data(Data &data)` | Read a length-prefixed data block into the provided `Data` struct. |
| `DataAll` | `Data DataAll()` | Return the remaining unread data as a `Data` value. |
| `Skip` | `void Skip(size_t length)` | Advance the read position by `length` bytes without consuming data. |
| `TimeBase` | `double TimeBase()` | Read a quantized time base value. Caches it for subsequent `Time()` calls. |
| `Time` | `double Time()` | Read a quantized time offset relative to the last `TimeBase()`. |
| `Versions` | `void Versions(int32_t &plugin_version, int32_t &client_version)` | Read plugin and client version pair. |

---

## WriteBuffer

Sequential binary writer that manages its own growable `Data` buffer.

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Offset` | `size_t Offset() const` | Returns the current write position in bytes. |
| `Empty` | `bool Empty() const` | Returns `true` if nothing has been written. |
| `Take` | `Data Take()` | Detach and return the written data. Resets the writer. |
| `Clear` | `void Clear() const` | Zero out the buffer contents. |
| `GetResetPoint` | `ResetPoint GetResetPoint()` | Create a checkpoint at the current offset for rollback. |
| `Byte` | `void Byte(uint8_t value)` | Write one unsigned byte. |
| `Sbyte` | `void Sbyte(int8_t value)` | Write one signed byte. |
| `UShort` | `void UShort(uint16_t value)` | Write a little-endian unsigned 16-bit integer. |
| `Short` | `void Short(int16_t value)` | Write a little-endian signed 16-bit integer. |
| `UInt` | `void UInt(uint32_t value)` | Write a little-endian unsigned 32-bit integer. |
| `Int` | `void Int(int32_t value)` | Write a little-endian signed 32-bit integer. |
| `ULong` | `void ULong(uint64_t value)` | Write a little-endian unsigned 64-bit integer. |
| `Long` | `void Long(int64_t value)` | Write a little-endian signed 64-bit integer. |
| `Float` | `void Float(float value)` | Write a 32-bit IEEE float. |
| `Double` | `void Double(double value)` | Write a 64-bit IEEE double. |
| `ULongVar` | `void ULongVar(uint64_t value)` | Write a variable-length encoded unsigned 64-bit integer. |
| `LongVar` | `void LongVar(int64_t value)` | Write a variable-length encoded signed 64-bit integer (ZigZag encoded). |
| `UIntVar` | `void UIntVar(uint32_t value)` | Write a variable-length encoded unsigned 32-bit integer. |
| `IntVar` | `void IntVar(int32_t value)` | Write a variable-length encoded signed 32-bit integer. |
| `UShortVar` | `void UShortVar(uint16_t value)` | Write a variable-length encoded unsigned 16-bit integer. |
| `ShortVar` | `void ShortVar(int16_t value)` | Write a variable-length encoded signed 16-bit integer. |
| `Bool` | `bool Bool(bool value)` | Write a boolean as one byte. Returns the value. |
| `Flags` | `WriteFlags Flags()` | Reserve a flags byte and return a `WriteFlags` handle for deferred writing. |
| `Player` | `void Player(PlayerId id)` | Write a variable-length player ID. |
| `ObjectId` | `void ObjectId(ObjectId id)` | Write an `ObjectId` (player origin + variable counter). |
| `Span` | `void Span(const BufferT<uint8_t> &data)` | Write a raw byte buffer. |
| `Data` | `void Data(const Data &data)` | Write a length-prefixed data block. |
| `DataAll` | `void DataAll(const Data &data)` | Write raw data without a length prefix. |
| `Time` | `void Time(double time)` | Write a quantized time offset relative to the current time base. |
| `TimeBase` | `void TimeBase(double time)` | Write a quantized time base value. Sets the base for subsequent `Time()` calls. |
| `Versions` | `void Versions(int32_t plugin_version, int32_t client_version, int32_t client_base_version)` | Write plugin, client, and client base version triple. |

---

## WriteFlags

Handle for a deferred flags byte in a `WriteBuffer`. Created by `WriteBuffer::Flags()`.

```cpp
struct WriteFlags {
    WriteBuffer* buffer;   // Owning buffer
    size_t offset;         // Byte offset of the reserved flags byte

    void AddFlags(uint8_t flag) const;  // OR additional flag bits into the reserved byte
};
```

---

## ResetPoint

Checkpoint for rolling back a `WriteBuffer` to a previous offset. Created by `WriteBuffer::GetResetPoint()`.

```cpp
struct ResetPoint {
    explicit ResetPoint(WriteBuffer *buffer);  // Capture the current offset
    void Use();                                // Roll the buffer back to the captured offset
};
```

---

## Data

Raw byte buffer with ownership semantics.

```cpp
struct Data {
    uint8_t *Ptr{nullptr};   // Pointer to byte data
    size_t Length{0};         // Length in bytes
};
```

### Constructors

```cpp
Data();                                                        // Default: null, length 0
explicit Data(size_t length);                                  // Allocate a zeroed buffer of `length` bytes
explicit Data(const PhotonCommon::CharType *ptr, size_t length);  // Copy from a CharType source
explicit Data(uint8_t *ptr, size_t length);                    // Wrap existing memory (non-owning)
```

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Valid` | `bool Valid() const` | Returns `true` if `Ptr` is non-null and `Length > 0`. |
| `Clone` | `Data Clone() const` | Allocate a new buffer and copy the contents. |
| `Free` | `void Free()` | Deallocate the buffer and reset to null. |
| `Resize` | `void Resize(size_t length)` | Grow the buffer, preserving existing data. |
| `Slice` | `Data Slice(size_t offset) const` | Return a non-owning view starting at `offset`. |
| `CloneSlice` | `Data CloneSlice(size_t offset) const` | Clone the data starting at `offset`. |
| `operator bool` | `operator bool() const` | Equivalent to `Valid()`. |
| `operator span<uint8_t>` | `operator std::span<uint8_t>() const` | Implicit conversion to a mutable span. |
| `operator span<const uint8_t>` | `operator std::span<const uint8_t>() const` | Implicit conversion to a const span. |

---

## BufferT\<T\>

Templated owning buffer for trivially-copyable types.

```cpp
template<typename T>
struct BufferT {
    T *Ptr{nullptr};     // Pointer to element data
    size_t Length{0};    // Number of elements
};
```

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `IsValid` | `bool IsValid()` | Returns `true` if `Ptr` is non-null and `Length > 0`. |
| `Init` | `void Init(size_t length)` | Allocate a zeroed buffer of `length` elements. |
| `Resize` | `void Resize(size_t length)` | Grow the buffer to `length` elements, preserving existing data. |
| `operator T*` | `operator T*() const` | Implicit conversion to raw pointer. |
| `operator void*` | `operator void*() const` | Implicit conversion to void pointer. |

Destructor deallocates the buffer via `delete[]`.
