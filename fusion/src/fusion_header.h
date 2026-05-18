
#ifndef FUSION_HEADER_H
#define FUSION_HEADER_H

#include <dmsdk/sdk.h>
#include "Aliases.h"

int32_t CompressFloat(float f);
float DecompressFloat(int32_t f);
size_t PushBool(SharedMode::Word* words, bool b);
size_t PopBool(SharedMode::Word* words, bool* out);
size_t PushFloat(SharedMode::Word* words, float f);
size_t PopFloat(SharedMode::Word* words, float* out);
size_t PushPoint3(SharedMode::Word* words, dmVMath::Point3& p3);
size_t PopPoint3(SharedMode::Word* words, dmVMath::Point3* out);
size_t PushVector3(SharedMode::Word* words, dmVMath::Vector3& v3);
size_t PopVector3(SharedMode::Word* words, dmVMath::Vector3* out);
size_t PushVector4(SharedMode::Word* words, dmVMath::Vector4& v4);
size_t PopVector4(SharedMode::Word* words, dmVMath::Vector4* out);
size_t PushQuat(SharedMode::Word* words, dmVMath::Quat& q);
size_t PopQuat(SharedMode::Word* words, dmVMath::Quat* out);
size_t PushUint32(SharedMode::Word* words, uint32_t i);
size_t PopUint32(SharedMode::Word* words, uint32_t* out);
size_t PushHash(SharedMode::Word* words, dmhash_t h);
size_t PopHash(SharedMode::Word* words, dmhash_t* out);
size_t PushHash(uint8_t* a, dmhash_t h);
size_t PopHash(uint8_t* a, dmhash_t* out);
size_t PushUint32(uint8_t* a, uint32_t i);
size_t PopUint32(uint8_t* a, uint32_t* out);
size_t PushUint16(uint8_t* a, uint16_t i);
size_t PopUint16(uint8_t* a, uint16_t* out);
size_t PushUint8(uint8_t* a, uint8_t i);
size_t PopUint8(uint8_t* a, uint8_t* out);

#endif
