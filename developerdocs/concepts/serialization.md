# Serialization

Fusion uses two distinct serialization systems for different purposes:

1. **Words buffer** -- Fixed-layout `int32_t` array for continuous state replication. Properties are written at fixed offsets with no type markers. This is the primary mechanism for synchronized state.
2. **ReadBuffer / WriteBuffer** -- Variable-length byte streams for packets, RPCs, and internal protocol messages. These use varint encoding for compact representation.

## Words Buffer (State Sync)

Every [Object](objects.md) has a `Words` buffer -- a flat `int32_t` array where replicated properties are stored at predetermined offsets. The SDK compares each object's `Words` against its `Shadow` (previous frame) to detect changes and transmit only dirty words.

### Layout

```
[ property_0 ] [ property_1 ] [ ... ] [ property_N ] [ ObjectTail (6 words) ]
|<------------ user data (word_count) ------------->|<---- reserved -------->|
```

- Properties are packed sequentially with **no type markers** and **no alignment padding**.
- Each property occupies a fixed number of words determined by its type.
- The iteration order of properties must be identical on all clients.
- The last 6 words are reserved for [ObjectTail](objects.md#objecttail) and must not be written by user code.

### Type-to-Word Mapping

Each data type maps to a fixed number of 32-bit words. Floating-point values are stored via `memcpy` (bit-preserving reinterpretation), not casting.

| Type | Words | Encoding |
|------|-------|----------|
| bool | 1 | 0 or 1 |
| int32 | 1 | Direct |
| float | 1 | `memcpy` float-to-int32 |
| int64 | 2 | Low word, high word |
| double | 2 | `memcpy` to int64, then split |
| String | 2 | StringHandle (id + generation) |
| Vector2 / Vector2i | 2 | (x, y) |
| Vector3 / Vector3i | 3 | (x, y, z) |
| Vector4 / Vector4i | 4 | (x, y, z, w) |
| Quaternion | 4 | (x, y, z, w) as floats |
| Color | 4 | (r, g, b, a) as floats |
| Rect2 / Rect2i | 4 | (pos.x, pos.y, size.x, size.y) |
| Plane | 4 | (normal.x, normal.y, normal.z, d) |
| Transform2D | 6 | 2x2 columns + origin |
| AABB | 6 | (pos.x, pos.y, pos.z, size.x, size.y, size.z) |
| Basis | 9 | 3x3 matrix, row-major |
| Transform3D | 12 | Basis (9) + origin (3) |

### Float-to-Word Conversion

Floats are stored as their raw bit pattern via `memcpy`, not via cast (which is undefined behavior):

```cpp
int32_t float_to_word(float value) {
    int32_t result;
    memcpy(&result, &value, sizeof(float));
    return result;
}

float word_to_float(int32_t word) {
    float result;
    memcpy(&result, &word, sizeof(float));
    return result;
}
```

### Int64 Split

64-bit integers are split into two 32-bit words (low word first):

```cpp
void int64_to_words(int64_t value, int32_t& low, int32_t& high) {
    low  = static_cast<int32_t>(value & 0xFFFFFFFF);
    high = static_cast<int32_t>((value >> 32) & 0xFFFFFFFF);
}

int64_t words_to_int64(int32_t low, int32_t high) {
    return (static_cast<int64_t>(static_cast<uint32_t>(high)) << 32)
         | static_cast<uint32_t>(low);
}
```

Doubles use the same pattern: `memcpy` to `int64_t`, then split into two words.

### Quaternion Compression

For bandwidth-sensitive applications, quaternions can be compressed from 4 words (16 bytes) to 1 word (4 bytes) using smallest-three encoding:

```cpp
// Compress: 4 floats -> 1 uint32
uint32_t compressed = SharedMode::QuaternionCompress(x, y, z, w);

// Decompress: 1 uint32 -> 4 floats
float x, y, z, w;
SharedMode::QuaternionDecompress(compressed, x, y, z, w);
```

The algorithm stores the three smallest components with 10 bits each, plus a 2-bit index identifying the largest component (which is reconstructed from the unit constraint). This matches the server-side implementation in `Maths.cs` exactly.

### Float Quantization

For position values where full `float` precision is unnecessary:

```cpp
// Quantize: float -> int32 with N decimal places
int32_t quantized = SharedMode::FloatQuantize(3.14159f, 3);  // -> 3142

// Dequantize: int32 -> float
float restored = SharedMode::FloatDequantize<float>(3142, 3);  // -> 3.142
```

Both `float` and `double` inputs are supported. The `decimals` parameter controls precision (e.g., 2 = hundredths, 3 = thousandths).

### Array Serialization

Packed arrays use a count-prefixed layout:

```
[ count: 1 word ] [ element_0 ] [ element_1 ] ... [ element_{max-1} ]
```

- Total words: `1 + (max_capacity * element_words)`
- The `count` word stores the actual number of elements.
- Capacity is fixed at object creation time. Elements beyond capacity are truncated.
- Unused element slots are zero-filled.

### String Serialization

Strings are not stored inline in the Words buffer. Instead, a `StringHandle` (2 words: id + generation) references an entry in the root object's [NetworkedStringHeap](string-heap.md):

```
words[offset]     = handle.id
words[offset + 1] = handle.generation
```

See the [String Heap](string-heap.md) documentation for the full allocation/resolve/free lifecycle.

### Change Detection

The SDK maintains a `Shadow` buffer (copy of the previous acked state). Each frame, it compares `Words` against `Shadow` word-by-word. Only words that differ are included in the outgoing state packet. After acknowledgment, `Shadow` is updated to match `Words`.

## ReadBuffer / WriteBuffer (Packet Serialization)

`ReadBuffer` and `WriteBuffer` provide variable-length serialization for network packets, RPCs, and protocol messages. Unlike the Words buffer, these streams use compact encoding with type-aware methods.

### WriteBuffer

```cpp
class WriteBuffer {
    // Fixed-size writes
    void Byte(uint8_t value);
    void Sbyte(int8_t value);
    void UShort(uint16_t value);
    void Short(int16_t value);
    void UInt(uint32_t value);
    void Int(int32_t value);
    void ULong(uint64_t value);
    void Long(int64_t value);
    void Float(float value);
    void Double(double value);

    // Varint encoding (compact for small values)
    void ULongVar(uint64_t value);
    void LongVar(int64_t value);    // ZigZag + varint
    void UIntVar(uint32_t value);
    void IntVar(int32_t value);     // ZigZag + varint
    void UShortVar(uint16_t value);
    void ShortVar(int16_t value);   // ZigZag + varint

    // Compound types
    void ObjectId(ObjectId id);     // Player + Counter as varints
    void Player(PlayerId id);       // Varint
    WriteFlags Flags();             // 1-byte flags with deferred write
    bool Bool(bool value);
    void Time(double time);
    void TimeBase(double time);
    void Versions(int32_t plugin, int32_t client, int32_t base);
    void Data(const Data& data);
    void DataAll(const Data& data);
    void Span(const BufferT<uint8_t>& data);

    // Control
    ResetPoint GetResetPoint();     // Bookmark for rollback
    Data Take();                    // Extract written data (moves ownership)
    bool Empty() const;
    size_t Offset() const;
    void Clear() const;
};
```

### ReadBuffer

```cpp
class ReadBuffer {
    explicit ReadBuffer(Data data);

    // Fixed-size reads
    uint8_t  Byte();
    int8_t   Sbyte();
    uint16_t UShort();
    int16_t  Short();
    uint32_t UInt();
    int32_t  Int();
    uint64_t ULong();
    int64_t  Long();
    float    Float();
    double   Double();

    // Varint decoding
    uint64_t ULongVar();
    int64_t  LongVar();     // ZigZag + varint
    uint32_t UIntVar();
    int32_t  IntVar();      // ZigZag + varint
    uint16_t UShortVar();
    int16_t  ShortVar();    // ZigZag + varint

    // Compound types
    ObjectId ObjectId();
    PlayerId Player();
    uint8_t  Flags();
    bool     Bool();
    double   TimeBase();
    double   Time();
    void     Versions(int32_t& plugin, int32_t& client);

    void Data(Data& data);
    Data DataAll();
    void Skip(size_t length);
    size_t Offset() const;
};
```

### Varint Encoding

Variable-length integer encoding uses LEB128-style format. Each byte stores 7 data bits plus a continuation bit in the MSB. This is efficient for values that are typically small (ObjectIds, PlayerIds, counts):

```
Value 300 = 0b100101100:
  Byte 1: 1_0101100  (continuation=1, data=0101100)
  Byte 2: 0_0000010  (continuation=0, data=0000010)
```

Signed integers use ZigZag encoding before varint encoding to keep small negative values compact:

```cpp
int64_t ZigZagEncode(int64_t i);   // 0, -1, 1, -2, 2 -> 0, 1, 2, 3, 4
int64_t ZigZagDecode(int64_t i);   // Reverse mapping
```

### ResetPoint

`WriteBuffer` supports rollback via `ResetPoint`. Call `GetResetPoint()` before writing speculative data, and `Use()` to roll back if the data turns out to be unnecessary:

```cpp
ResetPoint rp = writer.GetResetPoint();
writer.Int(someValue);
// ...
if (shouldDiscard) {
    rp.Use();  // Rolls back to the saved position
}
```

### WriteFlags

`WriteFlags` provides a deferred flags byte. The byte is allocated immediately at the current position, but bits can be set later after subsequent writes determine the flags:

```cpp
WriteFlags flags = writer.Flags();  // Allocates 1 byte, initialized to 0
writer.Int(objectData);             // Write object data...
flags.AddFlags(OBJECT_SENDFLAG_CREATE);  // Set bits after the fact
```

### Send Flags

Object state packets use these flags to indicate what data follows:

```cpp
constexpr uint8_t OBJECT_SENDFLAG_CREATE                    = 1;
constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_ENTRIES_CHANGE = 2;
constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_DATA_CHANGE    = 4;
constexpr uint8_t OBJECT_SENDFLAG_IN_INTEREST_SET           = 8;
constexpr uint8_t OBJECT_SENDFLAG_IS_SUBOBJECT              = 16;
constexpr uint8_t OBJECT_SENDFLAG_TIMEONLY                  = 32;
```

## Related

- [Objects](objects.md) -- Object hierarchy and buffer structure
- [String Heap](string-heap.md) -- StringHandle serialization for strings
- [Architecture](architecture.md) -- Fundamental types (Word, Data, BufferT)
- [RPCs](rpcs.md) -- ReadBuffer/WriteBuffer usage for RPC payloads
