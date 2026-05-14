// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "DirectMode.h"
#include "PropertyValue.h"
#include "StringType.h"

#include <cstdint>

namespace PhotonMatchmaking {
    struct RoomListing {
        PhotonCommon::StringType name;
        int playerCount = 0;
        uint8_t maxPlayers = 0;
        bool isOpen = false;
        DirectMode directMode = DirectMode::None;
        PropertyMap customProperties;
    };
} // namespace PhotonMatchmaking
