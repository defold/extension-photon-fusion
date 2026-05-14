// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "AuthenticationValues.h"
#include "StringType.h"
#include "ServerType.h"

namespace PhotonMatchmaking {
    struct ConnectOptions {
        AuthenticationValues auth;
        PhotonCommon::StringType username;
        ServerType serverType = ServerType::NameServer;
        PhotonCommon::StringType serverAddress;
        bool tryUseDatagramEncryption = false;
        bool useBackgroundSendReceiveThread = true;
    };
} // namespace PhotonMatchmaking
