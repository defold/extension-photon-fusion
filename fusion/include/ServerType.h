#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    enum class ServerType : uint8_t {
        NameServer = 0,
        MasterServer = 1,
    };
} // namespace PhotonMatchmaking
