#include "photon_extension_defines.h"

#if PHOTON_PLATFORM_SUPPORTED

#include <dmsdk/sdk.h>
#include "fusion_header.h"
#include "Aliases.h"


int32_t CompressFloat(float f)
{
    int32_t result;
    memcpy(&result, &f, sizeof(float));
    return result;
}
float DecompressFloat(int32_t f)
{
    float result;
    memcpy(&result, &f, sizeof(float));
    return result;
}

size_t PushFloat(SharedMode::Word* words, float f)
{
    words[0] = CompressFloat(f);
    return 1;
}
size_t PopFloat(SharedMode::Word* words, float* out)
{
    *out = DecompressFloat(words[0]);
    return 1;
}

size_t PushBool(SharedMode::Word* words, bool b)
{
    words[0] = b;
    return 1;
}
size_t PopBool(SharedMode::Word* words, bool* out)
{
    *out = (bool)words[0];
    return 1;
}

size_t PushPoint3(SharedMode::Word* words, dmVMath::Point3& p3)
{
    words[0] = CompressFloat(p3.getX());
    words[1] = CompressFloat(p3.getY());
    words[2] = CompressFloat(p3.getZ());
    return 3;
}
size_t PopPoint3(SharedMode::Word* words, dmVMath::Point3* out)
{
    out->setX(DecompressFloat(words[0]));
    out->setY(DecompressFloat(words[1]));
    out->setZ(DecompressFloat(words[2]));
    return 3;
}
size_t PushVector3(SharedMode::Word* words, dmVMath::Vector3& v3)
{
    words[0] = CompressFloat(v3.getX());
    words[1] = CompressFloat(v3.getY());
    words[2] = CompressFloat(v3.getZ());
    return 3;
}
size_t PopVector3(SharedMode::Word* words, dmVMath::Vector3* out)
{
    out->setX(DecompressFloat(words[0]));
    out->setY(DecompressFloat(words[1]));
    out->setZ(DecompressFloat(words[2]));
    return 3;
}
size_t PushVector4(SharedMode::Word* words, dmVMath::Vector4& v4)
{
    words[0] = CompressFloat(v4.getX());
    words[1] = CompressFloat(v4.getY());
    words[2] = CompressFloat(v4.getZ());
    words[3] = CompressFloat(v4.getW());
    return 4;
}
size_t PopVector4(SharedMode::Word* words, dmVMath::Vector4* out)
{
    out->setX(DecompressFloat(words[0]));
    out->setY(DecompressFloat(words[1]));
    out->setZ(DecompressFloat(words[2]));
    out->setW(DecompressFloat(words[3]));
    return 4;
}
size_t PushQuat(SharedMode::Word* words, dmVMath::Quat& q)
{
    words[0] = CompressFloat(q.getX());
    words[1] = CompressFloat(q.getY());
    words[2] = CompressFloat(q.getZ());
    words[3] = CompressFloat(q.getW());
    return 4;
}
size_t PopQuat(SharedMode::Word* words, dmVMath::Quat* out)
{
    out->setX(DecompressFloat(words[0]));
    out->setY(DecompressFloat(words[1]));
    out->setZ(DecompressFloat(words[2]));
    out->setW(DecompressFloat(words[3]));
    return 4;
}
size_t PushUint32(SharedMode::Word* words, uint32_t i)
{
    words[0] = i;
    return 1;
}
size_t PopUint32(SharedMode::Word* words, uint32_t* out)
{
    *out = (uint32_t)words[0];
    return 1;
}
size_t PushHash(SharedMode::Word* words, dmhash_t h)
{
    int32_t high = (int32_t)((h >> 32) & 0xFFFFFFFFu);
    int32_t low = (int32_t)(h & 0xFFFFFFFFu);
    words[0] = high;
    words[1] = low;
    return 2;
}
size_t PopHash(SharedMode::Word* words, dmhash_t* out)
{
    uint64_t high = (uint32_t)(words[0]);
    uint64_t low  = (uint32_t)(words[1]);
    dmhash_t h = (dmhash_t)((high << 32) | low);
    *out = h;
    return 2;
}

size_t PushHash(uint8_t* a, dmhash_t h)
{
    a[0] = (h & 0xFF00000000000000) >> 56;
    a[1] = (h & 0x00FF000000000000) >> 48;
    a[2] = (h & 0x0000FF0000000000) >> 40;
    a[3] = (h & 0x000000FF00000000) >> 32;
    a[4] = (h & 0x00000000FF000000) >> 24;
    a[5] = (h & 0x0000000000FF0000) >> 16;
    a[6] = (h & 0x000000000000FF00) >> 8;
    a[7] = (h & 0x00000000000000FF) >> 0;
    return 8;
}
size_t PopHash(uint8_t* a, dmhash_t* out)
{
    *out = (dmhash_t)(
        ((uint64_t)a[0]) << 56 |
        ((uint64_t)a[1]) << 48 |
        ((uint64_t)a[2]) << 40 |
        ((uint64_t)a[3]) << 32 |
        ((uint64_t)a[4]) << 24 |
        ((uint64_t)a[5]) << 16 |
        ((uint64_t)a[6]) << 8 |
        ((uint64_t)a[7]) << 0
        );
    return 8;
}
size_t PushUint32(uint8_t* a, uint32_t i)
{
    a[0] = (i & 0xFF000000) >> 24;
    a[1] = (i & 0x00FF0000) >> 16;
    a[2] = (i & 0x0000FF00) >> 8;
    a[3] = (i & 0x000000FF) >> 0;
    return 4;
}
size_t PopUint32(uint8_t* a, uint32_t* out)
{
    *out = (uint32_t)(
        ((uint64_t)a[0]) << 24 |
        ((uint64_t)a[1]) << 16 |
        ((uint64_t)a[2]) << 8 |
        ((uint64_t)a[3]) << 0
        );
    return 4;
}
size_t PushUint16(uint8_t* a, uint16_t i)
{
    a[0] = (i & 0x0000FF00) >> 8;
    a[1] = (i & 0x000000FF) >> 0;
    return 2;
}
size_t PopUint16(uint8_t* a, uint16_t* out)
{
    *out = (uint16_t)(
        ((uint64_t)a[0]) << 8 |
        ((uint64_t)a[1]) << 0
        );
    return 2;
}
size_t PushUint8(uint8_t* a, uint8_t i)
{
    a[0] = i;
    return 1;
}
size_t PopUint8(uint8_t* a, uint8_t* out)
{
    *out = (uint8_t)(a[0]);
    return 1;
}

#endif
