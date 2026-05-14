// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    enum class LobbyType : uint8_t {
        Default = 0,
        SqlLobby = 2,
        AsyncRandomLobby = 3
    };
} // namespace PhotonMatchmaking
