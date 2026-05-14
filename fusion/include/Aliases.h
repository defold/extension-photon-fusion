// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include "StringType.h"

namespace FusionCore {
	struct TypeRef {
		uint64_t Hash;
		uint32_t WordCount;
	};

	typedef uint32_t Tick;
	typedef uint16_t PlayerId;
	typedef uint16_t Map;

	typedef int32_t Word;

	constexpr PlayerId MasterClientPlayerId = 0xFFFF;     // UINT16_MAX
	constexpr PlayerId PluginPlayerId = 0xFFFF - 1;       // UINT16_MAX - 1
	constexpr PlayerId ObjectOwnerPlayerId = 0xFFFF - 2;  // UINT16_MAX - 2

	struct ObjectId {
		static constexpr size_t WordSize = 2;

		PlayerId Origin{0};
		FusionCore::Map Map{0};
		uint32_t Counter{0};

		ObjectId() = default;

		ObjectId(const PlayerId origin, const FusionCore::Map map, const uint32_t counter) {
			Origin = origin;
			Map = map;
			Counter = counter;
		}

		explicit ObjectId(const uint64_t &packed) {
			Origin = static_cast<PlayerId>(packed & UINT16_MAX);
			Map = static_cast<FusionCore::Map>((packed >> 16) & UINT16_MAX);
			Counter = static_cast<uint32_t>(packed >> 32);
		}

		bool IsNone() const { return Origin == 0 && Counter == 0 && Map == 0; }
		bool IsSome() const { return Origin != 0 || Counter != 0 || Map != 0; }

		bool operator==(const ObjectId &other) const {
			return Origin == other.Origin && Map == other.Map && Counter == other.Counter;
		}

		bool operator!=(const ObjectId &other) const {
			return Origin != other.Origin || Map != other.Map || Counter != other.Counter;
		}

		operator PhotonCommon::StringType() const;

		operator uint64_t() const;
	};

	static_assert(sizeof(ObjectId) == 8);
	static_assert(offsetof(ObjectId, Origin) == 0);
	static_assert(offsetof(ObjectId, Map) == 2);
	static_assert(offsetof(ObjectId, Counter) == 4);

	inline PhotonCommon::StringType to_string_type(ObjectId id) {
	    return id;
	}
}

#include "SharedModeCompat.h"
