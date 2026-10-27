#ifndef FUSION_AUTH_H
#define FUSION_AUTH_H

#include "AuthenticationValues.h"

extern "C"
{
#include <dmsdk/lua/lua.h>
}

// Returns an error message, or null on success. Preserves the Lua stack and
// leaves raising Lua errors to the caller, after C++ objects are destroyed.
const char* ReadAuthenticationValues(lua_State* L, int index, PhotonMatchmaking::AuthenticationValues& auth);

#endif
