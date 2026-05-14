// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "LobbyType.h"
#include "MatchmakingMode.h"
#include "PropertyValue.h"
#include "StringType.h"

#include <cstdint>
#include <vector>

namespace PhotonMatchmaking {
    struct MatchmakingOptions {
        PropertyMap filter;
        uint8_t maxPlayers = 0;
        MatchmakingMode mode = MatchmakingMode::FillRoom;
        PhotonCommon::StringType lobbyName;
        LobbyType lobbyType = LobbyType::Default;
        PhotonCommon::StringType sqlFilter;
        std::vector<PhotonCommon::StringType> expectedUsers;
    };
} // namespace PhotonMatchmaking
