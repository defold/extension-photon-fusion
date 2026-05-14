// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

namespace PhotonMatchmaking {
    struct TrafficStats {
        int packageHeaderSize = 0;
        int reliableCommandCount = 0;
        int unreliableCommandCount = 0;
        int fragmentCommandCount = 0;
        int controlCommandCount = 0;
        int totalPacketCount = 0;
        int totalCommandsInPackets = 0;
        int reliableCommandBytes = 0;
        int unreliableCommandBytes = 0;
        int fragmentCommandBytes = 0;
        int controlCommandBytes = 0;
        int totalCommandCount = 0;
        int totalCommandBytes = 0;
        int totalPacketBytes = 0;
        int timestampOfLastAck = 0;
        int timestampOfLastReliableCommand = 0;
    };
} // namespace PhotonMatchmaking
