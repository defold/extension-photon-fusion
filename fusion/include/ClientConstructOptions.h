// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "ConnectionProtocol.h"
#include "RegionSelectionMode.h"
#include "SerializationProtocol.h"
#include "StringType.h"

#include <cstdint>
#include <optional>

namespace PhotonMatchmaking {
    struct ClientConstructOptions {
        PhotonCommon::StringType appId;
        PhotonCommon::StringType appVersion;

        ConnectionProtocol protocol = ConnectionProtocol::Default;
        bool useAlternativePorts = false;

        RegionSelectionMode regionSelectionMode = RegionSelectionMode::Default;

        bool autoLobbyStats = false;
        SerializationProtocol serialization = SerializationProtocol::Protocol1_8;

        std::optional<int> disconnectTimeoutMs;
        std::optional<int> pingIntervalMs;
        std::optional<bool> enableCrc;
        std::optional<int> sentCountAllowance;
        std::optional<uint8_t> quickResendAttempts;
        std::optional<int> limitOfUnreliableCommands;
    };
} // namespace PhotonMatchmaking
