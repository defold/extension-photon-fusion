#pragma once

namespace PhotonMatchmaking {
    enum class DisconnectCause : int {
        None = 0,
        DisconnectByServerUserLimit = 1,
        ExceptionOnConnect = 2,
        DisconnectByServer = 3,
        DisconnectByServerLogic = 4,
        TimeoutDisconnect = 5,
        Exception = 6,
        InvalidAuthentication = 7,
        MaxCCUReached = 8,
        InvalidRegion = 9,
        OperationNotAllowedInCurrentState = 10,
        CustomAuthenticationFailed = 11,
        ClientVersionTooOld = 12,
        ClientVersionInvalid = 13,
        DashboardVersionInvalid = 14,
        AuthenticationTicketExpired = 15,
        DisconnectByOperationLimit = 16
    };
} // namespace PhotonMatchmaking
