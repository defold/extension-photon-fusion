// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Misc.h"
#include <cstdint>
#include <optional>

namespace FusionCore {
	class Client;
}

namespace FusionCore::Notify {
	constexpr int RECV_RESULT_NONE = 0;
	constexpr int RECV_RESULT_DISCONNECT = 2;

	constexpr int DEFAULT_HEADERS = 40 + 8 + 96;
	constexpr int MAX_MTU_BYTES_TOTAL = 1280;
	constexpr int MAX_MTU_BYTES_PAYLOAD = MAX_MTU_BYTES_TOTAL - DEFAULT_HEADERS;
	constexpr int PACKET_MTU_BYTES = MAX_MTU_BYTES_PAYLOAD / 8 * 8;

	constexpr uint8_t NOTIFY_VERSION = 1;

	constexpr uint8_t FRAG_FLAG_DATA = 1 << 1;
	constexpr uint8_t FRAG_FLAG_ACKS = 1 << 2;
	constexpr uint8_t FRAG_FLAG_LAST_FRAG = 1 << 7;

	class Platform;

	struct FragmentHeader {
		uint8_t Flags;
		uint8_t Version;
		uint8_t Channel;
		uint8_t _reserved_1;

		uint16_t Sequence;
		uint16_t AckSequence;
		uint64_t AckMask;

		uint32_t FragGroup;
		uint32_t FragIndex;
	};

	static_assert(offsetof(FragmentHeader, Flags) == 0);
	static_assert(offsetof(FragmentHeader, Version) == 1);
	static_assert(offsetof(FragmentHeader, Channel) == 2);
	static_assert(offsetof(FragmentHeader, Sequence) == 4);
	static_assert(offsetof(FragmentHeader, AckSequence) == 6);
	static_assert(offsetof(FragmentHeader, AckMask) == 8);
	static_assert(offsetof(FragmentHeader, FragGroup) == 16);
	static_assert(offsetof(FragmentHeader, FragIndex) == 20);

	static_assert(sizeof(FragmentHeader) == 24);

	struct Fragment;

	struct FragmentGroup {
		FragmentGroup *Prev{nullptr};
		FragmentGroup *Next{nullptr};

		void *User{nullptr};
		FusionCore::Data Data{};

		std::optional<bool> WasLost{};
		std::optional<bool> WasDelivered{};

		uint32_t Group{0};
		uint32_t Count{0};

		uint8_t *Delivered{nullptr};
		uint32_t DeliveredCount{0};

		FragmentGroup() = default;
		~FragmentGroup();

		bool IsDone() const { return WasLost.has_value() || WasDelivered.has_value(); }

		void SetDelivered(Fragment *fragment);

		FragmentGroup(const FragmentGroup &) = delete;

		FragmentGroup &operator=(const FragmentGroup &) = delete;
	};

	struct Fragment {
		Fragment *Prev{nullptr};
		Fragment *Next{nullptr};
		FragmentGroup *Group{nullptr};

		double SendTime{0};

		FragmentHeader Header{};
		FusionCore::Data Data{};

		Fragment() = default;
		Fragment(const Fragment &) = delete;

		Fragment &operator=(const Fragment &) = delete;
	};

	struct Channel {
		uint8_t Id{0};
		bool Reliable{false};

		size_t MtuData{PACKET_MTU_BYTES - sizeof(FragmentHeader)};

		LinkList<FragmentGroup> Notify{};

		uint32_t SendGroup{0};
		LinkList<Fragment> SendQueue{};
		LinkList<Fragment> ResendQueue{};

		uint32_t RecvGroup{0};

		LinkList<Fragment> RecvList{};

		Channel(const uint8_t id, const bool reliable) : Id(id), Reliable(reliable) { }
		Channel(const Channel &) = delete;

		Channel &operator=(const Channel &) = delete;
	};

	class Connection {
		friend Client;

		double _rtt{0};

		Platform &_platform;

		uint16_t _recvSequence{0};
		uint64_t _recvMask{0};

		uint16_t _sendSequence{0};
		LinkList<Fragment> _sendWindow{};

		void DeliverChannel(Channel &chan);

		void NotifyChannel(Channel &chan);

		int SendChannel(Channel &chan);

		void SendFragment(Fragment *frag);

		int Ack(FragmentHeader header);

		void Ack(FragmentHeader header, Fragment *fragment, int16_t distance, Channel &chan);

		int ReceiveAcksOnly(FragmentHeader header);

		bool ValidChannel(uint8_t id) const;

		void ReliableLost(Channel &chan, Fragment *frag);

		void UnreliableLost(Channel &chan, Fragment *frag);

		void ReliableDelivered(Channel &chan, Fragment *frag);

		void UnreliableDelivered(Channel &chan, Fragment *frag);

	public:
		explicit Connection(Platform &platform);

		Connection(const Connection &) = delete;

		Connection &operator=(const Connection &) = delete;

		Channel Game{1, false};
		Channel Streaming{2, true};
		Channel Unreliable{3, false};

		bool CanQueue(Channel &chan);

		bool Queue(Channel &chan, Data data, void *user);

		void Send();

		void Update();

		int Receive(Data data);

		int Receive(Channel &chan, FragmentHeader header, Data data);
	};

	class Platform {
	public:
		virtual ~Platform() = default;

		virtual double Clock() = 0;

		virtual void Send(Connection *connection, Data data) = 0;

		virtual void Recv(Connection *connection, Channel &channel, Data data) = 0;

		virtual void Lost(Connection *connection, Channel &chanel, void *user, Data data) = 0;

		virtual void Delivered(Connection *connection, Channel &chanel, void *user, Data data) = 0;
	};
}

#include "SharedModeCompat.h"
