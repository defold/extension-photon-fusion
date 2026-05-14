// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "StringType.h"

#include <vector>

namespace PhotonMatchmaking {
    struct JoinRoomOptions {
        bool rejoin = false;
        int cacheSliceIndex = 0;
        std::vector<PhotonCommon::StringType> expectedUsers;
    };
} // namespace PhotonMatchmaking
