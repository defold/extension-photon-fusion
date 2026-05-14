// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "DirectMode.h"
#include "LobbyType.h"
#include "PropertyValue.h"
#include "StringType.h"

#include <cstdint>
#include <vector>

namespace PhotonMatchmaking {
    struct CreateRoomOptions {
        bool isVisible = true;
        bool isOpen = true;
        uint8_t maxPlayers = 0;

        PropertyMap customProperties;
        std::vector<PhotonCommon::StringType> lobbyProperties;

        PhotonCommon::StringType lobbyName;
        LobbyType lobbyType = LobbyType::Default;

        int playerTtlMs = 0;
        int emptyRoomTtlMs = 0;

        bool suppressRoomEvents = false;
        bool publishUserId = false;
        DirectMode directMode = DirectMode::None;
        std::vector<PhotonCommon::StringType> plugins;
        std::vector<PhotonCommon::StringType> expectedUsers;
    };
} // namespace PhotonMatchmaking
