// Copyright Exit Games GmbH. All Rights Reserved.


#ifndef SHAREDCLIENT_GAME_H
#define SHAREDCLIENT_GAME_H

#include "Notify.h"
#include "Misc.h"
#include "Types.h"
#include <string>

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
		Shutdown = 3
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
		bool _expectingEnd;

		bool _aoiUsed;
		int32_t _aoiCellSize;
		int32_t _aoiBoxesMax;
		std::unordered_map<std::string, InterestBox> _aoiLocations{};

		double _timeDiff{0};
		double _localClock{0};
		double _serverClock{0};
		double _serverClockScale{0};

		uint32_t _objectCounter{0};
		Tick _sendTick{0};
		double _sendClock{0};

		uint32_t _sceneSequence;
		Data _sceneData;

		json _config{};
		json _configLocal{};

		std::unordered_map<ObjectId, Object *> _objects{};
		std::unordered_map<ObjectId, ObjectRoot *> _objectsRoots{};

		Photon _photon;
		PhotonNotifyPlatform _photonPlatform;
		Notify::Connection *_connection{nullptr};

		WriteBuffer _rpcBuffer{};


		void OnJoinRoom();

		void OnLeaveRoom();

		void OnDataEvent(uint8_t code, Data data);

		void PacketLost(Notify::Channel &channel, void *user, Data data);

		void PacketDelivered(Notify::Channel &channel, void *user, Data data);

		void RpcPacketReceived(Data data);

		void DestroyObjectFromRemote(const ObjectRoot *obj, DestroyModes mode);

		void StatePacketReceived(Data data);

		void RpcInternal(const Rpc &rpc);

		void SceneChange(const Data &rpc);



		void PacketQueue();

		void PacketQueueRpc();

		bool WriteObjectHeader(Object *obj, WriteBuffer &writer, bool create);

		void CheckForMutatedState(const Object *obj, Tick tick);

		void PacketQueueState();

		bool WriteDirtyWords(const Object *obj, WriteBuffer &writer, Tick remoteTickAcked);
		void WriteEmptyStringHeap(WriteBuffer& writer);
		uint8_t WriteStringHeap(ObjectRoot* obj, WriteBuffer& writer, Tick remoteTickAcked,  Tick tick);

		void ServerTimeReceived(double serverTime);

		void SendRpcInternal(const Rpc &rpc);

		Object *AllocateObject(const TypeRef &type, size_t words, bool root);

		bool ReadObjectData(Object *obj, ReadBuffer &reader);

		bool ReadStringHeap(ObjectRoot *obj, ReadBuffer &reader, bool stringHeapEntriesChanged, bool stringHeapDataChanged);

		Object *ReadObjectHeader(ObjectId id, ReadBuffer &reader, bool create, PlayerId owner, bool root,
		                         bool allowCreate);

		ObjectTail &GetTail(const Object *obj) const;
		ObjectTail* GetTailPtr(const Object *obj) const;

		void SkipObjectData(ReadBuffer &reader);
		void SkipStringHeap(ReadBuffer& reader, bool stringHeapEntriesChanged, bool stringHeapDataChanged);

	public:
		ObjectRoot *GetRoot(Object *obj) const;
		const ObjectRoot *GetRoot(const Object *obj) const;

		double NetworkTimeDiff() const {  return _timeDiff; }

		bool DestroyObjectLocal(ObjectRoot *obj, bool engineObjectAlreadyDestroyed);

		bool AOIUsed() const { return _aoiUsed; }
		int32_t AOICellSize() const { return _aoiCellSize; }

		const std::unordered_map<std::string, InterestBox> &GetAllAreaOfInterestBoxes();

		InterestBox GetAreaOfInterestBox(const std::string &name);

		void SetAreaOfInterestBox(const std::string &name, const InterestBox &box);

		void RemoveAreaOfInterestBox(const std::string &name);

		InterestVector CalculateAreaOfInterestLocation(double x, double y, double z) const;

		bool IsRoot(const Object *object);

		void SetAreaOfInterestLocation(const Object *obj, InterestVector location);

		ObjectFlags SanitizeFlags(ObjectFlags flags) const;

		void SetWantOwner(Object *obj);

		void SetDontWantOwner(Object *obj);


		void ClearOwnerCooldown(Object *obj);

		std::unordered_map<ObjectId, Object *> &AllObjects() { return _objects; }
		std::unordered_map<ObjectId, ObjectRoot *> &AllRootObjects() { return _objectsRoots; }

		std::function<void()> OnRoomJoin;
		std::function<void()> OnRoomLeave;

		std::function<void(Rpc &)> OnRpc;
		std::function<void(uint32_t index, uint32_t sequence, Data &)> OnSceneChange;

		std::function<void(ObjectRoot *)> OnObjectOwnerChanged;
		std::function<void(ObjectRoot *)> OnObjectPredictionOverride;
		std::function<void(ObjectRoot *)> OnObjectCreated;
		std::function<void(ObjectChild *)> OnSubObjectCreated;
		std::function<void(const ObjectRoot *, DestroyModes)> OnObjectDestroyed;

		Client(const char *appId, const char *appVersion);

		~Client();

		json &Config() { return _config; }
		Photon &Photon() { return _photon; }

		static SdkVersion GetSdkVersion();

		bool IsRunning() const { return !_config.empty() && _photon.IsInRoom(); }

		bool IsMasterClient();

		PlayerId LocalPlayerId();

		Rpc CreateUserRpc(uint64_t id, PlayerId targetPlayer, ObjectId targetObject, uint64 DescriptorTypeHash,
		                  uint64 EventHash, const char *data, size_t dataLength);

		bool SendUserRpc(const Rpc &rpc);

		PlayerId GetOwner(const Object *obj);

		double GetTime(const Object *obj);

		bool HasBeenUpdatedByPlugin(Object *obj);

		double GetRtt() const;

		void ConnectCloud(const char *region, const char *userId, const char* serverAddress);

		void ConnectLocal(const char *endpoint);

		void UpdateFrameBegin(double dt);

		void UpdateFrameEnd();

		void UpdateSocketOnly();

		void Shutdown(bool development);

		void ChangeScene(uint32_t index, uint32_t sequence, const char *data);

		void StateUpdatesPause();

		void StateUpdatesResume();

		double NetworkTime() const;

		double NetworkTimeScale() const;

		bool IsOwner(const Object *obj);

		bool CanModify(const Object *obj);

		bool HasOwner(const Object *obj) const;

		void SetObjectPriority(ObjectId id, int32_t priority);

		Object *FindObject(ObjectId id) const;

		ObjectRoot *FindObjectRoot(ObjectId id) const;

		ObjectRoot *CreateSceneObject(bool &alreadyPopulated, size_t words, const TypeRef &type, const char *header,
		                          size_t headerLength, uint32_t scene, uint32_t id, ObjectFlags objectFlags);

		ObjectRoot *CreateGlobalInstanceObject(bool &alreadyPopulated, size_t words, const TypeRef &type, const char *header,
						  size_t headerLength, uint32_t scene, uint32_t id, ObjectFlags objectFlags);

		ObjectRoot *CreateObject(size_t words, const TypeRef &type, const char *header,
		                     size_t headerLength, uint32_t scene, ObjectFlags objectFlags);

		ObjectChild *CreateSubObject(ObjectId parent, size_t words, const TypeRef &type, const char *header,
		                        size_t headerLength, int32_t objectIndex, int32_t rootObjectIndex, uint32_t targetObjectHash, bool ignoreRootProperties);

		bool HasSubObjects(const Object *Root);

		const std::vector<ObjectId>& GetSubObject(const Object* Root);

		bool AddSubObject(ObjectRoot *ParentObject, ObjectChild *SubObject);

		friend class PhotonNotifyPlatform;

		template<typename T>
		T ConfigGetOrDefault(const std::string &name, T defaultValue = T()) {
			T value{};

			if (_config.contains(name)) {
				_config[name].get_to(value);
				return value;
			}

			return defaultValue;
		}
	};
}

#endif
