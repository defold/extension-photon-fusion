// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

// Backwards compatibility: maps the old SharedMode:: namespace to FusionCore::.
// This header is automatically included by all public headers.
// Define FUSIONCORE_NO_DEPRECATED_NAMESPACE before any includes to disable the alias entirely.

#ifndef FUSIONCORE_NO_DEPRECATED_NAMESPACE

namespace FusionCore::Notify {}

namespace SharedMode {
    using namespace FusionCore;

    namespace Notify {
        using namespace FusionCore::Notify;
    }
}

#endif
