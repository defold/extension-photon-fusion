#ifndef FUSION_SDK_H
#define FUSION_SDK_H

#include <dmsdk/sdk.h>

#if defined(DM_PLATFORM_LINUX)
// The SDK includes X11 headers whose macros conflict with Photon identifiers.
#undef Bool
#undef None
#endif

#endif
