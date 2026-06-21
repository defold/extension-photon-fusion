// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>

#include "StringType.h"

namespace FusionCore
{
	enum class ForcedDisconnectReason : uint8_t
	{
		Generic              = 0,
		ProtocolIncompatible = 1,
		RoomVersionMismatch  = 2,
	};

	struct ParsedForcedDisconnect
	{
		PhotonCommon::StringType Message;
		ForcedDisconnectReason   Reason;
	};

	ParsedForcedDisconnect ParseForcedDisconnectPayload(const void* data, size_t length);
} // namespace FusionCore
