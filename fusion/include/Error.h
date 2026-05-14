// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "StringType.h"

namespace PhotonCommon {
    enum class ErrorCode : int {
        Ok = 0,
        Unknown = -1,
    };

    template<typename CodeT = ErrorCode>
    struct Error {
        CodeT code = CodeT::Unknown;
        StringType message;
    };
} // namespace PhotonCommon
