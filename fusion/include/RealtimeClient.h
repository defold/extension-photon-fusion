// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "ClientConstructOptions.h"
#include "ConnectOptions.h"
#include "ConnectionState.h"
#include "DirectMessageOptions.h"
#include "DisconnectCause.h"
#include "ErrorCode.h"
#include "EventOptions.h"
#include "FriendInfo.h"
#include "LobbyStats.h"
#include "LobbyType.h"
#include "MatchmakingMode.h"
#include "NetworkStats.h"
#include "PlayerView.h"
#include "PropertyValue.h"
#include "RegionInfo.h"
#include "Result.h"
#include "CreateRoomOptions.h"
#include "JoinRoomOptions.h"
#include "MatchmakingOptions.h"
#include "RoomListing.h"
#include "MutableRoomView.h"
#include "Task.h"
#include "TrafficStats.h"
#include "TrafficStatsGameLevel.h"
#include "WebFlags.h"
#include "WebRpcResponse.h"

#include "Broadcaster.h"

#include <cstdint>
#include <memory>
#include <optional>
#include "SpanCompat.h"
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace PhotonMatchmaking {

    class RealtimeClient final {
    public:
        explicit RealtimeClient(const ClientConstructOptions& options);

        ~RealtimeClient();

        RealtimeClient(const RealtimeClient&) = delete;
        RealtimeClient& operator=(const RealtimeClient&) = delete;
        RealtimeClient(RealtimeClient&&) = delete;
        RealtimeClient& operator=(RealtimeClient&&) = delete;

        void Service(bool dispatchIncomingCommands = true);
        void ServiceBasic();
        bool SendOutgoingCommands();
        bool SendAcksOnly();
        bool DispatchIncomingCommands();

        Task<Result<void>> Connect();
        Task<Result<void>> Connect(const ConnectOptions& options);
        Task<Result<void>> Disconnect();
        Task<Result<void>> Reconnect();

        ConnectionState GetState() const noexcept;
        bool IsConnected() const noexcept;
        bool IsInRoom() const noexcept;
        bool IsInLobby() const noexcept;

        DisconnectCause GetDisconnectCause() const;

        Task<Result<std::vector<RegionInfo>>> AvailableRegions();
        Task<Result<void>> SelectRegion(PhotonCommon::StringViewType region);
        PhotonCommon::StringType GetBestRegion() const;

        Task<Result<void>> JoinLobby(PhotonCommon::StringViewType name = {}, LobbyType type = LobbyType::Default);
        Task<Result<void>> LeaveLobby();
        Task<Result<std::vector<LobbyStats>>> GetLobbyStats();

        Task<Result<std::vector<RoomListing>>> GetRoomList(PhotonCommon::StringViewType lobby, PhotonCommon::StringViewType sqlFilter);
        const std::vector<RoomListing>& GetCachedRoomList() const;

        Task<Result<MutableRoomView>> CreateRoom(PhotonCommon::StringViewType name = {}, const CreateRoomOptions& createOptions = {});
        Task<Result<MutableRoomView>> JoinRoom(PhotonCommon::StringViewType name, const JoinRoomOptions& joinOptions = {});
        Task<Result<MutableRoomView>> JoinOrCreateRoom(PhotonCommon::StringViewType name, const CreateRoomOptions& createOptions = {}, const JoinRoomOptions& joinOptions = {});
        Task<Result<MutableRoomView>> JoinRandomRoom(const MatchmakingOptions& matchmakingOptions = {});
        Task<Result<MutableRoomView>> JoinRandomOrCreateRoom(const CreateRoomOptions& createOptions = {}, const MatchmakingOptions& matchmakingOptions = {});
        Task<Result<void>> LeaveRoom(bool willComeBack = false, bool sendAuthCookie = false);

        std::optional<MutableRoomView> GetCurrentRoom() const;

        PlayerView GetLocalPlayer() const;
        void SetPlayerName(PhotonCommon::StringViewType name, const WebFlags& webFlags = {});
        bool SetPlayerProperties(const PropertyMap& properties, const WebFlags& webFlags = {});
        template<typename T>
        bool SetPlayerProperty(PhotonCommon::StringViewType key, const T& value);
        bool RemovePlayerProperties(const std::vector<PhotonCommon::StringType>& keys, const WebFlags& webFlags = {});

        bool SendEvent(uint8_t eventCode, std::span<const uint8_t> data, const EventOptions& options = {});
        template<typename T>
        requires std::is_trivially_copyable_v<T> && (!std::is_convertible_v<T, std::span<const uint8_t>> && !std::is_convertible_v<T, std::span<uint8_t>>)
        bool SendEvent(uint8_t eventCode, const T& data, const EventOptions& options = {});

        template<typename T>
        requires std::is_trivially_copyable_v<T> && (!std::is_convertible_v<T, std::span<const uint8_t>> && !std::is_convertible_v<T, std::span<uint8_t>>)
        int SendDirect(const T& data, const DirectMessageOptions& options = {});
        int SendDirect(std::span<const uint8_t> data, const DirectMessageOptions& options = {});

        bool ChangeGroups(const std::vector<uint8_t>& remove, const std::vector<uint8_t>& add);

        Task<Result<std::vector<FriendInfo>>> FindFriends(const std::vector<PhotonCommon::StringType>& userIds);
        const std::vector<FriendInfo>& GetFriendList() const;
        int GetFriendListAge() const;

        Task<Result<WebRpcResponse>> WebRpc(PhotonCommon::StringViewType uriPath);
        Task<Result<WebRpcResponse>> WebRpc(PhotonCommon::StringViewType uriPath, const PropertyMap& parameters, bool sendAuthCookie = false);

        NetworkStats GetStats() const;
        int GetServerTime() const;

        void SetTrafficStatsEnabled(bool enabled);
        void ResetTrafficStats();
        PhotonCommon::StringType GetVitalStatsToString(bool all = false) const;

        PhotonCommon::Broadcaster<void(DisconnectCause)> OnDisconnected;
        PhotonCommon::Broadcaster<void(ErrorCode, PhotonCommon::StringViewType)> OnError;
        PhotonCommon::Broadcaster<void(const PropertyMap&)> OnRoomPropertiesChanged;
        PhotonCommon::Broadcaster<void(const std::vector<RoomListing>&)> OnRoomListUpdated;
        PhotonCommon::Broadcaster<void(int newId, int oldId)> OnMasterClientChanged;
        PhotonCommon::Broadcaster<void(const PlayerView&)> OnPlayerJoined;
        PhotonCommon::Broadcaster<void(int playerNumber, bool isInactive)> OnPlayerLeft;
        PhotonCommon::Broadcaster<void(int playerNumber, const PropertyMap&)> OnPlayerPropertiesChanged;
        PhotonCommon::Broadcaster<void(uint8_t eventCode, int senderId, std::span<const uint8_t> data)> OnEvent;
        PhotonCommon::Broadcaster<void(int senderId, std::span<const uint8_t> data, bool isRelay)> OnDirectMessage;
        PhotonCommon::Broadcaster<void(const std::vector<LobbyStats>&)> OnLobbyStats;
        PhotonCommon::Broadcaster<void(const std::unordered_map<PhotonCommon::StringType, PhotonCommon::StringType>&)> OnCustomAuthStep;
        PhotonCommon::Broadcaster<void()> OnAppStatsUpdated;
        PhotonCommon::Broadcaster<void(int warningCode)> OnWarning;
        PhotonCommon::Broadcaster<void()> OnPropertiesChangeFailed;
        PhotonCommon::Broadcaster<void(int cacheSliceIndex)> OnCacheSliceChanged;
        PhotonCommon::Broadcaster<void(int remotePlayerId)> OnDirectConnectionEstablished;
        PhotonCommon::Broadcaster<void(int remotePlayerId)> OnDirectConnectionFailed;
        PhotonCommon::Broadcaster<void(uint8_t opCode, int errorCode, PhotonCommon::StringViewType errorString, const PropertyMap& data)> OnCustomOperationResponse;
        PhotonCommon::Broadcaster<void()> OnRoomJoined;
        PhotonCommon::Broadcaster<void()> OnRoomLeft;

        PhotonCommon::StringType GetUserId() const;

        bool SendCustomOperation(uint8_t operationCode, const PropertyMap& params, bool reliable = true, uint8_t channel = 0, bool encrypt = false);

        bool SendCustomAuthData(const AuthenticationValues& auth);

        void SetAutoJoinLobby(bool enabled);

        void FetchServerTimestamp();

        PhotonCommon::StringType GetMasterServerAddress() const;

        void SetDisconnectTimeout(int ms);
        int GetDisconnectTimeout() const;

        void SetPingInterval(int ms);
        int GetPingInterval() const;

        void SetSentCountAllowance(int count);
        int GetSentCountAllowance() const;

        void SetQuickResendAttempts(uint8_t attempts);
        uint8_t GetQuickResendAttempts() const;

        void SetCrcEnabled(bool enabled);
        bool GetCrcEnabled() const;

        void SetLimitOfUnreliableCommands(int limit);
        int GetLimitOfUnreliableCommands() const;

        TrafficStats GetTrafficStatsIncoming() const;
        TrafficStats GetTrafficStatsOutgoing() const;
        TrafficStatsGameLevel GetTrafficStatsGameLevel() const;
        void ResetTrafficStatsMaximumCounters();

    private:
        friend class MutableRoomView;
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    template<typename T>
    bool RealtimeClient::SetPlayerProperty(PhotonCommon::StringViewType key, const T& value) {
        return SetPlayerProperties({{PhotonCommon::StringType(key), PropertyValue(value)}});
    }

    template<typename T>
    requires std::is_trivially_copyable_v<T> && (!std::is_convertible_v<T, std::span<const uint8_t>> && !std::is_convertible_v<T, std::span<uint8_t>>)
    bool RealtimeClient::SendEvent(uint8_t eventCode, const T& data, const EventOptions& options) {
        static_assert(!std::is_pointer_v<T>, "Pass a span instead of a raw pointer.");
        static_assert(std::is_trivially_copyable_v<T>, "SendEvent<T> requires trivially copyable type. Use the span overload for complex types.");
        return SendEvent(eventCode,std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data), sizeof(T)), options);
    }

    template<typename T>
    requires std::is_trivially_copyable_v<T> && (!std::is_convertible_v<T, std::span<const uint8_t>> && !std::is_convertible_v<T, std::span<uint8_t>>)
    int RealtimeClient::SendDirect(const T& data, const DirectMessageOptions& options) {
        static_assert(std::is_trivially_copyable_v<T>, "SendDirect<T> requires trivially copyable type. Use the span overload for complex types.");
        static_assert(!std::is_pointer_v<T>, "Pass a span instead of a raw pointer.");
        return SendDirect(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data),sizeof(T)), options);
    }
} // namespace PhotonMatchmaking
