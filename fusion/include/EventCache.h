// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    enum class EventCache : uint8_t {
        DoNotCache = 0,
        MergeCache = 1,
        ReplaceCache = 2,
        RemoveCache = 3,
        AddToRoomCache = 4,
        AddToRoomCacheGlobal = 5,
        RemoveFromRoomCache = 6,
        RemoveFromRoomCacheForActorsLeft = 7,
        SliceIncIndex = 10,
        SliceSetIndex = 11,
        SlicePurgeIndex = 12,
        SlicePurgeUpToIndex = 13
    };
} // namespace PhotonMatchmaking
