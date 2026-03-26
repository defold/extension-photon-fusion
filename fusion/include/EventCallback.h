#pragma once

#include <cstdint>
#include <functional>
#include "SpanCompat.h"

namespace PhotonMatchmaking {
    using EventCallback = std::function<void(uint8_t event_code, int sender_id, std::span<const uint8_t> data)>;
} // namespace PhotonMatchmaking
