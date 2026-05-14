// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Error.h"
#include "Result.h"
#include "Task.h"

namespace PhotonMatchmaking {
    enum class ErrorCode : int {
        Ok = 0,
        Unknown = -1,

        ConnectionFailed = 1,
        Disconnected = 2,
        Timeout = 3,
        ServerFull = 4,

        InvalidAuthentication = 10,
        CustomAuthenticationFailed = 11,
        AuthenticationTicketExpired = 12,
        MaxCCUReached = 13,
        InvalidRegion = 14,
        ClientVersionTooOld = 15,

        RoomFull = 20,
        RoomClosed = 21,
        RoomNotFound = 22,
        RoomAlreadyExists = 23,
        NoMatchFound = 24,
        AlreadyJoined = 25,
        SlotError = 26,

        OperationDenied = 30,
        OperationInvalid = 31,
        OperationLimitReached = 32,
        RateLimited = 33,

        InternalServerError = 40,
        PluginError = 41,
        ServerDisconnect = 42,

        NotConnected = 50,
        NotInRoom = 51,
        InvalidState = 52,
    };

    template<typename T = void>
    using Result = PhotonCommon::Result<T, ErrorCode>;

    template<typename T = void>
    using Task = PhotonCommon::Task<T>;

    using Error = PhotonCommon::Error<ErrorCode>;
} // namespace PhotonMatchmaking
