// Copyright Exit Games GmbH. All Rights Reserved.

#ifndef SHAREDCLIENT_ALIASES_H
#define SHAREDCLIENT_ALIASES_H

#include <cstdint>
#include <string>

namespace SharedMode {
	struct TypeRef {
		uint64_t Hash;
		uint32_t WordCount;
	};

	typedef uint32_t Tick;
	typedef int32_t PlayerId;

	typedef int32_t Word;

	constexpr PlayerId MasterClientPlayerId = -1;
	constexpr PlayerId PluginPlayerId = -2;
	constexpr PlayerId ObjectOwnerPlayerId = -3;

	struct ObjectId {
		PlayerId Origin{0};
		uint32_t Counter{0};

		ObjectId() = default;

		ObjectId(const PlayerId origin, const uint32_t counter) {
			Origin = origin;
			Counter = counter;
		}

		explicit ObjectId(const uint64_t &packed) {
			Origin = static_cast<PlayerId>(packed & UINT32_MAX);
			Counter = static_cast<uint32_t>(packed >> 32);
		}

		bool IsNone() const { return Origin == 0 && Counter == 0; }
		bool IsSome() const { return Origin != 0 || Counter != 0; }

		bool operator==(const ObjectId &other) const {
			return Origin == other.Origin && Counter == other.Counter;
		}

		bool operator!=(const ObjectId &other) const {
			return Origin != other.Origin || Counter != other.Counter;
		}

		operator std::wstring() const;

		operator std::string() const;

		operator uint64_t() const;
	};
}

#endif
