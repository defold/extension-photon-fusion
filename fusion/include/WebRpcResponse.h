#pragma once

#include "PropertyValue.h"
#include "StringType.h"

namespace PhotonMatchmaking {
    struct WebRpcResponse {
        int resultCode = 0;
        PhotonCommon::StringType errorString;
        PhotonCommon::StringType uriPath;
        PropertyMap returnData;
    };
} // namespace PhotonMatchmaking
