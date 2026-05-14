// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    enum class ReceiverGroup : uint8_t {
        Others = 0,
        All = 1,
        MasterClient = 2
    };
} // namespace PhotonMatchmaking
