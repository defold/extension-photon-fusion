#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    enum class ConnectionState : uint8_t {
        Disconnected,
        Connecting,
        Connected,
        JoiningRoom,
        InRoom,
        LeavingRoom,
        Disconnecting
    };
} // namespace PhotonMatchmaking
