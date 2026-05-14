// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "EventCache.h"
#include "ReceiverGroup.h"
#include "WebFlags.h"

#include <cstdint>
#include <vector>

namespace PhotonMatchmaking {
    struct EventOptions {
        bool reliable = true;
        uint8_t channel = 0;
        ReceiverGroup receiverGroup = ReceiverGroup::Others;
        std::vector<int> targetPlayers;
        uint8_t interestGroup = 0;
        EventCache caching = EventCache::DoNotCache;
        bool encrypt = false;
        int cacheSliceIndex = 0;
        WebFlags webFlags;
    };
} // namespace PhotonMatchmaking
