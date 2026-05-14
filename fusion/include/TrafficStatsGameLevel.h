// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstdint>

namespace PhotonMatchmaking {
    struct TrafficStatsGameLevel {
        int operationByteCount = 0;
        int operationCount = 0;
        int resultByteCount = 0;
        int resultCount = 0;
        int eventByteCount = 0;
        int eventCount = 0;
        int longestOpResponseCallbackMs = 0;
        uint8_t longestOpResponseCallbackOpCode = 0;
        int longestEventCallbackMs = 0;
        uint8_t longestEventCallbackCode = 0;
        int longestDeltaBetweenDispatchingMs = 0;
        int longestDeltaBetweenSendingMs = 0;
        int dispatchIncomingCommandsCalls = 0;
        int sendOutgoingCommandsCalls = 0;
        int totalByteCount = 0;
        int totalMessageCount = 0;
        int totalIncomingByteCount = 0;
        int totalIncomingMessageCount = 0;
        int totalOutgoingByteCount = 0;
        int totalOutgoingMessageCount = 0;
    };
} // namespace PhotonMatchmaking
