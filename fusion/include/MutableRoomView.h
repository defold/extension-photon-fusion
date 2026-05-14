// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "DirectMode.h"
#include "PlayerView.h"
#include "PropertyValue.h"
#include "WebFlags.h"
#include "StringType.h"

#include <cstdint>
#include <vector>

namespace ExitGames::LoadBalancing { class MutableRoom; }

namespace PhotonMatchmaking {
    class RealtimeClient;

    class MutableRoomView {
    public:
        MutableRoomView() = default;

        // Read access
        const PhotonCommon::StringType& GetName() const noexcept;
        int GetPlayerCount() const noexcept;
        uint8_t GetMaxPlayers() const noexcept;
        bool IsOpen() const noexcept;
        bool IsVisible() const noexcept;
        const PropertyMap& GetCustomProperties() const noexcept;
        const std::vector<PlayerView>& GetPlayers() const noexcept;
        int GetMasterClientId() const noexcept;
        bool IsMasterClient() const noexcept;
        bool IsMasterClient(int localPlayerNumber) const noexcept;
        int GetPlayerTtlMs() const noexcept;
        int GetEmptyRoomTtlMs() const noexcept;
        bool GetPublishUserId() const noexcept;
        DirectMode GetDirectMode() const noexcept;
        const std::vector<PhotonCommon::StringType>& GetExpectedUsers() const noexcept;
        const std::vector<PhotonCommon::StringType>& GetLobbyProperties() const noexcept;
        bool GetSuppressRoomEvents() const noexcept;
        const std::vector<PhotonCommon::StringType>& GetPlugins() const noexcept;

        // Mutations
        bool SetOpen(bool isOpen, const WebFlags& webFlags = {});
        bool SetVisible(bool isVisible, const WebFlags& webFlags = {});
        bool SetMaxPlayers(uint8_t maxPlayers, const WebFlags& webFlags = {});
        bool SetProperties(const PropertyMap& properties, const WebFlags& webFlags = {});
        bool SetProperties(const PropertyMap& properties, const PropertyMap& expected, const WebFlags& webFlags = {});
        bool RemoveProperties(const std::vector<PhotonCommon::StringType>& keys, const WebFlags& webFlags = {});
        bool SetLobbyProperties(const std::vector<PhotonCommon::StringType>& props);
        bool SetExpectedUsers(const std::vector<PhotonCommon::StringType>& userIds);
        bool SetMasterClient(int playerNumber);

        template<typename T>
        bool SetProperty(PhotonCommon::StringViewType key, const T& value);

    private:
        friend class RealtimeClient;

        PhotonCommon::StringType name;
        int playerCount = 0;
        uint8_t maxPlayers = 0;
        bool isOpen = false;
        bool isVisible = false;
        PropertyMap customProperties;
        std::vector<PlayerView> players;
        int masterClientId = 0;
        int localPlayerNumber = 0;
        int playerTtlMs = 0;
        int emptyRoomTtlMs = 0;
        bool publishUserId = false;
        DirectMode directMode = DirectMode::None;
        std::vector<PhotonCommon::StringType> expectedUsers;
        std::vector<PhotonCommon::StringType> lobbyProperties;
        bool suppressRoomEvents = false;
        std::vector<PhotonCommon::StringType> plugins;

        ExitGames::LoadBalancing::MutableRoom* room = nullptr;
    };

    template<typename T>
    bool MutableRoomView::SetProperty(PhotonCommon::StringViewType key, const T& value) {
        return SetProperties({{PhotonCommon::StringType(key), PropertyValue(value)}});
    }
} // namespace PhotonMatchmaking
