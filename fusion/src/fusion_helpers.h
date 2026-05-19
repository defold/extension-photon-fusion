
#ifndef FUSION_HELPERS_H
#define FUSION_HELPERS_H

#include <dmsdk/sdk.h>
#include "StringType.h"
#include "PropertyValue.h"
#include "CreateRoomOptions.h"
#include "MatchmakingOptions.h"
#include "JoinRoomOptions.h"

PhotonCommon::CharType* ToCharType(const char* text);
PhotonCommon::StringType ToStringType(const char* text);

int LuaToJson(lua_State* L, int index, char** json, size_t* json_len);
dmhash_t ResolveId(lua_State* L, int index);

PhotonMatchmaking::CreateRoomOptions CheckCreateRoomOptions(lua_State* L, int index);
PhotonMatchmaking::MatchmakingOptions CheckMatchmakingOptions(lua_State* L, int index);
PhotonMatchmaking::JoinRoomOptions CheckJoinRoomOptions(lua_State* L, int index);

int LuaAbsIndex(lua_State *L, int index);

void PrintStack(lua_State *L);

float Lerpf(float t, float a, float b);
dmVMath::Point3 LerpPoint(float t, dmVMath::Point3 a, dmVMath::Point3 b);


#endif