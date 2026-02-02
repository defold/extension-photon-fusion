// Copyright Exit Games GmbH. All Rights Reserved.

#ifndef SHAREDCLIENT_TYPES_H
#define SHAREDCLIENT_TYPES_H

#include <cstdint>
#include "Misc.h"
#include "Aliases.h"
#include "Buffers.h"
#include "StringHeap.h"

namespace SharedMode {
    struct InterestVector {
        int32_t X{0};
        int32_t Y{0};
        int32_t Z{0};

        InterestVector() = default;

        InterestVector(const int32_t x, const int32_t y, const int32_t z) : X(x), Y(y), Z(z) {
        }
    };

    struct InterestBox {
        InterestVector Center;
        InterestVector Extents;

        InterestBox() = default;

        InterestBox(const InterestVector center, const InterestVector extents) {
            Center = center;
            Extents = extents;
        }
    };


    constexpr int32_t OBJECT_STATUS_NEW = 0;
    constexpr int32_t OBJECT_STATUS_PENDING = 1;
    constexpr int32_t OBJECT_STATUS_CREATED = 2;

    constexpr uint8_t OBJECT_SENDFLAG_CREATE = 1;
    constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_ENTRIES_CHANGE = 2;
    constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_DATA_CHANGE = 4;

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

    enum class ObjectSettingsFlags : uint8_t {
        None = 0,
        OwnerLeavesOwnerToNone = 1 << 0,
        IsGlobalInstance = 1 << 1,
    };

    inline ObjectSettingsFlags operator~(ObjectSettingsFlags operand) {
        return static_cast<ObjectSettingsFlags>(~static_cast<uint8_t>(operand));
    }

    inline ObjectSettingsFlags operator&(ObjectSettingsFlags lhs, ObjectSettingsFlags rhs) {
        return static_cast<ObjectSettingsFlags>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
    }

    inline ObjectSettingsFlags operator|(ObjectSettingsFlags lhs, ObjectSettingsFlags rhs) {
        return static_cast<ObjectSettingsFlags>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }

    inline ObjectSettingsFlags& operator|=(ObjectSettingsFlags& lhs, ObjectSettingsFlags rhs)
    {
        lhs = lhs | rhs;
        return lhs;
    }

    enum class ObjectInterestModes : uint8_t {
        All = 0,
        Area = 1,
        Assigned = 2,
    };

    enum class ObjectOwnerModes : uint8_t {
        Transaction = 0,
        Dynamic = 1,
        MasterClient = 2,
    };

    enum class ObjectOwnerIntent : uint8_t {
        DontWantOwner = 0,
        WantOwner = 1,
    };

    struct ObjectFlags {
        union {
            uint32_t _packed;

            struct {
                ObjectSettingsFlags SettingsFlags;
                ObjectOwnerModes OwnerMode;
                ObjectInterestModes InterestMode;
            };
        };

        explicit ObjectFlags(uint32_t packed);

        ObjectFlags(const ObjectSettingsFlags settings, const ObjectOwnerModes owner,
                    const ObjectInterestModes interest): _packed(0) {
            SettingsFlags = settings;
            OwnerMode = owner;
            InterestMode = interest;
        }

        static ObjectFlags Read(ReadBuffer &reader);

        static void Write(WriteBuffer &writer, const ObjectFlags &rpc);
    };

    struct ObjectTail {
        int32_t AreaOfInterestX;
        int32_t AreaOfInterestY;
        int32_t AreaOfInterestZ;
        int32_t Destroyed;
        int32_t Dummy;
    };

    static_assert(alignof(ObjectTail) == 4);
    static_assert(sizeof(ObjectTail) == 20);
    static_assert(offsetof(ObjectTail, AreaOfInterestX) == 0);
    static_assert(offsetof(ObjectTail, AreaOfInterestY) == 4);
    static_assert(offsetof(ObjectTail, AreaOfInterestZ) == 8);
    static_assert(offsetof(ObjectTail, Destroyed) == 12);
    static_assert(offsetof(ObjectTail, Dummy) == 16);

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

        BufferT<Word> Shadow{};
        BufferT<Tick> Ticks{};

    protected:
        Client *Client;

    public:
        static constexpr size_t ExtraTailWords = 5;
        static constexpr double DynamicOwnerCooldownTime = 1.0 / 3;

        explicit Object(SharedMode::Client *client) : Client(client) {
        }

        ObjectId Id{0, 0};
        void *Engine{nullptr};
        ObjectType ObjectType{ObjectType::Base};
        bool HasValidData{false};
        Data Header{};
        TypeRef Type{};
        BufferT<Word> Words{};
        bool IgnoreProperties{false};

        virtual ~Object() = default;

        void SetHasValidData(const bool hasValidData) { HasValidData = hasValidData; }

        virtual ObjectRoot *Root() = 0;

        StringHandle AddString(const wchar_t *str);

        const wchar_t *ResolveString(const StringHandle &handle);

        StringHandle FreeString(const StringHandle &handle);
    };

    class ObjectChild final : public Object {
    public:
        ObjectId Parent{0, 0};
        uint32_t TargetObjectHash{0};
        int32_t SubObjectStatus{0};

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

        double Time{0};
        PlayerId Owner{0};
        ObjectFlags Flags{0};
        uint32_t Scene{0};

        ObjectOwnerIntent OwnerIntent{0};
        double OwnerIntentCooldown{0};

        Tick RemoteTickSent{0};
        Tick RemoteTickAcked{0};

        int32_t Status{0};
        int32_t UpdatesReceived{0};
        int32_t UpdatesInFlight{0};

        int32_t PluginVersion{1};
        int32_t ClientVersion{1};
        int32_t ClientBaseVersion{0};

        std::vector<ObjectId> SubObjects{};

        NetworkedStringHeap StringHeap{1024};

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

        ObjectRoot *Root() override;
    };

    class ObjectPacketEnvelope {
    public:
        std::vector<std::tuple<ObjectId, Tick> > ObjectUpdates{};
    };

    struct SdkVersion {
        union {
            struct {
                int32_t Major;
                int32_t Minor;
                int32_t Patch;
                int32_t Build;
                int32_t Protocol;
            };

            unsigned char _packed[20];
        };
    };

    struct WordData {
        int32_t offset;
        int32_t value;
    };
}

#endif
