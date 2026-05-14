// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Subscription.h"
#include <functional>
#include <vector>

namespace PhotonCommon {
    template <typename Signature>
    class Broadcaster;

    template <typename R, typename... Args>
    class Broadcaster<R(Args...)> {
    private:
        using Callback = std::function<R(Args...)>;

        struct Entry;
        struct Slot;

    public:
        Broadcaster() = default;
        ~Broadcaster() { *alive = false; }
        Broadcaster(const Broadcaster&) = delete;
        Broadcaster& operator=(const Broadcaster&) = delete;

        Subscription Subscribe(Callback cb) {
            std::shared_ptr<Subscription::State> state = std::make_shared<Subscription::State>();
            uint32_t si  = AllocSlot();
            uint32_t gen = nextGen++;
            uint32_t pi  = static_cast<uint32_t>(packed.size());

            slots[si]         = {pi, gen};
            state->slotIndex  = si;
            state->generation = gen;
            state->detachFn   = [this, prevent_dangling = this->alive](uint32_t s, uint32_t g) { if (*prevent_dangling) DoUnsubscribe(s, g); };

            packed.push_back({std::move(cb), state, si});
            return Subscription{state};
        }

        void Broadcast(Args... args) {
            ++dispatchDepth;

            const std::size_t count = packed.size();
            for (std::size_t i = 0; i < count; ++i) {
                Entry& e = packed[i];
                if (e.state->subscribed && !e.state->blocked) {
                    e.callback(args...);
                }
            }
            if (--dispatchDepth == 0) {
                for (std::function<void()>& op : deferred) {
                    op();
                }
                deferred.clear();
                Compact();
            }
        }

        void operator()(Args... args) { Broadcast(std::forward<Args>(args)...); }

        // Invokes all subscribers and returns true if any returned true. Intended
        // for predicate / "vote" patterns where R is bool. No subscribers -> false.
        bool BroadcastAny(Args... args) {
            static_assert(std::is_convertible_v<R, bool>, "BroadcastAny requires a bool-convertible return type");
            ++dispatchDepth;

            bool result = false;
            const std::size_t count = packed.size();
            for (std::size_t i = 0; i < count; ++i) {
                Entry& e = packed[i];
                if (e.state->subscribed && !e.state->blocked) {
                    if (e.callback(args...)) {
                        result = true;
                    }
                }
            }
            if (--dispatchDepth == 0) {
                for (std::function<void()>& op : deferred) {
                    op();
                }
                deferred.clear();
                Compact();
            }
            return result;
        }

        void UnsubscribeAll() {
            for (Entry& e : packed) {
                e.state->subscribed = false;
            }

            packed.clear();
            slots.clear();
            freeList.clear();
        }

        std::size_t SubscriberCount() const {
            return packed.size();
        }

        bool IsEmpty() const {
            return packed.empty();
        }

    private:
        struct Entry {
            Callback callback;
            std::shared_ptr<Subscription::State> state;
            uint32_t slotIndex = 0;
        };

        struct Slot {
            uint32_t packedIndex = 0;
            uint32_t generation = 0;
        };

        std::vector<Slot> slots;
        std::vector<Entry> packed;
        std::vector<uint32_t> freeList;
        uint32_t nextGen       = 1;
        int dispatchDepth = 0;
        std::vector<std::function<void()>> deferred;
        std::shared_ptr<bool> alive = std::make_shared<bool>(true);

        uint32_t AllocSlot() {
            if (!freeList.empty()) {
                uint32_t idx = freeList.back();
                freeList.pop_back();
                return idx;
            }
            slots.push_back({});
            return static_cast<uint32_t>(slots.size() - 1);
        }

        void DoUnsubscribe(uint32_t slotIdx, uint32_t gen) {
            std::function<void()> op = [this, slotIdx, gen] {
                if (slotIdx >= slots.size()) {
                    return;
                }
                Slot& slot = slots[slotIdx];
                if (slot.generation != gen) {
                    return;
                }
                packed[slot.packedIndex].state->subscribed = false;
            };
            if (dispatchDepth > 0) {
                deferred.push_back(std::move(op));
            }
            else {
                op();
            }
        }

        void Compact() {
            uint32_t writeIdx = 0;
            for (uint32_t readIdx = 0; readIdx < packed.size(); ++readIdx) {
                if (packed[readIdx].state->subscribed) {
                    if (writeIdx != readIdx) {
                        packed[writeIdx] = std::move(packed[readIdx]);
                    }
                    slots[packed[writeIdx].slotIndex].packedIndex = writeIdx;
                    ++writeIdx;
                }
            }
            packed.resize(writeIdx);
        }
    };
} // namespace PhotonCommon