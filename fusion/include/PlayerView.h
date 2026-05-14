// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "PropertyValue.h"
#include "StringType.h"

namespace PhotonMatchmaking {
    struct PlayerView {
        int number = 0;
        PhotonCommon::StringType name;
        PhotonCommon::StringType userId;
        PropertyMap customProperties;
        bool isInactive = false;
        bool isMasterClient = false;
    };
} // namespace PhotonMatchmaking
