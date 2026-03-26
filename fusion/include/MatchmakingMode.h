#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    enum class MatchmakingMode : uint8_t {
        FillRoom = 0,
        SerialMatching = 1,
        RandomMatching = 2
    };
} // namespace PhotonMatchmaking
