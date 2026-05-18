#include "photon_extension_defines.h"

#if PHOTON_PLATFORM_SUPPORTED

#include <dmsdk/sdk.h>
#include "fusion_helpers.h"
#include "StringType.h"
#include "PropertyValue.h"

PhotonCommon::CharType* ToCharType(const char* text)
{
    return (PhotonCommon::CharType*)text;
}

PhotonCommon::StringType ToStringType(const char* text)
{
    return PhotonCommon::to_string_type((const PhotonCommon::CharType*)text);
}

void LuaTableToStdStringVector(lua_State* L, int index, std::vector<PhotonCommon::StringType>& v)
{
    if (lua_type(L, index) == LUA_TTABLE)
    {
        size_t len = lua_objlen(L, index);
        for (int i = 0; i < len; i++)
        {
            lua_pushinteger(L, i + 1);
            lua_gettable(L, index);
            const char* s = luaL_checkstring(L, -1);
            v[i] = ToStringType(s);
            lua_pop(L, 1); // pop value
        }
    }
}

void LuaTableToPropertyMap(lua_State* L, int index, PhotonMatchmaking::PropertyMap map)
{
    if (lua_type(L, index) == LUA_TTABLE)
    {
        lua_pushnil(L);
        while (lua_next(L, index) != 0)
        {
            const char* key = luaL_checkstring(L, -2);
            int type = lua_type(L, -1);
            if (type == LUA_TNUMBER)
            {
                map[ToStringType(key)] = luaL_checknumber(L, -1);
            }
            else if (type == LUA_TBOOLEAN)
            {
                map[ToStringType(key)] = lua_toboolean(L, -1);
            }
            else if (type == LUA_TSTRING)
            {
                map[ToStringType(key)] = ToStringType(luaL_checkstring(L, -1));
            }
            
            lua_pop(L, 1); // pop value
        }
    }
}

void PrintStack(lua_State *L)
{
    int top = lua_gettop(L);
    int i;

    dmLogInfo("stack top = %d", top);

    for (i = 1; i <= top; i++) {
        int t = lua_type(L, i);

        switch (t)
        {
            case LUA_TSTRING:
                dmLogInfo("[%d] %s: '%s'", i, lua_typename(L, t), lua_tostring(L, i));
                break;

            case LUA_TBOOLEAN:
                dmLogInfo("[%d] %s: %s", i, lua_typename(L, t), lua_toboolean(L, i) ? "true" : "false");
                break;

            case LUA_TNUMBER:
                dmLogInfo("[%d] %s: %g", i, lua_typename(L, t), lua_tonumber(L, i));
                break;

            case LUA_TNIL:
                dmLogInfo("[%d] %s: nil", i, lua_typename(L, t));
                break;

            default:
                /* userdata, table, function, thread, lightuserdata */
                dmLogInfo("[%d] %s: %p", i, lua_typename(L, t), lua_topointer(L, i));
                break;
        }
    }
}

dmhash_t ResolveId(lua_State* L, int index)
{
    if (lua_isnoneornil(L, index))
    {
        dmGameObject::HInstance caller_instance = dmScript::CheckGOInstance(L);
        return dmGameObject::GetIdentifier(caller_instance);
    }
    else
    {
        return dmScript::CheckHashOrString(L, index);
    }
}

float Lerpf(float t, float a, float b)
{
    return a + (b - a) * t;
}

dmVMath::Point3 LerpPoint(float t, dmVMath::Point3 a, dmVMath::Point3 b)
{
    return dmVMath::Point3(
        a.getX() + (b.getX() - a.getX()) * t,
        a.getY() + (b.getY() - a.getY()) * t,
        a.getZ() + (b.getZ() - a.getZ()) * t);
}

#endif