// Copyright Exit Games GmbH. All Rights Reserved.

#ifndef SHAREDCLIENT_PHOTON_H
#define SHAREDCLIENT_PHOTON_H

#define CONCAT_WSTRING(a, b) (std::wstring(a) + std::wstring(b) + std::wstring(L"\n")).c_str()

#include "Misc.h"
#include "Common-cpp/inc/JString.h"
#include "LoadBalancing-cpp/inc/Client.h"
#include <functional>

namespace SharedMode {
	constexpr int PhotonClient_StatusNone = 0;
	constexpr int PhotonClient_StatusConnecting = 1;
	constexpr int PhotonClient_StatusError = 2;
	constexpr int PhotonClient_StatusDisconnected = 3;
	constexpr int PhotonClient_StatusConnected = 4;
	constexpr int PhotonClient_StatusJoiningRoom = 5;
	constexpr int PhotonClient_StatusInRoom = 6;

	class LBListener;

	class Photon {
		friend class Client;
		friend class LBListener;

		std::vector<std::function<void()> > OnJoinRoomCallbacks;
		std::vector<std::function<void()> > OnLeaveRoomCallbacks;

		std::function<void()> OnJoinRoomInternalCallback;
		std::function<void()> OnLeaveRoomInternalCallback;
		std::function<void()> OnDisconnectedCallback;

		std::function<void(int)> OnPlayerJoinedCallback;
		std::function<void(int)> OnPlayerLeftCallback;

		std::function<void(uint8_t, Data)> OnDataReceivedInternalCallback;

		LBListener *_lbListener{nullptr};
		ExitGames::LoadBalancing::Client *_lbClient{nullptr};

		int _status{0};
		const char *_region{nullptr};

		void OnPlayerJoined(int playerNr);

		void OnPlayerLeft(int playerNr);

		void OnDisconnected();

		void OnJoinedRoom();

		void OnLeaveRoom();

		void OnCustomEvent(int playerNr, nByte eventCode, const ExitGames::Common::Object &eventContent);

	public:
		Photon(const char *appId, const char *appVersion);

		~Photon();

		void Service(bool dispatch);

		void PushOnJoinedCallback(const std::function<void()> &callback);

		void PopOnJoinedCallback();

		void ConnectCloud(const char *region, const char *userId, const char* serverAddress);

		void ConnectLocal(const char *address);

		bool Disconnect();

		void JoinRoom(const char *room);

		void JoinRoomRandom();

		void JoinOrCreateRoom(const char *room,
		                      const ExitGames::LoadBalancing::RoomOptions &options =
				                      ExitGames::LoadBalancing::RoomOptions());

		void JoinOrCreateRoomRandom(const char *room,
		                            const ExitGames::LoadBalancing::RoomOptions &options =
				                            ExitGames::LoadBalancing::RoomOptions());

		void LeaveRoom();

		void CreateRoom(const char *room,
		                const ExitGames::LoadBalancing::RoomOptions &options = ExitGames::LoadBalancing::RoomOptions());

		void SendEvent(nByte code, nByte *data, size_t length, bool reliable);

		int32_t LocalPlayer();

		int32_t MasterClient();

		int Status() const { return _lbClient != nullptr ? _status : 0; }
		bool IsConnected() const { return _lbClient != nullptr && _status >= PhotonClient_StatusConnected; }
		bool IsJoiningOrInRoom() const { return _lbClient != nullptr && _status >= PhotonClient_StatusJoiningRoom; }
		bool IsInRoom() const { return _lbClient != nullptr && _status == PhotonClient_StatusInRoom; }
		ExitGames::LoadBalancing::Client &LoadBalancingClient() const { return *_lbClient; }

		void SetLogLevel(const int level) const { _lbClient->setDebugOutputLevel(level); }

		friend class LBListener;
	};
}

#endif

