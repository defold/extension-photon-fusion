#pragma once

#include "StringType.h"

namespace PhotonMatchmaking {
    struct RegionInfo {
        PhotonCommon::StringType code;
        PhotonCommon::StringType server;
        int pingMs = -1;
    };
} // namespace PhotonMatchmaking
