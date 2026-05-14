// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Subscription.h"

namespace PhotonCommon {
    class ScopedSubscription {
    public:
        ScopedSubscription() = default;
        ScopedSubscription(Subscription s);
        ~ScopedSubscription();

        ScopedSubscription(ScopedSubscription&& o) noexcept;
        ScopedSubscription& operator=(ScopedSubscription&& o) noexcept;
        ScopedSubscription(const ScopedSubscription&) = delete;
        ScopedSubscription& operator=(const ScopedSubscription&) = delete;

        Subscription        Release();
        const Subscription& Get() const;
        explicit operator bool()  const;

    private:
        Subscription sub;
    };
} // namespace PhotonCommon