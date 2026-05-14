// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    enum class CustomAuthenticationType : uint8_t {
        None = 255,
        Custom = 0,
        Steam = 1,
        Facebook = 2,
        Oculus = 3,
        PlayStation4 = 4,
        Xbox = 5,
        Viveport = 10,
        NintendoSwitch = 11,
        PlayStation5 = 12,
        Epic = 13,
        FacebookGaming = 15
    };
} // namespace PhotonMatchmaking
