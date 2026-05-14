// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "ScopedSubscription.h"
#include <vector>

namespace PhotonCommon {
    class SubscriptionBag {
    public:
        SubscriptionBag& operator+=(Subscription s);

        void        UnsubscribeAll();
        std::size_t Count()   const;
        bool        IsEmpty() const;
    private:
        std::vector<ScopedSubscription> bag;
    };
} // namespace PhotonCommon