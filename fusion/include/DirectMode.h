#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    enum class DirectMode : uint8_t {
        None = 0,
        AllToOthers = 1,
        MasterToOthers = 2,
        AllToAll = 3,
        MasterToAll = 4
    };
} // namespace PhotonMatchmaking
