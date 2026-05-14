// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstdint>

#if defined PHOTON_PLATFORM_MICROSOFT || defined PHOTON_PLATFORM_APPLE  || defined PHOTON_PLATFORM_ANDROID || defined PHOTON_PLATFORM_NINTENDO || defined PHOTON_PLATFORM_SONY || defined PHOTON_PLATFORM_WASM
#	define PHOTON_WEBSOCKET_AVAILABLE
#endif

#if defined _EG_ANDROID_PLATFORM //|| defined _EG_WINDOWS_PLATFORM
#	define _EG_WEBSOCKET_LWS
#endif

namespace PhotonMatchmaking {
    enum class ConnectionProtocol : uint8_t {
        UDP = 0,
        TCP = 1,
        WS = 4,
        WSS = 5,

#ifdef PHOTON_PLATFORM_WASM
        Default = WS
#else
        Default = UDP
#endif
    };
} // namespace PhotonMatchmaking
