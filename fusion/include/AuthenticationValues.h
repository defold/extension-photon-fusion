// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CustomAuthenticationType.h"
#include "PropertyValue.h"
#include "StringType.h"

#include <cstdint>
#include <variant>
#include <vector>

namespace PhotonMatchmaking {
    struct AuthenticationValues {
        PhotonCommon::StringType userId;
        CustomAuthenticationType type = CustomAuthenticationType::None;
        PhotonCommon::StringType parameters;
        std::variant<std::monostate, std::vector<uint8_t>, PhotonCommon::StringType, PropertyMap> data;
    };
} // namespace PhotonMatchmaking
