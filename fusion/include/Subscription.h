// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace PhotonCommon {
    class Subscription {
    public:
        Subscription() = default;

        void Unsubscribe();
        bool IsSubscribed() const;
        explicit operator bool() const;

        void Block();
        void Unblock();
        bool IsBlocked() const;

    private:
        struct State {
            bool     subscribed = true;
            bool     blocked   = false;
            uint32_t slotIndex{};
            uint32_t generation{};
            std::function<void(uint32_t, uint32_t)> detachFn;
        };

        std::shared_ptr<State> state;

        explicit Subscription(std::shared_ptr<State> s);
        template <typename> friend class Broadcaster;
    };
} // namespace PhotonCommon