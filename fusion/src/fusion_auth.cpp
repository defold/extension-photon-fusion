#include "photon_extension_defines.h"

#if PHOTON_PLATFORM_SUPPORTED

#include "fusion_auth.h"

#include <cmath>
#include <cstring>

static PhotonCommon::StringType ReadString(lua_State* L, int index)
{
    size_t length;
    const char* value = lua_tolstring(L, index, &length);
    return PhotonCommon::StringType((const PhotonCommon::CharType*)value, length);
}

static bool IsAuthenticationType(lua_Number value)
{
    using PhotonMatchmaking::CustomAuthenticationType;
    if (!(value >= 0 && value <= 255) || value != (uint8_t)value)
    {
        return false;
    }

    switch ((CustomAuthenticationType)(uint8_t)value)
    {
        case CustomAuthenticationType::None:
        case CustomAuthenticationType::Custom:
        case CustomAuthenticationType::Steam:
        case CustomAuthenticationType::Facebook:
        case CustomAuthenticationType::Oculus:
        case CustomAuthenticationType::PlayStation4:
        case CustomAuthenticationType::Xbox:
        case CustomAuthenticationType::Viveport:
        case CustomAuthenticationType::NintendoSwitch:
        case CustomAuthenticationType::PlayStation5:
        case CustomAuthenticationType::Epic:
        case CustomAuthenticationType::FacebookGaming:
            return true;
        default:
            return false;
    }
}

static const char* ReadAuthenticationData(lua_State* L, int index, PhotonMatchmaking::AuthenticationValues& auth)
{
    if (lua_type(L, index) == LUA_TSTRING)
    {
        auth.data = ReadString(L, index);
        return 0;
    }
    if (lua_type(L, index) != LUA_TTABLE)
    {
        return "auth.data must be a string or a table with string keys";
    }

    PhotonMatchmaking::PropertyMap map;
    lua_pushnil(L);
    while (lua_next(L, index) != 0)
    {
        if (lua_type(L, -2) != LUA_TSTRING)
        {
            return "auth.data property tables must have string keys";
        }
        switch (lua_type(L, -1))
        {
            case LUA_TSTRING:
                map[ReadString(L, -2)] = ReadString(L, -1);
                break;
            case LUA_TBOOLEAN:
                map[ReadString(L, -2)] = (bool)lua_toboolean(L, -1);
                break;
            case LUA_TNUMBER:
                if (!std::isfinite(lua_tonumber(L, -1)))
                {
                    return "auth.data numbers must be finite";
                }
                map[ReadString(L, -2)] = (double)lua_tonumber(L, -1);
                break;
            default:
                return "auth.data property values must be strings, numbers, or booleans";
        }
        lua_pop(L, 1);
    }
    auth.data = std::move(map);
    return 0;
}

static const char* ReadAuthenticationTable(lua_State* L, int index, PhotonMatchmaking::AuthenticationValues& auth)
{
    lua_pushnil(L);
    while (lua_next(L, index) != 0)
    {
        if (lua_type(L, -2) != LUA_TSTRING)
        {
            return "auth option names must be strings";
        }
        const char* key = lua_tostring(L, -2);
        if (strcmp(key, "type") == 0)
        {
            if (lua_type(L, -1) != LUA_TNUMBER || !IsAuthenticationType(lua_tonumber(L, -1)))
            {
                return "auth.type must be a fusion.AUTH_TYPE_* constant";
            }
            auth.type = (PhotonMatchmaking::CustomAuthenticationType)(uint8_t)lua_tonumber(L, -1);
        }
        else if (strcmp(key, "user_id") == 0 || strcmp(key, "parameters") == 0)
        {
            if (lua_type(L, -1) != LUA_TSTRING)
            {
                return "auth.user_id and auth.parameters must be strings";
            }
            if (strcmp(key, "user_id") == 0)
            {
                auth.userId = ReadString(L, -1);
            }
            else
            {
                auth.parameters = ReadString(L, -1);
            }
        }
        else if (strcmp(key, "data") == 0)
        {
            const char* error = ReadAuthenticationData(L, lua_gettop(L), auth);
            if (error)
            {
                return error;
            }
        }
        else
        {
            return "unknown auth option (expected type, user_id, parameters, or data)";
        }
        lua_pop(L, 1);
    }
    return 0;
}

const char* ReadAuthenticationValues(lua_State* L, int index, PhotonMatchmaking::AuthenticationValues& auth)
{
    auth = PhotonMatchmaking::AuthenticationValues();
    if (lua_isnoneornil(L, index))
    {
        return 0;
    }
    if (lua_type(L, index) != LUA_TTABLE)
    {
        return "auth must be a table";
    }

    int top = lua_gettop(L);
    if (index < 0 && index > LUA_REGISTRYINDEX)
    {
        index = top + index + 1;
    }
    const char* error = ReadAuthenticationTable(L, index, auth);
    lua_settop(L, top);
    return error;
}

#endif
