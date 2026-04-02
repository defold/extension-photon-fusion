// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#ifndef SHAREDCLIENT_TYPES_H
#define SHAREDCLIENT_TYPES_H

#include <cstdint>
#include "Misc.h"
#include "Aliases.h"
#include "Buffers.h"
#include "StringHeap.h"

namespace SharedMode {
    constexpr int32_t OBJECT_STATUS_NEW = 0;
    constexpr int32_t OBJECT_STATUS_PENDING = 1;
    constexpr int32_t OBJECT_STATUS_CREATED = 2;

    constexpr uint8_t OBJECT_SENDFLAG_CREATE = 1;
    constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_ENTRIES_CHANGE = 2;
    constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_DATA_CHANGE = 4;
    constexpr uint8_t OBJECT_SENDFLAG_IN_INTEREST_SET = 8;
    constexpr uint8_t OBJECT_SENDFLAG_IS_SUBOBJECT = 16;
    constexpr uint8_t OBJECT_SENDFLAG_TIMEONLY = 32;

    constexpr uint64_t RPC_InternalMinId = 1;
    constexpr uint64_t RPC_InternalMaxId = 1023;
    constexpr uint64_t RPC_InternalSceneChange = 1;
    constexpr uint64_t RPC_InternalObjectPriority = 2;

    struct RpcFlags {
        uint32_t _value;

        static RpcFlags Read(ReadBuffer &reader);

        static void Write(WriteBuffer &writer, const RpcFlags &rpc);
    };

    class Rpc {
    public:
        uint64_t Id{};
        RpcFlags Flags{};
        PlayerId OriginPlayer{};
        PlayerId TargetPlayer{};
        ObjectId TargetObject{0, 0};
        uint16_t TargetComponent{};

        uint64_t DescriptorTypeHash{0};
        uint64_t EventHash{0};

        Data Bytes;

        bool IsInternal() const {
            return Id >= RPC_InternalMinId && Id <= RPC_InternalMaxId;
        }

        static Rpc Read(ReadBuffer &reader);

        static void Write(WriteBuffer &writer, const Rpc &rpc);
    };

    enum class ObjectOwnerModes : uint8_t {
        Transaction = 0,
        PlayerAttached = 1,
        Dynamic = 2,
        MasterClient = 3,
        GameGlobal = 4,
    };

    enum class ObjectOwnerIntent : uint8_t {
        DontWantOwner = 0,
        WantOwner = 1,
    };

    enum class ObjectSpecialFlags : uint8_t {
        None = 0,
        IsRootTransform = 1 << 1,
        IgnoreRootTransformProperties = 1 << 2,
        SceneObject = 1 << 3,
        ExistsOnClient = 1 << 4,
    };

    inline ObjectSpecialFlags operator&(ObjectSpecialFlags a, ObjectSpecialFlags b)
    {
        return static_cast<ObjectSpecialFlags>(
            static_cast<uint8_t>(a) & static_cast<uint8_t>(b)
        );
    }

    inline ObjectSpecialFlags operator|(ObjectSpecialFlags a, ObjectSpecialFlags b)
    {
        return static_cast<ObjectSpecialFlags>(
            static_cast<uint8_t>(a) | static_cast<uint8_t>(b)
        );
    }

    inline ObjectSpecialFlags& operator|=(ObjectSpecialFlags& a, ObjectSpecialFlags b)
    {
        a = a | b;
        return a;
    }

#pragma pack(push, 4)
    struct ObjectTail {
        int32_t RequiredObjectsCount;
        uint64_t InterestKey;
        int32_t Destroyed;
        int32_t SendRate;
        int32_t Dummy;
    };
    #pragma pack(pop)

    static_assert(std::is_trivially_copyable<ObjectTail>());
    static_assert(sizeof(ObjectTail) == 24);
    static_assert(offsetof(ObjectTail, RequiredObjectsCount) == 0);
    static_assert(offsetof(ObjectTail, InterestKey) == 4);
    static_assert(offsetof(ObjectTail, Destroyed) == 12);
    static_assert(offsetof(ObjectTail, SendRate) == 16);
    static_assert(offsetof(ObjectTail, Dummy) == 20);

    enum class InterestKeyType : uint8_t {
        Global = 0,
        Area = 1,
        User = 2
    };

    enum class ObjectType : uint8_t {
        Base = 1,
        Child = 2,
        Root = 3
    };

    class ObjectRoot;
    class Client;

    class Object {
        friend class Client;

        Object *Prev{nullptr};
        Object *Next{nullptr};

        BufferT<Word> WordsPlugin{};
        BufferT<uint8_t> WordsPluginReceived{};

        bool CreatedLocal{false};
        bool ReceivedPluginUpdate{false};
        bool SendUpdates{true};
        bool InterestKeySet{false};

        uint32_t BytesSentLastFrame{0};
        uint32_t BytesReceivedLastFrame{0};

        BufferT<Tick> Ticks{};

    protected:
        SharedMode::Client *Client;

    public:
        static constexpr size_t ExtraTailWords = sizeof(ObjectTail) / 4;
        static constexpr double DynamicOwnerCooldownTime = 1.0 / 3;

        explicit Object(SharedMode::Client *client) : Client(client) {
        }

        ObjectId Id{0, 0};
        void *Engine{nullptr};
        SharedMode::ObjectType ObjectType{SharedMode::ObjectType::Base};
        bool HasValidData{false};
        Data Header{};
        TypeRef Type{};

        BufferT<Word> Shadow{};
        BufferT<Word> Words{};

        ObjectSpecialFlags SpecialFlags{};
        uint8_t SendFlags{0};

        Tick RemoteTickSent{0};
        Tick RemoteTickAcked{0};

        int32_t Status{0};

        NetworkedStringHeap StringHeap{1024};

        virtual ~Object() = default;

        [[nodiscard]] uint32_t GetBytesSendLastTick() const {return BytesSentLastFrame; }
        [[nodiscard]] uint32_t GetBytesReceivedLastTick() const {return BytesReceivedLastFrame; }

        void ResetReceivedBytes()
        {
            BytesReceivedLastFrame = 0;
        }

        [[nodiscard]] uint32_t ConsumeBytesSendLastTick() {
            auto result = BytesSentLastFrame;
            BytesSentLastFrame = 0;
            return result;
        }

        [[nodiscard]] uint32_t ConsumeBytesReceivedLastTick() {
            auto result = BytesReceivedLastFrame;
            BytesReceivedLastFrame = 0;
            return result;
        }

        void SetHasValidData(const bool hasValidData) { HasValidData = hasValidData; }

        void SetSendUpdates(bool sendUpdates) { SendUpdates = sendUpdates; }

        virtual ObjectRoot *Root() = 0;

        StringHandle AddString(const PhotonCommon::CharType *str);

        const PhotonCommon::CharType* ResolveString(const StringHandle& handle, StringMessage& OutStatus);

        StringHandle FreeString(const StringHandle &handle);

        bool IsValidStringHandle(const StringHandle& handle);

        uint32_t GetStringLength(const StringHandle &handle);

        void LogStringData(const StringHandle &handle);
    };

    class ObjectChild final : public Object {
    public:
        ObjectId Parent{0, 0};
        uint32_t TargetObjectHash{0};
        explicit ObjectChild(SharedMode::Client *client) : Object(client) {
            ObjectType = ObjectType::Child;
        }

        static ObjectId GetParent(const Object *obj) {
            if (const auto *child = Cast(obj)) {
                return child->Parent;
            }

            return ObjectId(0);
        }

        static bool Is(const Object *obj) {
            return obj != nullptr && obj->ObjectType == ObjectType::Child;
        }

        static ObjectChild *Cast(Object *obj) {
            if (obj != nullptr && obj->ObjectType == ObjectType::Child) {
                return static_cast<ObjectChild *>(obj); // NOLINT(*-pro-type-static-cast-downcast)
            }

            return nullptr;
        }

        static const ObjectChild *Cast(const Object *obj) {
            if (obj != nullptr && obj->ObjectType == ObjectType::Child) {
                return static_cast<const ObjectChild *>(obj); // NOLINT(*-pro-type-static-cast-downcast)
            }

            return nullptr;
        }

        ObjectRoot *Root() override;
    };

    class ObjectRoot final : public Object {
    public:
        explicit ObjectRoot(SharedMode::Client *client) : Object(client) {
            ObjectType = ObjectType::Root;
        }

        int LocalSendRate{1};

        double Time{0};
        PlayerId Owner{0};
        ObjectOwnerModes OwnerMode{};
        uint32_t Scene{0};

        ObjectOwnerIntent OwnerIntent{0};
        double OwnerIntentCooldown{0};

        int32_t UpdatesReceived{0};

        bool SentThisFrame{false};

        bool ObjectReady{false};

        int32_t PluginVersion{1};
        int32_t ClientVersion{1};
        int32_t ClientBaseVersion{0};

        std::vector<ObjectId> SubObjects{};

        static bool Is(const Object *obj) {
            return obj != nullptr && obj->ObjectType == ObjectType::Root;
        }

        static ObjectRoot *Cast(Object *obj) {
            if (obj != nullptr && obj->ObjectType == ObjectType::Root) {
                return static_cast<ObjectRoot *>(obj); // NOLINT(*-pro-type-static-cast-downcast)
            }

            return nullptr;
        }

        static const ObjectRoot *Cast(const Object *obj) {
            if (obj != nullptr && obj->ObjectType == ObjectType::Root) {
                return static_cast<const ObjectRoot *>(obj); // NOLINT(*-pro-type-static-cast-downcast)
            }

            return nullptr;
        }

        bool IsRequired(ObjectId id) const;
        int32_t RequiredObjectsCount() const;
        ObjectId *RequiredObjects() const;

        ObjectRoot *Root() override;
    };

    class ObjectPacketEnvelope {
    public:
        std::vector<std::tuple<ObjectId, Tick> > ObjectUpdates{};
    };

    struct SdkVersion {
        union {
            
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4201)
#endif

            struct {
                int32_t Major;
                int32_t Minor;
                int32_t Patch;
                int32_t Build;
                int32_t Protocol;
            };

#ifdef _MSC_VER
#pragma warning(pop)
#endif

            unsigned char _packed[20];
        };
    };

    struct WordData {
        int32_t offset;
        int32_t value;
    };
}

#endif
