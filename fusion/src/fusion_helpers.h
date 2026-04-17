
#ifndef FUSION_HELPERS_H
#define FUSION_HELPERS_H

#include <dmsdk/sdk.h>
#include "StringType.h"
#include "PropertyValue.h"

PhotonCommon::StringType ToStringType(const char* text);
void LuaTableToStdStringVector(lua_State* L, int index, std::vector<PhotonCommon::StringType>& v);
void LuaTableToPropertyMap(lua_State* L, int index, PhotonMatchmaking::PropertyMap map);
void PrintStack(lua_State *L);
dmhash_t ResolveId(lua_State* L, int index);

#endif