#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    enum class RegionSelectionMode : uint8_t {
        Default = 0,
        Select = 1,
        Best = 2
    };
} // namespace PhotonMatchmaking
