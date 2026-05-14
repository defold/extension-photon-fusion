// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "LobbyType.h"
#include "StringType.h"

namespace PhotonMatchmaking {
    struct LobbyStats {
        PhotonCommon::StringType name;
        LobbyType type = LobbyType::Default;
        int peerCount = 0;
        int roomCount = 0;
    };
} // namespace PhotonMatchmaking
