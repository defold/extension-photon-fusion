// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "StringType.h"

namespace PhotonMatchmaking {
    struct FriendInfo {
        PhotonCommon::StringType userId;
        bool isOnline = false;
        PhotonCommon::StringType roomName;
        bool isInRoom = false;
    };
} // namespace PhotonMatchmaking
