// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "ReceiverGroup.h"

#include <vector>

namespace PhotonMatchmaking {
    struct DirectMessageOptions {
        std::vector<int> targetPlayers;
        ReceiverGroup receiverGroup = ReceiverGroup::Others;
        bool fallbackRelay = false;
    };
} // namespace PhotonMatchmaking
