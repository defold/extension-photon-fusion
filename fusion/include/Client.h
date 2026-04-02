// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#ifndef SHAREDCLIENT_GAME_H
#define SHAREDCLIENT_GAME_H

#include "Notify.h"
#include "Misc.h"
#include "Types.h"
#include "RealtimeClient.h"

#include <string>
#include <unordered_map>
#include <map>
#include <set>
#include <tuple>

#include "SubscriptionBag.h"

template<>
struct std::hash<SharedMode::ObjectId> {
	std::size_t operator()(const SharedMode::ObjectId &k) const noexcept {
		size_t hash = 17;
		hash = hash * 31 + k.Origin;
		hash = hash * 31 + k.Counter;
		return hash;
	}
};


namespace SharedMode {
	class Client;

	enum class DestroyModes {
		Local = 0,
		Remote = 1,
		SceneChange = 2,
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
		double _clientSendRate{30};
		std::map<uint64_t, uint8_t> _interestKeys{};

		double _timeDiff{0};
		double _localClock{0};
		double _serverClock{0};
		double _serverClockScale{0};

		uint32_t _objectCounter{0};
		Tick _sendTick{0};
		Tick _receiveCounter{0};
		double _sendClock{0};

		uint32_t _sceneSequence;
		Data _sceneData;

		struct PendingDestroyedMapActors {
			uint32_t SceneIndex{0};
			uint32_t SceneSequence{0};
			std::vector<ObjectId> Ids{};
		};
		PendingDestroyedMapActors _pendingDestroyedMapActors{};

		std::unordered_map<ObjectId, Object *> _objects{};
		std::unordered_map<ObjectId, ObjectRoot *> _objectsRoots{};

		PhotonMatchmaking::RealtimeClient* realtimeClient;
		PhotonCommon::SubscriptionBag realtimeSubscriptions;

		PhotonNotifyPlatform _photonPlatform;
		Notify::Connection *_connection{nullptr};

		WriteBuffer _rpcBuffer{};

		void OnDataEvent(uint8_t code, Data data);

		void SubscribeToRealtimeCallbacks();

		void PacketLost(Notify::Channel &channel, void *user, Data data);

		void PacketDelivered(Notify::Channel &channel, void *user, Data data);

		void RpcPacketReceived(Data data);

		void DestroyObjectFromRemote(const ObjectRoot *obj, DestroyModes mode);
		void DestroySubObjectFromRemote(ObjectChild *obj);
		void RemoveSubObjectFromParent(ObjectChild *obj);

		void StatePacketReceived(Data data);

		void RpcInternal(const Rpc &rpc);

		void SceneChange(const Data &rpc);

		void ApplyPendingDestroyedMapActors();

		void PacketQueue();

		void PacketQueueRpc();

		bool WriteObjectHeader(Object *obj, WriteBuffer &writer, bool create);

		void CheckForMutatedState(const Object *obj, Tick tick);

		bool WriteObjectRoot(WriteBuffer& writer, ObjectPacketEnvelope* envelope, ObjectRoot* root, bool force);

		void PacketQueueState();

		bool WriteDirtyWords(const Object *obj, WriteBuffer &writer, Tick remoteTickAcked);
		void WriteEmptyStringHeap(WriteBuffer& writer);
		uint8_t WriteStringHeap(Object* obj, WriteBuffer& writer, Tick remoteTickAcked, Tick tick);

		void ServerTimeReceived(double serverTime);

		void SendRpcInternal(const Rpc &rpc);

		Object *AllocateObject(const TypeRef &type, size_t words, bool root);

		bool ReadObjectData(Object *obj, ReadBuffer &reader);

		bool ReadStringHeap(Object *obj, ReadBuffer &reader, bool stringHeapEntriesChanged, bool stringHeapDataChanged);

		Object *ReadObjectHeader(ObjectId id, ReadBuffer &reader, bool create, PlayerId owner, bool root,
		                         bool allowCreate);

		static ObjectTail &GetTail(const Object *obj);
		static ObjectId *GetRequiredObjects(const Object *obj);

		void TryFireObjectReady(ObjectRoot *obj);

		void SkipObjectData(ReadBuffer &reader);
		void SkipStringHeap(ReadBuffer& reader, bool stringHeapEntriesChanged, bool stringHeapDataChanged);

		void SetInterestKey(Object *obj, uint64_t key);

	public:
		int GetSendTick() const { return _sendTick; }
		int GetReceivedCounter() const { return _receiveCounter; }
		ObjectRoot *GetRoot(Object *obj) const;
		const ObjectRoot *GetRoot(const Object *obj) const;

		double NetworkTimeDiff() const {  return _timeDiff; }

		bool DestroyObjectLocal(ObjectRoot *obj, bool engineObjectAlreadyDestroyed);
		bool DestroySubObjectLocal(ObjectChild *obj);

		std::map<uint64_t, uint8_t>& GetInterestKeys();

		bool IsRoot(const Object *object);

		bool HasSetInterestKey(Object *obj);
		void ClearInterestKey(Object *obj);

		void SetGlobalInterestKey(Object *obj);
		void SetUserInterestKey(Object *obj, uint64_t key);
		void SetAreaInterestKey(Object *obj, uint64_t key);
		
		InterestKeyType GetInterestKeyType(Object *obj);

		void ClearAllKeys();
		void ClearAreaKeys();
		void ClearUserKeys();
		void SetAreaKeys(const std::vector<std::tuple<uint64_t, uint8_t>>& keys);
		void AddUserKey(uint64_t key, uint8_t sendRate = 0);
		void RemoveUserKey(uint64_t key);
		std::vector<std::tuple<uint64_t, uint8_t>> GetAllAreaKeys() const;
		std::vector<std::tuple<uint64_t, uint8_t>> GetAllUserKeys() const;

		ObjectOwnerModes SanitizeOwnerMode(ObjectOwnerModes ownerMode) const;

		void SetLocalSendRate(Object *obj, uint32_t sendRate);

		void SetWantOwner(Object *obj);

		void SetDontWantOwner(Object *obj);

		ObjectId GetNewObjectId();

		void ClearOwnerCooldown(Object *obj);

		std::unordered_map<ObjectId, Object *> &AllObjects() { return _objects; }
		std::unordered_map<ObjectId, ObjectRoot *> &AllRootObjects() { return _objectsRoots; }

		PhotonCommon::Broadcaster<void()> OnFusionStart;
		PhotonCommon::Broadcaster<void(std::string message)> OnForcedDisconnect;
		PhotonCommon::Broadcaster<void(Rpc &)> OnRpc;
		PhotonCommon::Broadcaster<void(uint32_t index, uint32_t sequence, Data &)> OnSceneChange;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnObjectOwnerChanged;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnOwnerWasGiven;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnObjectPredictionOverride;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnObjectReady;
		PhotonCommon::Broadcaster<void(ObjectChild *)> OnSubObjectCreated;
		PhotonCommon::Broadcaster<void(const ObjectRoot *, DestroyModes)> OnObjectDestroyed;
		PhotonCommon::Broadcaster<void(ObjectChild *, DestroyModes)> OnSubObjectDestroyed;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnInterestEnter;
		PhotonCommon::Broadcaster<void(ObjectRoot *)> OnInterestExit;
		PhotonCommon::Broadcaster<void(uint32_t, ObjectId)> OnDestroyedMapActor;

		explicit Client(PhotonMatchmaking::RealtimeClient& realtimeClient);

		~Client();

		void Start();
		void Stop();

		PhotonMatchmaking::RealtimeClient& GetRealtimeClient() { return *realtimeClient; }

		static SdkVersion GetSdkVersion();

		bool IsRunning() const { return _connection != nullptr && !_configEmpty; }

		bool IsMasterClient();

		PlayerId LocalPlayerId();

		Rpc CreateUserRpc(uint64_t id, PlayerId targetPlayer, ObjectId targetObject, uint64_t DescriptorTypeHash,
		                  uint64_t EventHash, const char *data, size_t dataLength);

		bool SendUserRpc(const Rpc &rpc);

		PlayerId GetOwner(const Object *obj);

		double GetTime(const Object *obj);

		bool HasBeenUpdatedByPlugin(Object *obj, bool reset);

		double GetRtt() const;

		void UpdateFrameBegin(double dt);

		void UpdateFrameEnd();

		void UpdateServiceOnly();

		void Shutdown();

		void ChangeScene(uint32_t index, uint32_t sequence, const PhotonCommon::CharType* data);

		void StateUpdatesPause();

		void StateUpdatesResume();

		double NetworkTime() const;

		double NetworkTimeScale() const;

		bool IsOwner(const Object *obj);

		bool CanModify(const Object *obj);

		bool HasOwner(const Object *obj) const;

		int32_t PlayerCount() const;

		void SetSendRate(const Object *obj, int32_t sendRate);
	  void ResetSendRate(const Object *obj);

		Object *FindObject(ObjectId id) const;

		ObjectRoot *FindObjectRoot(ObjectId id) const;

		Object* FindSubObjectWithHash(ObjectRoot* Root, uint32_t subObjectHash) const;

		ObjectRoot *CreateSceneObject(bool &alreadyPopulated, size_t words, const TypeRef &type, const PhotonCommon::CharType* header,
		                          size_t headerLength, uint32_t scene, uint32_t id, ObjectOwnerModes ownerMode, ObjectSpecialFlags SpecialFlags, int32_t requiredObjectsCount = 0);

		ObjectRoot *CreateGlobalInstanceObject(bool &alreadyPopulated, size_t words, const TypeRef &type, const PhotonCommon::CharType* header,
						  size_t headerLength, uint32_t scene, uint32_t id, ObjectOwnerModes ownerMode, ObjectSpecialFlags SpecialFlags, int32_t requiredObjectsCount = 0);

		ObjectRoot *CreateObject(size_t words, const TypeRef &type, const PhotonCommon::CharType* header,
		                     size_t headerLength, uint32_t scene, ObjectOwnerModes ownerMode, ObjectSpecialFlags SpecialFlags, int32_t requiredObjectsCount = 0, ObjectId preconfiguredId = ObjectId());

		ObjectChild *CreateSubObject(ObjectId parent, size_t words, const TypeRef &type, const PhotonCommon::CharType* header,
		                        size_t headerLength, uint32_t targetObjectHash, ObjectId id, ObjectSpecialFlags SpecialFlags);

		bool HasSubObjects(const Object *Root);

		const std::vector<ObjectId>& GetSubObject(const Object* Root);

		bool AddSubObject(ObjectRoot *ParentObject, ObjectChild *SubObject);

		friend class PhotonNotifyPlatform;
		friend class ObjectRoot;
	};
}

#endif
