// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

// Static helpers for building the values that go into Photon room custom
// properties recognized by the Fusion plugin. Use these from an integration
// (Godot, Unreal, Unity, ...) when constructing a CreateRoomOptions to seed
// a new room with a specific initial configuration, map state, or SDK version.
//
// Mirror of FusionPlugin/RoomProperties.cs on the server side. The wire formats
// must stay in sync — anything emitted here is parsed by the plugin's
// Plugin.cs OnCreateGame, and anything the plugin writes back is parsed by
// FusionCore::Client::Start.

#include "Aliases.h"
#include "Buffers.h"
#include "Misc.h"
#include "SpanCompat.h"
#include "StringType.h"
#include "Types.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace FusionCore {

    // Base64 encode / decode utilities. Used for fusion_map_data and
    // fusion_sdk_version room properties — both binary blobs carried as
    // base64-encoded strings on the Photon wire.
    namespace Base64 {

        inline std::string Encode(std::span<const uint8_t> bytes) {
            static constexpr char alphabet[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

            std::string out;
            out.reserve((bytes.size() + 2) / 3 * 4);

            size_t i = 0;
            for (; i + 3 <= bytes.size(); i += 3) {
                uint32_t v = (uint32_t(bytes[i]) << 16)
                           | (uint32_t(bytes[i + 1]) << 8)
                           | uint32_t(bytes[i + 2]);
                out.push_back(alphabet[(v >> 18) & 0x3F]);
                out.push_back(alphabet[(v >> 12) & 0x3F]);
                out.push_back(alphabet[(v >> 6) & 0x3F]);
                out.push_back(alphabet[v & 0x3F]);
            }

            if (i < bytes.size()) {
                uint32_t v = uint32_t(bytes[i]) << 16;
                const bool two = (i + 1 < bytes.size());
                if (two) v |= uint32_t(bytes[i + 1]) << 8;
                out.push_back(alphabet[(v >> 18) & 0x3F]);
                out.push_back(alphabet[(v >> 12) & 0x3F]);
                out.push_back(two ? alphabet[(v >> 6) & 0x3F] : '=');
                out.push_back('=');
            }

            return out;
        }

        inline std::vector<uint8_t> Decode(std::string_view input) {
            std::vector<uint8_t> out;
            out.reserve((input.size() * 3) / 4);

            uint32_t buffer = 0;
            int bits = 0;

            for (const char c : input) {
                int val;
                if (c >= 'A' && c <= 'Z')      val = c - 'A';
                else if (c >= 'a' && c <= 'z') val = c - 'a' + 26;
                else if (c >= '0' && c <= '9') val = c - '0' + 52;
                else if (c == '+')             val = 62;
                else if (c == '/')             val = 63;
                else if (c == '=')             break;
                else                           continue;

                buffer = (buffer << 6) | static_cast<uint32_t>(val);
                bits += 6;

                if (bits >= 8) {
                    bits -= 8;
                    out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
                }
            }

            return out;
        }
    }

    // Well-known room (game) property keys recognized by the Fusion plugin.
    namespace FusionRoomProperties {
        // JSON string with overrides on top of FusionConfigDefaults::Json.
        // The plugin merges these on top of its built-in defaults at OnCreateGame.
        // Gated by the server's "AllowConfigOverride" default — if false, ignored.
        inline constexpr PhotonCommon::StringViewType Config = PHOTON_STR("fusion_config");

        // byte[] in FusionMapStateBuilder format. Optional — defaults to empty.
        inline constexpr PhotonCommon::StringViewType MapData = PHOTON_STR("fusion_map_data");

        // 20-byte packed SdkVersion. Optional — if absent, the room locks to the
        // first valid client's version via the version handshake.
        inline constexpr PhotonCommon::StringViewType SdkVersion = PHOTON_STR("fusion_sdk_version");
    }

    // The built-in default Fusion configuration. The plugin uses this when no
    // fusion_config override is supplied; integrations can deserialize, mutate,
    // and re-serialize a copy if they want to override specific fields.
    namespace FusionConfigDefaults {
        inline constexpr PhotonCommon::StringViewType Json = PHOTON_STR(R"({
            "RoomSendRate": 30,
            "DefaultPriority": 2,
            "AllowCustomEvents": false,
            "AllowConfigOverride": true,
            "ConfigWasOverridden": false,
            "ConfigOverrideFailed": false,
            "InterestSlackTime": 2.0,
            "InterestUpdateTime": 0.25,
            "OwnershipRequestCooldown": 0.1
        })");
    }

    // Build the byte[] payload for the fusion_map_data room property. The wire
    // format matches Plugin.cs:SerializeMapState on the server and what
    // Client::Start expects on the receiving side.
    namespace FusionMapStateBuilder {

        // Serialize an arbitrary map state. Each (Map, bytes) entry corresponds
        // to one entry in the plugin's _maps dictionary.
        inline std::vector<uint8_t> Build(
            const std::map<Map, std::vector<uint8_t>>& maps,
            Map mapCounter = 0)
        {
            WriteBuffer writer{};
            writer.WriteMap(mapCounter);
            writer.UShortVar(static_cast<uint16_t>(maps.size()));
            for (const auto& [map, bytes] : maps) {
                writer.WriteMap(map);
                Data view{const_cast<uint8_t*>(bytes.data()), bytes.size()};
                writer.WriteDataAll(view);
            }

            auto data = writer.Take();
            std::vector<uint8_t> out(data.Ptr, data.Ptr + data.Length);
            data.Free();
            return out;
        }

        // Bytes for an empty initial state — just Map.Global (0) with no data
        // and the map counter at zero. Matches Plugin.cs constructor defaults.
        inline std::vector<uint8_t> Empty() {
            return Build({{Map{0}, {}}}, Map{0});
        }

        // Base64-encoded form, ready to drop into a Photon room custom property.
        // The plugin expects fusion_map_data as a base64 string on the wire.
        inline std::string BuildBase64(
            const std::map<Map, std::vector<uint8_t>>& maps,
            Map mapCounter = 0)
        {
            const auto bytes = Build(maps, mapCounter);
            return Base64::Encode(bytes);
        }

        inline std::string EmptyBase64() {
            const auto bytes = Empty();
            return Base64::Encode(bytes);
        }
    }

    // Helpers for the fusion_sdk_version room property.
    namespace FusionSdkVersionBuilder {
        // 20-byte packed representation of the supplied SdkVersion. Pass
        // FusionCore::Client::GetSdkVersion() to publish this build's version.
        inline std::vector<uint8_t> ToBytes(const SdkVersion& version) {
            return std::vector<uint8_t>(
                std::begin(version._packed),
                std::end(version._packed));
        }

        // Base64-encoded form, ready to drop into a Photon room custom property.
        // The plugin expects fusion_sdk_version as a base64 string on the wire.
        inline std::string ToBase64(const SdkVersion& version) {
            return Base64::Encode(std::span<const uint8_t>(
                version._packed, sizeof(version._packed)));
        }
    }

}
