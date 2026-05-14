// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "StringType.h"

#include <cstdint>

namespace PhotonMatchmaking {
    struct NetworkStats {
        int roundTripTimeMs = 0;
        int rttVarianceMs = 0;
        int bytesIn = 0;
        int bytesOut = 0;
        int bytesCurrentDispatch = 0;
        int bytesLastOperation = 0;
        int queuedIncomingCommands = 0;
        int queuedOutgoingCommands = 0;
        int incomingReliableCommands = 0;
        int resentReliableCommands = 0;
        int playersInGame = 0;
        int gamesRunning = 0;
        int playersOnline = 0;
        int serverTimeOffsetMs = 0;
        int serverTimeMs = 0;
        int timestampLastReceive = 0;
        int packetLossByCrc = 0;
        int sentCountAllowance = 0;
        bool encryptionAvailable = false;
        bool payloadEncryptionAvailable = false;
        short peerId = 0;
        int disconnectTimeoutMs = 0;
        int pingIntervalMs = 0;
        int trafficStatsElapsedMs = 0;
        PhotonCommon::StringType masterServerAddress;
        int channelCountUserChannels = 0;
        uint8_t serializationProtocol = 0;
        short peerCount = 0;
    };
} // namespace PhotonMatchmaking
