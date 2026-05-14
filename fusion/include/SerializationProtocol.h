// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    enum class SerializationProtocol : uint8_t {
        Protocol1_6 = 0,
        Protocol1_8 = 1
    };
} // namespace PhotonMatchmaking
