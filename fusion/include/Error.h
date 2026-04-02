// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "ErrorCode.h"
#include "StringType.h"

namespace PhotonMatchmaking {
    struct Error {
        ErrorCode code = ErrorCode::Unknown;
        PhotonCommon::StringType message;
    };
} // namespace PhotonMatchmaking
