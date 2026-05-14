// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "StringType.h"

#include <cstdint>
#include <unordered_map>
#include <variant>
#include <vector>

namespace PhotonMatchmaking {
    using PropertyValue = std::variant<
        bool,
        int8_t,
        int16_t,
        int32_t,
        int64_t,
        float,
        double,
        PhotonCommon::StringType,
        std::vector<uint8_t>,
        std::vector<int32_t>,
        std::vector<float>,
        std::vector<PhotonCommon::StringType>
    >;

    using PropertyMap = std::unordered_map<PhotonCommon::StringType, PropertyValue>;
} // namespace PhotonMatchmaking
