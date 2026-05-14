// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <functional>
#include "Misc.h"
#include "Aliases.h"
#include "Buffers.h"
#include "EMA.h"
#include "StringHeap.h"
#include "SpanCompat.h"

namespace FusionCore {
    constexpr uint8_t OBJECT_SENDFLAG_CREATE = 1;
    constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_ENTRIES_CHANGE = 2;
    constexpr uint8_t OBJECT_SENDFLAG_STRINGHEAP_DATA_CHANGE = 4;
    constexpr uint8_t OBJECT_SENDFLAG_IN_INTEREST_SET = 8;
    constexpr uint8_t OBJECT_SENDFLAG_IS_SUBOBJECT = 16;
    constexpr uint8_t OBJECT_SENDFLAG_TIMEONLY = 32;

    constexpr uint64_t RPC_InternalMinId = 1;
    constexpr uint64_t RPC_InternalMaxId = 1023;
    constexpr uint64_t RPC_InternalMapChange = 1;
    constexpr uint64_t RPC_InternalObjectPriority = 2;
    constexpr uint64_t RPC_InternalMapAdd = 3;
    constexpr uint64_t RPC_InternalMapRemove = 4;
    constexpr uint64_t RPC_InternalOwnershipRequest = 5;
    constexpr uint64_t RPC_InternalRejectSubObject = 6;
    constexpr uint64_t RPC_InternalDestroyedMapActors = 7;
    constexpr uint64_t RPC_InternalForceDestroyObject = 8;
    constexpr uint64_t RPC_InternalForceAliveObject = 9;
    constexpr uint64_t RPC_InternalInput = 10;
    constexpr uint64_t RPC_InternalPlayerInterest = 11;
    constexpr uint64_t RPC_InternalOwnershipResponse = 12;

    enum class RpcFlags : uint32_t {
        None                   = 0,
        ReturnResultOnFailure  = 1 << 0,
        IncorrectTargetForward = 1 << 1,
        DontReplyWithResult    = 1 << 2,

        PlayerMissing = 1 << 5,
        ObjectMissing = 1 << 6,
        MapIncorrect  = 1 << 7,
        MaxDeliveryAttemptsReached = 1 << 8,
        ErrorFlags    = PlayerMissing | ObjectMissing | MapIncorrect | MaxDeliveryAttemptsReached,
    };

    inline RpcFlags operator|(RpcFlags a, RpcFlags b) {
        return static_cast<RpcFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline RpcFlags& operator|=(RpcFlags& a, RpcFlags b) {
        a = a | b;
        return a;
    }

    inline bool HasFlag(RpcFlags flags, RpcFlags flag) {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) == static_cast<uint32_t>(flag);
    }

    class Rpc {
    public:
        uint64_t Id{};
        uint64_t Sequence{};
        RpcFlags Flags{RpcFlags::None};
        uint32_t DeliveryAttempts{0};
        PlayerId OriginPlayer{};
        PlayerId TargetPlayer{};
        ObjectId TargetObject{0, 0, 0};

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
        PlayerPredicted = 5,
    };

    enum class ObjectOwnerIntent : uint8_t {
        DontWantOwner = 0,
        WantOwner = 1,
    };

    enum class SimulationMode : uint8_t {
        Shared = 0,
        Authority = 1,
    };

    #pragma pack(push, 4)
    struct ObjectTail {
        uint32_t Reserved[8];

        int32_t RequiredObjectsCount;
        uint64_t InterestKey;

        int32_t Destroyed;
        int32_t RoomSendRate;

        PlayerId PredictingPlayer;
        uint32_t RejectedSequence;

        uint32_t InputSequence;
        uint32_t InputTime;

        int32_t Dummy;
    };
    #pragma pack(pop)

    static_assert(std::is_trivially_copyable<ObjectTail>());
    static_assert(sizeof(ObjectTail) == 72);
    static_assert(offsetof(ObjectTail, Reserved) == 0);
    static_assert(offsetof(ObjectTail, RequiredObjectsCount) == 32);
    static_assert(offsetof(ObjectTail, InterestKey) == 36);
    static_assert(offsetof(ObjectTail, Destroyed) == 44);
    static_assert(offsetof(ObjectTail, RoomSendRate) == 48);
    static_assert(offsetof(ObjectTail, PredictingPlayer) == 52);
    static_assert(offsetof(ObjectTail, RejectedSequence) == 56);
    static_assert(offsetof(ObjectTail, InputSequence) == 60);
    static_assert(offsetof(ObjectTail, InputTime) == 64);
    static_assert(offsetof(ObjectTail, Dummy) == 68);

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
    class ObjectChild;
    class Client;

    class Object {
        friend class Client;
        friend class ObjectRoot;
        friend class ObjectChild;

        FusionCore::Client *Client{nullptr};

        Object *Prev{nullptr};
        Object *Next{nullptr};

        BufferT<Word> WordsPlugin{};
        BufferT<uint8_t> WordsPluginReceived{};

        bool CreatedLocal{false};
        bool ReceivedPluginUpdate{false};
        bool SendUpdates{true};
        bool InterestKeySet{false};
        bool HasValidData{false};

        EMA SendStats{0.5};
        EMA RecvStats{0.5};

        uint8_t SendFlags{0};

        Tick RemoteTickSent{0};
        Tick RemoteTickAcked{0};

        BufferT<Tick> Ticks{};

        FusionCore::ObjectType ObjectType{FusionCore::ObjectType::Base};
        NetworkedStringHeap StringHeap{0};

    public:
        static constexpr size_t ExtraTailWords = sizeof(ObjectTail) / 4;

        explicit Object(FusionCore::Client *client) : Client(client) { }

        [[nodiscard]] const NetworkedStringHeap &GetStringHeap() const { return StringHeap; }
        [[nodiscard]] NetworkedStringHeap &GetStringHeap() { return StringHeap; }

        ObjectId Id{0, 0, 0};
        TypeRef Type{0, 0};

        void *Engine{nullptr};
        Data EngineBlob{};
        uint32_t EngineFlags{0};
        uint64_t EngineHash{0};

        BufferT<Word> Words{};
        BufferT<Word> Shadow{};

        virtual ~Object() = default;

        [[nodiscard]] bool SendHeader() const { return RemoteTickAcked == 0; }
        [[nodiscard]] bool GetHasValidData() const { return HasValidData; }
        [[nodiscard]] EMAReport GetSendReport() const { return SendStats.Report(); }
        [[nodiscard]] EMAReport GetRecvReport() const { return RecvStats.Report(); }

        void SetHasValidData() { HasValidData = true; }
        void SetSendUpdates(bool sendUpdates) { SendUpdates = sendUpdates; }

        [[nodiscard]] enum ObjectType GetObjectType() const { return ObjectType; }
        [[nodiscard]] uint8_t GetSendFlags() const { return SendFlags; }
        [[nodiscard]] Tick GetRemoteTickSent() const { return RemoteTickSent; }
        [[nodiscard]] Tick GetRemoteTickAcked() const { return RemoteTickAcked; }

        virtual ObjectRoot *Root() = 0;

        StringHandle AddString(const PhotonCommon::CharType *str);
        const PhotonCommon::CharType* ResolveString(const StringHandle& handle, StringMessage& OutStatus);

        StringHandle FreeString(const StringHandle &handle);
        bool IsValidStringHandle(const StringHandle& handle);
        uint32_t GetStringLength(const StringHandle &handle);
        void LogStringData(const StringHandle &handle);
    };

    class ObjectChild final : public Object {
        friend class Client;
        friend class Object;
        friend class ObjectRoot;

        ObjectId Parent{0, 0, 0};

    public:
        explicit ObjectChild(FusionCore::Client *client) : Object(client) {
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

    struct InterestKeySet {
        std::map<uint64_t, uint8_t> Keys{};
        bool Dirty{false};

        void Set(uint64_t key, uint8_t sendRate) {
            auto it = Keys.find(key);
            if (it == Keys.end() || it->second != sendRate) {
                Keys[key] = sendRate;
                Dirty = true;
            }
        }

        void Remove(uint64_t key) {
            if (Keys.erase(key) > 0) {
                Dirty = true;
            }
        }

        void Clear() {
            if (!Keys.empty()) {
                Keys.clear();
                Dirty = true;
            }
        }

        void ClearWithPredicate(const std::function<bool(uint64_t)>& predicate) {
            for (auto it = Keys.begin(); it != Keys.end();) {
                if (predicate(it->first)) {
                    it = Keys.erase(it);
                    Dirty = true;
                } else {
                    ++it;
                }
            }
        }

        [[nodiscard]] bool IsDirty() const { return Dirty; }
        void ClearDirty() { Dirty = false; }

        void SetAreaKeys(const std::vector<std::tuple<uint64_t, uint8_t>>& keys) {
            ClearAreaKeys();
            for (const auto& [key, sendRate] : keys) {
                Set((key << 1) | 1, sendRate);
            }
        }

        void ClearAreaKeys() {
            ClearWithPredicate([](uint64_t key) { return (key & 1) == 1; });
        }

        void ClearUserKeys() {
            ClearWithPredicate([](uint64_t key) { return (key & 1) == 0; });
        }

        void AddUserKey(uint64_t key, uint8_t sendRate = 0) {
            Set(key << 1, sendRate);
        }

        void RemoveUserKey(uint64_t key) {
            Remove(key << 1);
        }

        [[nodiscard]] std::vector<std::tuple<uint64_t, uint8_t>> GetAllAreaKeys() const {
            std::vector<std::tuple<uint64_t, uint8_t>> result;
            for (const auto& [encoded, sendRate] : Keys) {
                if (encoded & 1) result.emplace_back(encoded >> 1, sendRate);
            }
            return result;
        }

        [[nodiscard]] std::vector<std::tuple<uint64_t, uint8_t>> GetAllUserKeys() const {
            std::vector<std::tuple<uint64_t, uint8_t>> result;
            for (const auto& [encoded, sendRate] : Keys) {
                if ((encoded & 1) == 0) result.emplace_back(encoded >> 1, sendRate);
            }
            return result;
        }
    };

    struct InputEntry {
        uint32_t Sequence{0};
        float DeltaTime{0};
        Data Payload;
        bool IsNew{true};
        uint8_t SendCount{0};

        InputEntry() = default;
        ~InputEntry() { Payload.Free(); }

        InputEntry(InputEntry &&other) noexcept
            : Sequence(other.Sequence), DeltaTime(other.DeltaTime),
              Payload(other.Payload), IsNew(other.IsNew), SendCount(other.SendCount) {
            other.Payload = {};
        }

        InputEntry &operator=(InputEntry &&other) noexcept {
            if (this != &other) {
                Payload.Free();
                Sequence = other.Sequence;
                DeltaTime = other.DeltaTime;
                Payload = other.Payload;
                IsNew = other.IsNew;
                SendCount = other.SendCount;
                other.Payload = {};
            }
            return *this;
        }

        InputEntry(const InputEntry &) = delete;
        InputEntry &operator=(const InputEntry &) = delete;
    };

    struct ObjectInput {
        ObjectId Id;
        Data Payload;
    };

    struct PredictedObjectState {
        std::deque<InputEntry> Inputs{};
        uint32_t MaxReceivedSequence{0};
        bool Reset{false};
        double AccumulatedTime{0};
    };

    class ObjectRoot final : public Object {
        friend class Client;
        friend class Object;

        double Time{0};

        PlayerId Owner{0};
        PlayerId OwnerNext{0};
        ObjectOwnerModes OwnerMode{0};
        ObjectOwnerIntent OwnerIntent{0};
        double OwnerIntentCooldown{0};

        bool ObjectReady{false};
        bool SentThisFrame{false};
        bool HasReceivedState{false};

        uint32_t LocalSendRate{1};

        int32_t PluginVersion{1};
        int32_t ClientVersion{1};
        int32_t ClientBaseVersion{0};

        std::vector<ObjectId> SubObjects{};

        PredictedObjectState PredictedState{};

    public:
        explicit ObjectRoot(FusionCore::Client *client) : Object(client) {
            ObjectType = ObjectType::Root;
        }

        [[nodiscard]] const std::vector<ObjectId> &GetSubObjects() const { return SubObjects; }
        [[nodiscard]] Map GetMap() const { return Id.Map; }
        [[nodiscard]] ObjectOwnerModes GetOwnerMode() const { return OwnerMode; }
        [[nodiscard]] PlayerId GetOwner() const { return Owner; }
        [[nodiscard]] double GetTime() const { return Time; }
        [[nodiscard]] bool IsReady() const { return ObjectReady; }
        [[nodiscard]] int32_t GetPluginVersion() const { return PluginVersion; }
        [[nodiscard]] int32_t GetClientVersion() const { return ClientVersion; }
        [[nodiscard]] bool GetHasReceivedState() const { return HasReceivedState; }

        [[nodiscard]] EMAReport GetCombinedSendReport() const;
        [[nodiscard]] EMAReport GetCombinedRecvReport() const;

        void QueueInput(float dt, Data payload);
        void ExecuteInputs(float dt);

        [[nodiscard]] uint32_t GetInputQueueCount() const { return static_cast<uint32_t>(PredictedState.Inputs.size()); }
        [[nodiscard]] float GetInputQueueDeltaTime() const {
            float total = 0;
            for (const auto &entry : PredictedState.Inputs) { total += entry.DeltaTime; }
            return total;
        }

        [[nodiscard]] double GetInputTime() const;

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
        std::span<ObjectId> RequiredObjects() const;
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

#include "SharedModeCompat.h"
