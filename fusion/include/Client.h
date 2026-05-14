// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Notify.h"
#include "Misc.h"
#include "Types.h"
#include "RealtimeClient.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <tuple>

#include "SubscriptionBag.h"

template<>
struct std::hash<FusionCore::ObjectId> {
	std::size_t operator()(const FusionCore::ObjectId &k) const noexcept {
		size_t hash = 17;
		hash = hash * 31 + k.Origin;
		hash = hash * 31 + k.Counter;
		return hash;
	}
};


namespace FusionCore {
	class Client;

	enum class DestroyModes {
		Local = 0,
		Remote = 1,
		MapChange = 2,
		Shutdown = 3,
		RejectedNotOwner = 4,
		ForceDestroy = 5
	};

	enum LogLevel : uint8_t {
		Trace = 1 << 0,
		Debug = 1 << 1,
		Info = 1 << 2,
		Warning = 1 << 3,
		Error = 1 << 4
	};

	class PhotonNotifyPlatform final : public Notify::Platform {
		Client *_game;
		Timer _timer{};

	public:
		explicit PhotonNotifyPlatform(Client *game) {
			_game = game;
			_timer.Start();
		}

		double Clock() override { return _timer.ElapsedSeconds(); }

		void Send(Notify::Connection *connection, Data data) override;
		void Recv(Notify::Connection *connection, Notify::Channel &channel, Data data) override;
		void Lost(Notify::Connection *connection, Notify::Channel &channel, void *user, Data data) override;
		void Delivered(Notify::Connection *connection, Notify::Channel &channel, void *user, Data data) override;
	};

	class Client {
		bool _expectingEnd{false};

		bool _configEmpty{true};
    bool _wasMasterClient{false};
		double _clientSendRate{30};
		double _authoritySendRate{30};
		double _dynamicOwnerCooldown{1.0 / 3};
		std::unordered_map<PlayerId, InterestKeySet> _playerInterestKeys{};

		double _timeDiff{0};
		double _localClock{0};
		double _serverClock{0};
		double _serverClockScale{0};

		uint32_t _objectCounter{0};
		Tick _sendTick{0};
		Tick _receiveCounter{0};
		double _sendClock{0};
		double _authoritySendClock{0};
    float _ignoreServerTimeTimeout{0};

    Map _mapCounter{0};
		std::unordered_map<Map, Data> _maps{};
		std::vector<ObjectId> _pendingDestroyedMapActors{};

		EMA _sendStats{0.5};
		EMA _recvStats{0.5};
		EMA _sendStateStats{0.5};
		EMA _recvStateStats{0.5};
		EMA _sendRpcStats{0.5};
		EMA _recvRpcStats{0.5};

		std::unordered_map<ObjectId, Object *> _objects{};
		std::unordered_map<ObjectId, ObjectRoot *> _objectsRoots{};

		PhotonMatchmaking::RealtimeClient* realtimeClient;
		PhotonCommon::SubscriptionBag realtimeSubscriptions;

		PhotonNotifyPlatform _photonPlatform;
		Notify::Connection *_connection{nullptr};

		uint32_t _rpcSequence{0};
		WriteBuffer _rpcBuffer{};
		WriteBuffer _unreliableRpcBuffer{};

		WriteBuffer _inputBuffer{};
		uint32_t _inputSequence{0};
		SimulationMode _simulationMode{SimulationMode::Shared};
		std::unordered_set<ObjectId> _predictedObjects{};

    Map GetMapCounterIncrement() {
      if (IsMasterClient()) {
        if (_wasMasterClient) {
          return 1;
        }

        _wasMasterClient = true;
        return 5;
      }

      return 0;
    }

		void OnDataEvent(uint8_t code, Data data);

		void SubscribeToRealtimeCallbacks();

		void PacketLost(Notify::Channel &channel, void *user, Data data);

		void PacketDelivered(Notify::Channel &channel, void *user, Data data);

		void RpcPacketReceived(Data data);
		void InputPacketReceived(Data data);

		void InputWritePending();
		void InputFlushToServer();

		void DestroyObjectFromRemote(const ObjectRoot *obj, DestroyModes mode);
		void DestroySubObjectFromRemote(ObjectChild *obj);
		void RemoveSubObjectFromParent(ObjectChild *obj);

		void StatePacketReceived(Data data);

		void RpcInternal(const Rpc &rpc);
		void MapChange(const Data &data, bool initial);
		void MapChangeDeserialize(const Data &data);
		void ApplyPendingDestroyedMapActors();

		void PacketQueue();
		void PacketQueueRpc();
		void PacketQueueUnreliableRpc();

		bool WriteObjectHeader(Object *obj, WriteBuffer &writer, bool create);
		bool WriteObjectRoot(WriteBuffer& writer, ObjectPacketEnvelope* envelope, ObjectRoot* root, bool force);

		bool CheckForMutatedState(const Object *obj, Tick tick);
		bool CheckMutatedStringHeap(Object* obj, Tick tick);
		void PacketQueueState();

		bool WriteDirtyWords(const Object *obj, WriteBuffer &writer, Tick remoteTickAcked);
		void WriteEmptyStringHeap(WriteBuffer& writer);
		uint8_t WriteStringHeap(Object* obj, WriteBuffer& writer, Tick remoteTickAcked, Tick tick);

		void ServerTimeReceived(double serverTime);
		void SendRpcInternal(Rpc& rpc);
		void SendRpcUnreliable(Rpc& rpc);
		Object *AllocateObject(const TypeRef &type, size_t words, bool root);

		bool ReadObjectData(Object *obj, ReadBuffer &reader);
		bool ReadStringHeap(Object *obj, ReadBuffer &reader, bool stringHeapEntriesChanged, bool stringHeapDataChanged);
		Object *ReadObjectHeader(ObjectId id, ReadBuffer &reader, bool create, PlayerId owner, bool root, bool allowCreate);

		static ObjectTail &GetTail(const Object *obj);
		static std::span<ObjectId> GetRequiredObjects(const Object *obj);

		void TryFireObjectReady(ObjectRoot *obj);

		void SkipObjectData(ReadBuffer &reader);
		void SkipStringHeap(ReadBuffer& reader, bool stringHeapEntriesChanged, bool stringHeapDataChanged);
		void SetInterestKey(Object *obj, uint64_t key);

    	int _localPlayerNumber{0};
		int _masterClientPlayerNumber{0};

	public:
		[[nodiscard]] EMAReport GetSendReport() const { return _sendStats.Report(); }
		[[nodiscard]] EMAReport GetRecvReport() const { return _recvStats.Report(); }
		[[nodiscard]] EMAReport GetSendStateReport() const { return _sendStateStats.Report(); }
		[[nodiscard]] EMAReport GetRecvStateReport() const { return _recvStateStats.Report(); }
		[[nodiscard]] EMAReport GetSendRpcReport() const { return _sendRpcStats.Report(); }
		[[nodiscard]] EMAReport GetRecvRpcReport() const { return _recvRpcStats.Report(); }
		
		int GetSendTick() const { return _sendTick; }
		int GetReceivedCounter() const { return _receiveCounter; }
		
		ObjectRoot *GetRoot(Object *obj) const;
		const ObjectRoot *GetRoot(const Object *obj) const;

		double NetworkTimeDiff() const {  return _timeDiff; }

		bool DestroyObjectLocal(ObjectRoot *obj, bool engineObjectAlreadyDestroyed);
		bool DestroySubObjectLocal(ObjectChild *obj);

		bool IsRoot(const Object *object);

		InterestKeySet& GetLocalInterestKeys();
		InterestKeySet& GetPlayerInterestKeys(PlayerId player);

		bool HasSetInterestKey(Object *obj);
		void ClearInterestKey(Object *obj);
		InterestKeyType GetInterestKeyType(Object *obj);

		void SetGlobalInterestKey(Object *obj);
		void SetUserInterestKey(Object *obj, uint64_t key);
		void SetAreaInterestKey(Object *obj, uint64_t key);

		ObjectOwnerModes SanitizeOwnerMode(ObjectOwnerModes ownerMode) const;

		void SetWantOwner(Object *obj);
		void SetDontWantOwner(Object *obj);
		void SetGiveAwayOwner(Object *obj, PlayerId player);
        void RejectOwnershipRequest(Object *obj);
        void ClearOwnerCooldown(Object *obj);

        ObjectId GetNewObjectId(Map map);

		std::unordered_map<ObjectId, Object *> &AllObjects() { return _objects; }
		std::unordered_map<ObjectId, ObjectRoot *> &AllRootObjects() { return _objectsRoots; }

		PhotonCommon::Broadcaster<void()> OnFusionStart;
		PhotonCommon::Broadcaster<void(std::string message)> OnForcedDisconnect;
		PhotonCommon::Broadcaster<void(Rpc &)> OnRpc;
		PhotonCommon::Broadcaster<void(Rpc &)> OnRpcError;
		PhotonCommon::Broadcaster<void(const std::unordered_map<Map, Data> &, bool)> OnMapChange;

		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnObjectOwnerChanged;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnObjectOwnerAssigned;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnPredictionOverride;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnObjectReady;
		PhotonCommon::Broadcaster<void(ObjectChild *)> OnSubObjectCreated;
		PhotonCommon::Broadcaster<void(const ObjectRoot *, DestroyModes)> OnObjectDestroyed;
		PhotonCommon::Broadcaster<void(ObjectChild *, DestroyModes)> OnSubObjectDestroyed;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnObjectForceAlive;
		PhotonCommon::Broadcaster<void(ObjectChild *)> OnSubObjectForceAlive;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnInterestEnter;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnInterestExit;
		PhotonCommon::Broadcaster<void(ObjectId)> OnDestroyedMapActor;
		PhotonCommon::Broadcaster<void(ObjectRoot*, uint32_t, float, Data, bool)> OnInput;
		PhotonCommon::Broadcaster<void(ObjectRoot*)> OnPredictionReset;
		// Called on the current owner when another player requests ownership.
		// Subscribers return true to grant, false to deny; the aggregate result
		// (any subscriber returning true) is auto-sent back to the requester as an
		// RPC_InternalOwnershipResponse and surfaces via OnOwnershipResponse.
		PhotonCommon::Broadcaster<bool(ObjectRoot*, PlayerId)> OnOwnershipRequest;
		PhotonCommon::Broadcaster<void(ObjectRoot*, bool)> OnOwnershipResponse;

		explicit Client(PhotonMatchmaking::RealtimeClient& realtimeClient);

		~Client();

		// Start the fusion client. Must be called after the integration has joined
		// a room — reads the "fusion_config" and "fusion_map_data" room properties
		// directly off RealtimeClient::GetCurrentRoom(), applies the config, applies
		// the initial map data, fires OnFusionStart, then sends the version
		// handshake to the server. Destroyed map actors arrive separately via
		// EVENT_CODE_DESTROYED_ACTORS after the version handshake completes.
		// Runtime map state updates flow via the RPC_InternalMapChange path.
		// Returns true on success, false on precondition failure (no room joined,
		// required room properties missing or malformed) — see PHOTON_LOG_ERROR
		// output for the specific cause.
		bool Start();
		void Stop();

		PhotonMatchmaking::RealtimeClient& GetRealtimeClient() { return *realtimeClient; }

		static SdkVersion GetSdkVersion();

		bool IsRunning() const { return _connection != nullptr && !_configEmpty; }

		bool IsMasterClient() const;
		PlayerId LocalPlayerId() const;

		Rpc CreateUserRpc(uint64_t id, PlayerId targetPlayer, ObjectId targetObject,
		                  uint64_t EventHash, const char *data, size_t dataLength);

		bool SendUserRpc(Rpc& rpc);

		PlayerId GetOwner(const Object *obj);
		double GetTime(const Object *obj);
		bool HasBeenUpdatedByPlugin(Object *obj, bool reset);
		double GetRtt() const;

        void RefreshRoomCache();

        void UpdateFrameBegin(double dt);
		void UpdateFrameEnd();
		void UpdateServiceOnly();
		void Shutdown();

		Map MapChange(const PhotonCommon::CharType* data);
		Map MapAdd(const PhotonCommon::CharType* data);
		void MapRemove(Map map);

		bool MapIsValid(Map map) const { return _maps.find(map) != _maps.end(); }

		const std::unordered_map<Map, Data> &GetMaps() const { return _maps; }

		void StateUpdatesPause();
		void StateUpdatesResume();

		double NetworkTime() const;
		double NetworkTimeScale() const;

		bool IsOwner(const Object *obj);
		bool CanModify(const Object *obj);
		bool HasOwner(const Object *obj) const;

		SimulationMode GetSimulationMode() const { return _simulationMode; }
		PlayerId GetPredictingPlayer(const Object *obj) const;
		void SetPredictingPlayer(ObjectRoot *obj, PlayerId player);
		bool IsPredictingPlayer(const Object *obj) const;
		bool HasInputAuthority(const Object *obj) const;
		uint32_t GetInputSequence(const Object *obj) const;
		void SetInputSequence(ObjectRoot *obj, uint32_t sequence);
		uint32_t NextInputSequence() { return ++_inputSequence; }

		int32_t PlayerCount() const;

		void SetDynamicOwnerCooldown(double seconds) { _dynamicOwnerCooldown = seconds; }
		double GetDynamicOwnerCooldown() const { return _dynamicOwnerCooldown; }

		void SetAuthoritySendRate(double rate) { _authoritySendRate = rate; }
		double GetAuthoritySendRate() const { return _authoritySendRate; }

    void SetLocalSendRate(Object *obj, uint32_t sendRate);
		void SetRoomSendRate(const Object *obj, int32_t sendRate);
    
    void ResetLocalSendRate(Object *obj);
	  void ResetRoomSendRate(const Object *obj);

		Object *FindObject(ObjectId id, const bool allowDestroyed = false) const;
		ObjectRoot *FindObjectRoot(ObjectId id) const;

		Object* FindSubObjectWithHash(ObjectRoot* Root, uint32_t subObjectHash) const;


		ObjectRoot *CreateMapObject(bool &alreadyPopulated, const size_t words, const TypeRef &type, const PhotonCommon::CharType* header,
						  size_t headerLength, const Map map, const uint32_t id, ObjectOwnerModes ownerMode, uint32_t engineFlags, int32_t requiredObjectsCount);

		ObjectRoot *CreateGlobalInstanceObject(bool &alreadyPopulated, size_t words, const TypeRef &type, const PhotonCommon::CharType* header,
						  size_t headerLength, Map map, uint32_t id, ObjectOwnerModes ownerMode, uint32_t engineFlags, int32_t requiredObjectsCount = 0);

		ObjectRoot *CreateObject(size_t words, const TypeRef &type, const PhotonCommon::CharType* header,
		                     size_t headerLength, Map map, ObjectOwnerModes ownerMode, uint32_t engineFlags, int32_t requiredObjectsCount = 0, ObjectId preconfiguredId = ObjectId());

		ObjectChild *CreateSubObject(ObjectId parent, size_t words, const TypeRef &type, const PhotonCommon::CharType* header,
		                        size_t headerLength, uint64_t engineHash, ObjectId id, uint32_t engineFlags);


		const std::vector<ObjectId>& GetSubObject(const Object* Root);
		bool HasSubObjects(const Object *Root);
		bool AddSubObject(ObjectRoot *ParentObject, ObjectChild *SubObject);

		friend class PhotonNotifyPlatform;
		friend class ObjectRoot;
	};
}

#include "SharedModeCompat.h"
