// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    struct WebFlags {
        bool httpForward = false;
        bool sendAuthCookie = false;
        bool sendSync = false;
        bool sendState = false;
    };
} // namespace PhotonMatchmaking
