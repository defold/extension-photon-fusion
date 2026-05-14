/** Photon Fusion
 * Functions and constants for interacting with Photon Fusion
 * @document
 * @namespace fusion
 */

#define EXTENSION_NAME Fusion
#define LIB_NAME "Fusion"
#define MODULE_NAME "fusion"

#ifndef DLIB_LOG_DOMAIN
#define DLIB_LOG_DOMAIN "fusion"
#endif

#include <dmsdk/sdk.h>
#include <dmsdk/gamesys/components/comp_model.h>
#include <dmsdk/gamesys/components/comp_factory.h>
#include <dmsdk/gamesys/resources/res_model.h>
#include <dmsdk/gameobject/gameobject_props.h>
#include <dmsdk/rig/rig.h>

#include "photon_extension_defines.h"

#if PHOTON_PLATFORM_SUPPORTED

#include "Client.h"
#include "ClientConstructOptions.h"
#include "LogUtils.h"
#include "LogOutput.h"
#include "CreateRoomOptions.h"

#include "fusion_messages.h"
#include "fusion_header.h"
#include "fusion_helpers.h"


class FusionDefoldLogOutput : public PhotonCommon::LogOutput
{
public:
    FusionDefoldLogOutput() {}
    ~FusionDefoldLogOutput() {};
    void LogTrace(const PhotonCommon::CharType* message)
    {
        dmLogDebug("[internal][T] %s", (char*)message);
    }

    void LogDebug(const PhotonCommon::CharType* message)
    {
        dmLogDebug("[internal][D] %s", (char*)message);
    }

    void LogInfo(const PhotonCommon::CharType* message)
    {
        dmLogInfo("[internal][I] %s", (char*)message);
    }

    void LogWarning(const PhotonCommon::CharType* message)
    {
        dmLogWarning("[internal][W] %s", (char*)message);
    }

    void LogError(const PhotonCommon::CharType* message)
    {
        dmLogError("[internal][E] %s", (char*)message);
    }
};


struct FusionObject
{
    dmhash_t                 m_Id;
    FusionCore::ObjectRoot*  m_SharedObject;
    dmVMath::Point3          m_Position;
    dmVMath::Quat            m_Rotation;
    dmVMath::Vector3         m_Scale;
};

enum FusionConnectionState
{
    NOT_CONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
};


struct FusionCtx
{
    dmResource::HFactory                           m_ResourceFactory;
    dmConfigFile::HConfig                          m_ConfigFile;
    dmGameObject::HCollection                      m_Collection;
    uint64_t                                       m_Timestamp;
    FusionCore::Client*                            m_FusionClient;
    dmHashTable<dmhash_t, FusionObject*>           m_FusionObjects;
    FusionDefoldLogOutput*                         m_FusionDefoldLogOutput;
    dmScript::LuaCallbackInfo*                     m_EventCallback;
    PhotonMatchmaking::ConnectionState             m_ConnectionState;
    bool                                           m_IsStarted;

    // Component type identifiers
    uint32_t                                       m_Scriptc;
    uint32_t                                       m_Spritec;
    uint32_t                                       m_Modelc;
    uint32_t                                       m_Collisionobjectc;
    uint32_t                                       m_Particlefxc;
    uint32_t                                       m_Labelc;

    FusionCtx()
    {
        memset((void*)this, 0, sizeof(*this));
    }
};

FusionCtx* g_Ctx = 0;
dmScript::LuaCallbackInfo* g_FusionEventCallback = 0;

// Event constants
dmhash_t g_FusionEventOnObjectReady = dmHashString64("on_object_ready");
dmhash_t g_FusionEventOnSubObjectCreated = dmHashString64("on_sub_object_created");
dmhash_t g_FusionEventOnObjectDestroyed = dmHashString64("on_object_destroyed");
dmhash_t g_FusionEventOnSubObjectDestroyed = dmHashString64("on_sub_object_destroyed");
dmhash_t g_FusionEventOnObjectForceAlive = dmHashString64("on_object_force_alive");
dmhash_t g_FusionEventOnSubObjectForceAlive = dmHashString64("on_sub_object_force_alive");
dmhash_t g_FusionEventOnObjectOwnerAssigned = dmHashString64("on_object_owner_assigned");
dmhash_t g_FusionEventOnObjectOwnerChanged = dmHashString64("on_object_owner_changed");
dmhash_t g_FusionEventOnPredictionOverride = dmHashString64("on_prediction_override");
dmhash_t g_FusionEventOnLobbyStats = dmHashString64("on_lobby_stats");
dmhash_t g_FusionEventOnRoomJoined = dmHashString64("on_room_joined");
dmhash_t g_FusionEventOnRoomLeft = dmHashString64("on_room_left");
dmhash_t g_FusionEventOnRpc = dmHashString64("on_rpc");
dmhash_t g_FusionEventOnMapChange = dmHashString64("on_map_change");
dmhash_t g_FusionEventOnDestroyedMapActor = dmHashString64("on_destroyed_map_actor");
dmhash_t g_FusionEventOnInterestEnter = dmHashString64("on_interest_enter");
dmhash_t g_FusionEventOnInterestExit = dmHashString64("on_interest_exit");
dmhash_t g_FusionEventOnForcedDisconnect = dmHashString64("on_forced_disconnect");
dmhash_t g_FusionEventOnFusionStart = dmHashString64("on_fusion_start");
dmhash_t g_FusionEventOnConnected = dmHashString64("on_connected");
dmhash_t g_FusionEventOnDisconnected = dmHashString64("on_disconnected");


/******
 * Helpers
 *************************/

template <typename T>
static dmGameObject::Result PostDDF(const T* message, dmhash_t id)
{
    dmMessage::URL receiver;
    receiver.m_Path = id;
    receiver.m_Socket = dmGameObject::GetMessageSocket(g_Ctx->m_Collection);
    return dmGameObject::PostDDF(message, 0, &receiver, 0, false);
}


/******
 * Object handlers
 *************************/

static FusionCore::ObjectRoot* GetSharedObject(dmhash_t id)
{
    FusionObject** object = g_Ctx->m_FusionObjects.Get(id);
    if (object)
    {
        return (*object)->m_SharedObject;
    }

    return 0;
}

static bool HasSharedObject(dmhash_t id)
{
    FusionObject** object = g_Ctx->m_FusionObjects.Get(id);
    return object != 0;
}

static FusionObject* FindFusionObject(const FusionCore::ObjectRoot* object)
{
    dmHashTable<dmhash_t, FusionObject*>::Iterator iter = g_Ctx->m_FusionObjects.GetIterator();
    while(iter.Next())
    {
        FusionObject* fusion_object = iter.GetValue();
        if (object == fusion_object->m_SharedObject)
        {
            return fusion_object;
        }
    }
    return 0;
}

static dmhash_t DeleteFusionObject(const FusionCore::ObjectRoot* object)
{
    FusionObject* fusion_object = FindFusionObject(object);
    if (!fusion_object)
    {
        return 0;
    }
    dmhash_t id = fusion_object->m_Id;
    if (id != 0)
    {
        dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_Collection, id);
        dmGameObject::Delete(g_Ctx->m_Collection, instance, true);
    }
    g_Ctx->m_FusionObjects.Erase(id);
    free(fusion_object);
    return id;
}

static void DeleteGameObject(dmhash_t id)
{
    dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_Collection, id);
    dmGameObject::Delete(g_Ctx->m_Collection, instance, true);
}

static bool CreateGameObject(dmhash_t id, const FusionCore::ObjectRoot* object)
{
    uint64_t socket;
    uint64_t path;
    uint64_t fragment;

    uint8_t* header = object->EngineBlob.Ptr;
    size_t offset = 0;
    offset += PopHash(header + offset, &socket);
    offset += PopHash(header + offset, &path);
    offset += PopHash(header + offset, &fragment);

    //
    // get factory component
    dmGameObject::HInstance factory_go = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_Collection, path);
    if (factory_go == 0)
    {
        dmLogError("Main collection does not have a game object named %s", dmHashReverseSafe64(path));
        return false;
    }
    dmGameSystem::HFactoryWorld world;
    dmGameSystem::HFactoryComponent factory;
    uint32_t component_type_index;
    dmGameObject::Result r = dmGameObject::GetComponent(factory_go, fragment, &component_type_index, (dmGameObject::HComponent*)&factory, (dmGameObject::HComponentWorld*)&world);
    if (dmGameObject::RESULT_OK != r)
    {
        dmLogError("Unable to get component %s", dmHashReverseSafe64(fragment));
        return false;
    }

    //
    // spawn gameobject
    dmVMath::Point3 position = dmVMath::Point3(0, 0, 0);
    dmVMath::Quat rotation = dmVMath::Quat::identity();
    dmVMath::Vector3 scale = dmVMath::Vector3(1.0, 1.0, 1.0);
    
    // dmGameObject::PropertyContainerBuilderParams params;
    // params.m_BoolCount = 1;
    // dmGameObject::HPropertyContainerBuilder builder = dmGameObject::PropertyContainerCreateBuilder(params);
    // dmGameObject::PropertyContainerPushBool(builder, dmHashString64("remote_object"), true);
    // dmGameObject::HPropertyContainer properties = dmGameObject::PropertyContainerCreate(builder);
    dmGameObject::HPropertyContainer properties = 0x0;

    dmGameObject::HInstance instance;
    r = dmGameSystem::CompFactorySpawn(
            world, factory, g_Ctx->m_Collection,
            id, 
            position, rotation, scale,
            properties,
            &instance);

    dmGameObject::PropertyContainerDestroy(properties);

    return (dmGameObject::RESULT_OK == r);
}

static FusionObject* CreateFusionObject(dmhash_t id, FusionCore::ObjectRoot* object)
{
    FusionObject* fusion_object = FindFusionObject(object);
    if (fusion_object)
    {
        return fusion_object;
    }

    dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_Collection, id);

    if (g_Ctx->m_FusionObjects.Full())
    {
        g_Ctx->m_FusionObjects.OffsetCapacity(100);
    }
    fusion_object = (FusionObject*)malloc(sizeof(FusionObject));
    fusion_object->m_Id = id;
    fusion_object->m_SharedObject = object;
    fusion_object->m_Scale.setX(1.0);
    fusion_object->m_Scale.setY(1.0);
    fusion_object->m_Scale.setZ(1.0);
    fusion_object->m_Position.setX(0.0);
    fusion_object->m_Position.setY(0.0);
    fusion_object->m_Position.setZ(0.0);
    fusion_object->m_Rotation.setX(0.0);
    fusion_object->m_Rotation.setY(0.0);
    fusion_object->m_Rotation.setZ(0.0);
    fusion_object->m_Rotation.setW(0.0);
    // important to add object here before the game object is created
    // we need to be able to query for instance has_authority() from a script
    g_Ctx->m_FusionObjects.Put(id, fusion_object);

    if (!instance)
    {
        bool ok = CreateGameObject(id, object);
        if (!ok)
        {
            g_Ctx->m_FusionObjects.Erase(id);
            free(fusion_object);
            return 0;
        }
    }

    return fusion_object;
}

static bool BuildObjectHeader(dmhash_t id, dmMessage::URL* factory_url, uint8_t* header, size_t &headerLength, size_t &wordsCount)
{
    headerLength = 0;
    headerLength += PushHash(header + headerLength, factory_url->m_Socket);
    headerLength += PushHash(header + headerLength, factory_url->m_Path);
    headerLength += PushHash(header + headerLength, factory_url->m_Fragment);

    size_t componentCountOffset = headerLength;
    headerLength += PushUint16(header + headerLength, 0);

    // pos, rot, scale
    wordsCount = 3 + 4 + 3;

    dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_Collection, id);
    uint16_t component_index = 0;
    while (true)
    {
        dmLogInfo("Getting component with index '%d' from instance '%s'", component_index, dmHashReverseSafe64(id));
        dmhash_t component_id;
        dmGameObject::Result r = dmGameObject::GetComponentId(instance, component_index, &component_id);
        if (dmGameObject::RESULT_OK != r)
        {
            dmLogInfo("No more components!");
            break;
        }
        uint32_t component_type;
        dmGameObject::HComponent component;
        dmGameObject::HComponentWorld world;
        r = dmGameObject::GetComponent(instance, component_id, &component_type, &component, &world);
        if (dmGameObject::RESULT_OK != r)
        {
            dmLogError("Unable to get component with id '%s'", dmHashReverseSafe64(component_id));
            return false;
        }

        dmLogInfo("Got first component '%s' with type %d", dmHashReverseSafe64(component_id), component_type);
        headerLength += PushHash(header + headerLength, component_id);

        if (component_type == g_Ctx->m_Scriptc)
        {
            wordsCount += 0;
        }
        else if (component_type == g_Ctx->m_Modelc)
        {
            wordsCount += 2 + 1; // animation (hash), cursor (float)
        }
        else if (component_type == g_Ctx->m_Spritec)
        {
            wordsCount += 2 + 1; // animation (hash), cursor (float)
        }
        else if (component_type == g_Ctx->m_Collisionobjectc)
        {
            wordsCount += 3 + 3; // linear velocity (v3), angular velocity (v3)
        }
        else
        {

        }

        component_index++;
    }

    // write component count
    headerLength += PushUint16(header + componentCountOffset, component_index);
    return true;
}


static void SerializeFusionObject(FusionObject* fusion_object)
{
    const dmhash_t id = fusion_object->m_Id;
    const FusionCore::ObjectRoot* object = fusion_object->m_SharedObject;

    dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_Collection, id);

    FusionCore::Word *words = object->Words.Ptr;
    dmVMath::Point3 pos = dmGameObject::GetPosition(instance);
    dmVMath::Quat rot = dmGameObject::GetRotation(instance);
    dmVMath::Vector3 scale = dmGameObject::GetScale(instance);

    // if (object->Flags.InterestMode == FusionCore::ObjectInterestModes::Area)
    // {
    //     FusionCore::AOILocation location = g_Ctx->m_FusionClient->CalculateAreaOfInterestLocation(pos.getX(), pos.getY(), pos.getZ());
    //     g_Ctx->m_FusionClient->SetAreaOfInterestLocation(object, location);
    // }

    size_t word_offset = 0;
    word_offset += PushPoint3(words + word_offset, pos);
    word_offset += PushQuat(words + word_offset, rot);
    word_offset += PushVector3(words + word_offset, scale);

    uint64_t socket;
    uint64_t path;
    uint64_t fragment;

    uint8_t* header = object->EngineBlob.Ptr;
    size_t header_offset = 0;
    header_offset += PopHash(header + header_offset, &socket);
    header_offset += PopHash(header + header_offset, &path);
    header_offset += PopHash(header + header_offset, &fragment);
    uint16_t component_count;
    header_offset += PopUint16(header + header_offset, &component_count);
    // dmLogInfo("object %s:%s#%s has %d components", dmHashReverseSafe64(socket), dmHashReverseSafe64(path), dmHashReverseSafe64(fragment), component_count);
    for (int i = 0; i < component_count; i++)
    {
        dmhash_t component_id;
        header_offset += PopHash(header + header_offset, &component_id);

        uint32_t component_type;
        dmGameObject::HComponent component;
        dmGameObject::HComponentWorld world;
        dmGameObject::Result r = dmGameObject::GetComponent(instance, component_id, &component_type, &component, &world);
        if (dmGameObject::RESULT_OK != r)
        {
            dmLogError("Unable to get component with id '%s'", dmHashReverseSafe64(component_id));
            continue;
        }
        // dmLogInfo("  component %d with id %s has type %d", i, dmHashReverseSafe64(component_id), component_type);

        if (component_type == g_Ctx->m_Spritec)
        {
            dmhash_t animation;
            float cursor;
            dmGameObject::GetPropertyAsHash(instance, component_id, dmHashString64("animation"), &animation);
            dmGameObject::GetPropertyAsFloat(instance, component_id, dmHashString64("cursor"), &cursor);

            word_offset += PushHash(words + word_offset, animation);
            word_offset += PushFloat(words + word_offset, cursor);
        }
        else if (component_type == g_Ctx->m_Modelc)
        {
            dmhash_t animation;
            float cursor;
            dmGameObject::GetPropertyAsHash(instance, component_id, dmHashString64("animation"), &animation);
            dmGameObject::GetPropertyAsFloat(instance, component_id, dmHashString64("cursor"), &cursor);
            // dmLogInfo("    send model animation %s", dmHashReverseSafe64(animation));
            // dmLogInfo("    send model animation cursor %f", cursor);
            word_offset += PushHash(words + word_offset, animation);
            word_offset += PushFloat(words + word_offset, cursor);
        }
        else if (component_type == g_Ctx->m_Collisionobjectc)
        {
            dmVMath::Vector3 linear_velocity;
            dmVMath::Vector3 angular_velocity;
            dmGameObject::GetPropertyAsVector3(instance, component_id, dmHashString64("linear_velocity"), &linear_velocity);
            dmGameObject::GetPropertyAsVector3(instance, component_id, dmHashString64("angular_velocity"), &angular_velocity);
            word_offset += PushVector3(words + word_offset, linear_velocity);
            word_offset += PushVector3(words + word_offset, angular_velocity);
        }
    }
}

float Lerpf(float t, float a, float b)
{
    return a + (b - a) * t;
}
dmVMath::Point3 LerpPoint(float t, dmVMath::Point3 a, dmVMath::Point3 b)
{
    return dmVMath::Point3(
        a.getX() + (b.getX() - a.getX()) * t,
        a.getY() + (b.getY() - a.getY()) * t,
        a.getZ() + (b.getZ() - a.getZ()) * t);
}
static void LerpObjectTransform(dmhash_t id, FusionObject* fusion_object)
{
    dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_Collection, id);

    dmVMath::Point3 curpos = dmGameObject::GetPosition(instance);
    dmVMath::Point3 newpos = LerpPoint(0.1, curpos, fusion_object->m_Position);
    dmGameObject::SetPosition(instance, newpos);

    dmVMath::Quat currot = dmGameObject::GetRotation(instance);
    dmVMath::Quat newrot = dmVMath::Slerp(0.1, currot, fusion_object->m_Rotation);
    dmGameObject::SetRotation(instance, newrot);
    
    dmVMath::Vector3 curscl = dmGameObject::GetScale(instance);
    dmVMath::Vector3 newscl = dmVMath::Lerp(0.1, curscl, fusion_object->m_Scale);
    dmGameObject::SetScale(instance, newscl);
}
static void DeserializeFusionObject(FusionObject* fusion_object)
{
    const dmhash_t id = fusion_object->m_Id;
    const FusionCore::ObjectRoot* object = fusion_object->m_SharedObject;
    // if (!g_Ctx->m_FusionClient->HasBeenUpdatedByPlugin(object))
    // {
    //     LerpObjectTransform(id, fusion_object);
    //     return;
    // }

    FusionCore::Word *words = object->Words.Ptr;
    if (words == 0x0)
    {
        return;
    }
    size_t word_offset = 0;

    dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_Collection, id);
    if (!instance)
    {
        dmLogWarning("Fusion_TickAfterFrameBegin unable to find object with id %s", dmHashReverseSafe64(id));
        return;
    }

    word_offset += PopPoint3(words + word_offset, &fusion_object->m_Position);
    word_offset += PopQuat(words + word_offset, &fusion_object->m_Rotation);
    word_offset += PopVector3(words + word_offset, &fusion_object->m_Scale);
    LerpObjectTransform(id, fusion_object);

    uint64_t socket;
    uint64_t path;
    uint64_t fragment;

    uint8_t* header = object->EngineBlob.Ptr;
    size_t header_offset = 0;
    header_offset += PopHash(header + header_offset, &socket);
    header_offset += PopHash(header + header_offset, &path);
    header_offset += PopHash(header + header_offset, &fragment);
    uint16_t component_count;
    header_offset += PopUint16(header + header_offset, &component_count);
    // dmLogInfo("object %s:%s#%s has %d components", dmHashReverseSafe64(socket), dmHashReverseSafe64(path), dmHashReverseSafe64(fragment), component_count);
    for (int i = 0; i < component_count; i++)
    {
        dmhash_t component_id;
        header_offset += PopHash(header + header_offset, &component_id);

        uint32_t component_type;
        dmGameObject::HComponent component;
        dmGameObject::HComponentWorld world;
        dmGameObject::Result r = dmGameObject::GetComponent(instance, component_id, &component_type, &component, &world);
        if (dmGameObject::RESULT_OK != r)
        {
            dmLogError("Unable to get component with id '%s'", dmHashReverseSafe64(component_id));
            continue;
        }
        // dmLogInfo("  component %d with id %s has type %d", i, dmHashReverseSafe64(component_id), component_type);

        if (component_type == g_Ctx->m_Spritec)
        {
            dmLogInfo("  sprite");
            dmhash_t animation;
            float cursor;
            word_offset += PopHash(words + word_offset, &animation);
            word_offset += PopFloat(words + word_offset, &cursor);
            dmGameObject::SetPropertyFromHash(instance, component_id, dmHashString64("animation"), animation);
            dmGameObject::SetPropertyFromFloat(instance, component_id, dmHashString64("cursor"), cursor);
        }
        else if (component_type == g_Ctx->m_Modelc)
        {
            float current_cursor;
            dmGameObject::GetPropertyAsFloat(instance, component_id, dmHashString64("cursor"), &current_cursor);

            dmhash_t animation;
            float cursor;
            word_offset += PopHash(words + word_offset, &animation);
            word_offset += PopFloat(words + word_offset, &cursor);
            // dmLogInfo("    recv model animation %s", dmHashReverseSafe64(animation));
            // dmLogInfo("    recv model animation cursor %f", cursor);

            dmhash_t current_animation;
            dmGameObject::GetPropertyAsHash(instance, component_id, dmHashString64("animation"), &current_animation);
            if (current_animation != animation)
            {
                dmLogInfo("    changing animation from %s to %s", dmHashReverseSafe64(current_animation), dmHashReverseSafe64(animation));
                dmRig::RigPlayback playback = dmRig::RigPlayback::PLAYBACK_LOOP_FORWARD;
                float blend_duration = 0;
                float offset = 0;
                float playback_rate = 1.0;
                dmGameSystem::CompModelPlayAnimation((dmGameSystem::HModelWorld)world, (dmGameSystem::HModelComponent)component, animation, playback, blend_duration, offset, playback_rate, 0, 0);
            }
        }
        else if (component_type == g_Ctx->m_Collisionobjectc)
        {
            dmVMath::Vector3 linear_velocity;
            dmVMath::Vector3 angular_velocity;
            word_offset += PopVector3(words + word_offset, &linear_velocity);
            word_offset += PopVector3(words + word_offset, &angular_velocity);
            dmGameObject::SetPropertyFromVector3(instance, component_id, dmHashString64("linear_velocity"), linear_velocity);
            dmGameObject::SetPropertyFromVector3(instance, component_id, dmHashString64("angular_velocity"), angular_velocity);
        }
    }
}

static dmGameObject::Result DoRegisterObject(dmhash_t id, dmMessage::URL* factory_url, uint32_t scene, FusionCore::ObjectOwnerModes owner_mode)
{
    if (HasSharedObject(id))
    {
        return dmGameObject::RESULT_INVALID_OPERATION;
    }

    uint8_t header[1000];
    size_t headerLength;
    size_t wordsCount;
    bool ok = BuildObjectHeader(id, factory_url, header, headerLength, wordsCount);
    if (!ok)
    {
        return dmGameObject::RESULT_INVALID_OPERATION;
    }

    FusionCore::TypeRef type;
    type.Hash = 0;
    type.WordCount = wordsCount + FusionCore::Object::ExtraTailWords;

    uint32_t engineFlags = 0;
    FusionCore::ObjectRoot* object = g_Ctx->m_FusionClient->CreateObject(wordsCount,
                                                                         type,
                                                                         (PhotonCommon::CharType*)header,
                                                                         headerLength,
                                                                         scene,
                                                                         owner_mode,
                                                                        engineFlags);

    FusionObject* fusion_object = CreateFusionObject(id, object);
    if (!fusion_object)
    {
        return dmGameObject::RESULT_INVALID_OPERATION;
    }

    SerializeFusionObject(fusion_object);

    return dmGameObject::RESULT_OK;
}

/******
 * Fusion callbacks
 *************************/
static lua_State* SetupListener()
{
    if (!dmScript::IsCallbackValid(g_FusionEventCallback))
    {
        return 0;
    }

    lua_State* L = dmScript::GetCallbackLuaContext(g_FusionEventCallback);
    if (!dmScript::SetupCallback(g_FusionEventCallback))
    {
        dmLogError("Failed to setup callback");
        return 0;
    }
    return L;
}

static int CallListener(lua_State* L, int nargs, int nresults)
{
    int result = dmScript::PCall(L, nargs, nresults);
    dmScript::TeardownCallback(g_FusionEventCallback);
    return result;
}

static int CallListener(dmhash_t event_id)
{
    lua_State* L = SetupListener();
    if (!L)
    {
        return 0;
    }
    dmScript::PushHash(L, event_id);
    return CallListener(L, 2, 0);
}

static int CallListener(dmhash_t event_id, dmhash_t v)
{
    lua_State* L = SetupListener();
    if (!L)
    {
        return 0;
    }
    dmScript::PushHash(L, event_id);
    dmScript::PushHash(L, v);
    return CallListener(L, 3, 0);
}

static int CallListener(dmhash_t event_id, const char* v)
{
    lua_State* L = SetupListener();
    if (!L)
    {
        return 0;
    }
    dmScript::PushHash(L, event_id);
    lua_pushstring(L, v);
    return CallListener(L, 3, 0);
}

static void Fusion_OnObjectReady(FusionCore::ObjectRoot* object)
{
    dmLogInfo("Fusion_OnObjectReady owner: %d local player: %d", object->GetOwner(), g_Ctx->m_FusionClient->LocalPlayerId());

    dmhash_t id = dmGameObject::CreateInstanceId();
    FusionObject* fusion_object = CreateFusionObject(id, object);
    if (fusion_object)
    {
        lua_State* L = SetupListener();
        if(L)
        {
            dmScript::PushHash(L, g_FusionEventOnObjectReady);
            lua_newtable(L);
            dmScript::PushHash(L, fusion_object->m_Id);
            lua_setfield(L, -2, "id");
            CallListener(L, 3, 0);

            FusionMessages::OnObjectReady msg;
            msg.m_Id = fusion_object->m_Id;
            dmGameObject::Result result = PostDDF(&msg, fusion_object->m_Id);
        }
    }
    else
    {
        DeleteGameObject(id);
    }
}
static void Fusion_OnSubObjectCreated(FusionCore::ObjectChild* child)
{
    dmLogInfo("Fusion_OnSubObjectCreated - NOT IMPLEMENTED");
    CallListener(g_FusionEventOnSubObjectCreated);
}

static void Fusion_OnObjectDestroyed(const FusionCore::ObjectRoot* object, const FusionCore::DestroyModes mode)
{
    dmLogInfo("Fusion_OnObjectDestroyed owner: %d local player: %d", object->GetOwner(), g_Ctx->m_FusionClient->LocalPlayerId());

    if (mode == FusionCore::DestroyModes::Local)
    {
        dmLogInfo("DestroyModes Local");
        return;
    }
    // if (mode == FusionCore::DestroyModes::Remote)
    // {
    //     dmLogInfo("DestroyModes Remote");
    // }
    // else if (mode == FusionCore::DestroyModes::SceneChange)
    // {
    //     dmLogInfo("DestroyModes SceneChange");
    // }
    // else if (mode == FusionCore::DestroyModes::Shutdown)
    // {
    //     dmLogInfo("DestroyModes Shutdown");
    // }

    FusionObject* fusion_object = FindFusionObject(object);
    if (!fusion_object)
    {
        dmLogError("Unable to find object to destroy");
        return;
    }

    dmhash_t id = DeleteFusionObject(object);
    if (id)
    {
        CallListener(g_FusionEventOnObjectDestroyed, id);
    }
    else
    {
        dmLogError("Unable to find object to destroy");
    }
}
static void Fusion_OnSubObjectDestroyed(FusionCore::ObjectChild* child, const FusionCore::DestroyModes mode)
{
    dmLogInfo("Fusion_OnSubObjectDestroyed - NOT IMPLEMENTED");
    CallListener(g_FusionEventOnSubObjectDestroyed);
}
static void Fusion_OnObjectForceAlive(FusionCore::ObjectRoot* child)
{
    dmLogInfo("Fusion_OnObjectForceAlive - NOT IMPLEMENTED");
    CallListener(g_FusionEventOnObjectForceAlive);
}
static void Fusion_OnSubObjectForceAlive(FusionCore::ObjectChild* child)
{
    dmLogInfo("Fusion_OnSubObjectForceAlive - NOT IMPLEMENTED");
    CallListener(g_FusionEventOnSubObjectForceAlive);
}
static void Fusion_OnObjectOwnerAssigned(FusionCore::ObjectRoot* object)
{
    dmLogInfo("Fusion_OnObjectOwnerAssigned local player: %d", g_Ctx->m_FusionClient->LocalPlayerId());

    FusionObject* fusion_object = FindFusionObject(object);
    if (!fusion_object)
    {
        dmLogError("Unable to find object");
        return;
    }

    lua_State* L = SetupListener();
    if(L)
    {
        dmScript::PushHash(L, g_FusionEventOnObjectOwnerChanged);
        lua_newtable(L);
        dmScript::PushHash(L, fusion_object->m_Id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, object->GetOwner());
        lua_setfield(L, -2, "owner");
        CallListener(L, 3, 0);
    }

    FusionMessages::OnObjectOwnerAssigned msg;
    msg.m_Id = fusion_object->m_Id;
    msg.m_Owner = object->GetOwner();
    dmGameObject::Result result = PostDDF(&msg, fusion_object->m_Id);
}
static void Fusion_OnObjectOwnerChanged(FusionCore::ObjectRoot* object)
{
    dmLogInfo("Fusion_OnObjectOwnerChanged local player: %d", g_Ctx->m_FusionClient->LocalPlayerId());

    FusionObject* fusion_object = FindFusionObject(object);
    if (!fusion_object)
    {
        dmLogError("Unable to find object");
        return;
    }

    lua_State* L = SetupListener();
    if(L)
    {
        dmScript::PushHash(L, g_FusionEventOnObjectOwnerChanged);
        lua_newtable(L);
        dmScript::PushHash(L, fusion_object->m_Id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, object->GetOwner());
        lua_setfield(L, -2, "owner");
        CallListener(L, 3, 0);
    }

    FusionMessages::OnObjectOwnerChanged msg;
    msg.m_Id = fusion_object->m_Id;
    msg.m_Owner = object->GetOwner();
    dmGameObject::Result result = PostDDF(&msg, fusion_object->m_Id);
}
static void Fusion_OnPredictionOverride(FusionCore::ObjectRoot* object)
{
    dmLogInfo("Fusion_OnPredictionOverride");
    CallListener(g_FusionEventOnPredictionOverride);
}
static void Fusion_OnLobbyStats(const std::vector<PhotonMatchmaking::LobbyStats>& lobbyStats)
{
    dmLogInfo("Fusion_OnLobbyStats");
    for (int index = 0; index < lobbyStats.size(); index++)
    {
        PhotonMatchmaking::LobbyStats lobby = lobbyStats[index];
        dmLogInfo("Fusion_OnLobbyStats %d %s", index, (char*)lobby.name.c_str());
    }
    CallListener(g_FusionEventOnLobbyStats);
}
static void Fusion_OnRoomJoined()
{
    dmLogInfo("Fusion_OnRoomJoined local player: %d", g_Ctx->m_FusionClient->LocalPlayerId());
    lua_State* L = SetupListener();
    if(L)
    {
        const char* name = (const char*)g_Ctx->m_FusionClient->GetRealtimeClient().GetCurrentRoom()->GetName().c_str();

        dmScript::PushHash(L, g_FusionEventOnRoomJoined);
        lua_newtable(L);
        lua_pushstring(L, name);
        lua_setfield(L, -2, "name");
        CallListener(L, 3, 0);
    }
}
static void Fusion_OnRoomLeft()
{
    dmLogInfo("Fusion_OnRoomLeft");
    CallListener(g_FusionEventOnRoomLeft);
}
static void Fusion_OnRpc(FusionCore::Rpc& rpc)
{
    dmLogInfo("Fusion_OnRpc");
    lua_State* L = SetupListener();
    if(L)
    {
        dmScript::PushHash(L, g_FusionEventOnRpc);

        lua_newtable(L);
        lua_pushinteger(L, rpc.Id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, rpc.OriginPlayer);
        lua_setfield(L, -2, "origin_player");
        lua_pushinteger(L, rpc.TargetPlayer);
        lua_setfield(L, -2, "target_player");
        lua_pushinteger(L, rpc.TargetObject.Origin);
        lua_setfield(L, -2, "target_object_player");
        lua_pushinteger(L, rpc.TargetObject.Counter);
        lua_setfield(L, -2, "target_object_counter");
        dmScript::PushHash(L, dmhash_t(rpc.EventHash));
        lua_setfield(L, -2, "event");
        lua_pushlstring(L, (char*)rpc.Bytes.Ptr, rpc.Bytes.Length);
        lua_setfield(L, -2, "bytes");

        CallListener(L, 3, 0);
    }
}
static void Fusion_OnRpcError(FusionCore::Rpc& rpc)
{
    dmLogInfo("Fusion_OnRpcError");
    lua_State* L = SetupListener();
    if(L)
    {
        dmScript::PushHash(L, g_FusionEventOnRpc);

        lua_newtable(L);
        lua_pushinteger(L, rpc.Id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, rpc.OriginPlayer);
        lua_setfield(L, -2, "origin_player");
        lua_pushinteger(L, rpc.TargetPlayer);
        lua_setfield(L, -2, "target_player");
        lua_pushinteger(L, rpc.TargetObject.Origin);
        lua_setfield(L, -2, "target_object_player");
        lua_pushinteger(L, rpc.TargetObject.Counter);
        lua_setfield(L, -2, "target_object_counter");
        dmScript::PushHash(L, dmhash_t(rpc.EventHash));
        lua_setfield(L, -2, "event");
        lua_pushlstring(L, (char*)rpc.Bytes.Ptr, rpc.Bytes.Length);
        lua_setfield(L, -2, "bytes");
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "error");

        CallListener(L, 3, 0);
    }
}
static void Fusion_OnMapChange(const std::unordered_map<FusionCore::Map, FusionCore::Data> data, bool b)
{
    dmLogInfo("Fusion_OnMapChange");

    lua_State* L = SetupListener();
    if(L)
    {
        dmScript::PushHash(L, g_FusionEventOnMapChange);

        lua_newtable(L);
        // lua_pushinteger(L, index);
        // lua_setfield(L, -2, "index");
        // lua_pushinteger(L, sequence);
        // lua_setfield(L, -2, "sequence");
        // lua_pushlstring(L, (char*)data.Ptr, data.Length);
        // lua_setfield(L, -2, "data");

        CallListener(L, 3, 0);
    }
}
void Fusion_OnDestroyedMapActor(FusionCore::ObjectId id)
{
    dmLogInfo("Fusion_OnDestroyedMapActor");
    lua_State* L = SetupListener();
    if(L)
    {
        dmScript::PushHash(L, g_FusionEventOnDestroyedMapActor);

        lua_newtable(L);
        lua_pushinteger(L, id.Origin);
        lua_setfield(L, -2, "id");

        CallListener(L, 3, 0);
    }
}
void Fusion_OnInterestEnter(FusionCore::ObjectRoot* object)
{
    dmLogInfo("Fusion_OnInterestEnter");
    FusionObject* fusion_object = FindFusionObject(object);
    if (!fusion_object)
    {
        dmhash_t id = dmGameObject::CreateInstanceId();
        fusion_object = CreateFusionObject(id, object);
    }
    if (fusion_object)
    {
        CallListener(g_FusionEventOnInterestEnter, fusion_object->m_Id);
    }
}
void Fusion_OnInterestExit(FusionCore::ObjectRoot* object)
{
    dmLogInfo("Fusion_OnInterestExit");
    dmhash_t id = DeleteFusionObject(object);
    if (id)
    {
        CallListener(g_FusionEventOnInterestExit, id);
    }
}
void Fusion_OnForcedDisconnect(std::string message)
{
    dmLogInfo("Fusion_OnForcedDisconnect");
    CallListener(g_FusionEventOnForcedDisconnect, message.c_str());
}
void Fusion_OnFusionStart()
{
    dmLogInfo("Fusion_OnFusionStart");
    g_Ctx->m_IsStarted = true;
    CallListener(g_FusionEventOnFusionStart);
}


/******
 * Fusion update lifecycle
 *************************/

// send object properties
void Fusion_TickBeforeFrameEnd()
{
    dmLogInfo("TickBeforeFrameEnd");
    dmHashTable<dmhash_t, FusionObject*>::Iterator iter = g_Ctx->m_FusionObjects.GetIterator();
    while(iter.Next())
    {
        dmhash_t id = iter.GetKey();
        FusionObject* object = iter.GetValue();
        if (g_Ctx->m_FusionClient->IsOwner(object->m_SharedObject))
        {
            SerializeFusionObject(object);
        }
    }
}

// updated object properties with received data
void Fusion_TickAfterFrameBegin(double dt)
{
    dmLogInfo("Fusion_TickAfterFrameBegin");
    dmHashTable<dmhash_t, FusionObject*>::Iterator iter = g_Ctx->m_FusionObjects.GetIterator();
    while(iter.Next())
    {
        FusionObject* object = iter.GetValue();
        if (!g_Ctx->m_FusionClient->IsOwner(object->m_SharedObject))
        {
            DeserializeFusionObject(object);
        }
    }
}


/******
 * Fusion Lua API functions
 *************************/

static void DoInit(lua_State* L, const char* appId, const char* appVersion)
{
    dmLogInfo("DoInit");
    g_Ctx->m_FusionObjects.SetCapacity(100, 100);

    if (g_Ctx->m_FusionClient)
    {
        delete g_Ctx->m_FusionClient;
    }

    PhotonMatchmaking::ClientConstructOptions options = PhotonMatchmaking::ClientConstructOptions();
    options.appId = ToStringType(appId);
    options.appVersion = ToStringType(appVersion);
    PhotonMatchmaking::RealtimeClient* realtimeClient = new PhotonMatchmaking::RealtimeClient(options);

    g_Ctx->m_FusionClient = new FusionCore::Client(*realtimeClient);
    g_Ctx->m_FusionClient->OnForcedDisconnect.Subscribe(Fusion_OnForcedDisconnect);
    g_Ctx->m_FusionClient->OnFusionStart.Subscribe(Fusion_OnFusionStart);
    g_Ctx->m_FusionClient->OnObjectReady.Subscribe(Fusion_OnObjectReady);
    g_Ctx->m_FusionClient->OnInterestEnter.Subscribe(Fusion_OnInterestEnter);
    g_Ctx->m_FusionClient->OnInterestExit.Subscribe(Fusion_OnInterestExit);
    g_Ctx->m_FusionClient->OnSubObjectCreated.Subscribe(Fusion_OnSubObjectCreated);
    g_Ctx->m_FusionClient->OnObjectDestroyed.Subscribe(Fusion_OnObjectDestroyed);
    g_Ctx->m_FusionClient->OnSubObjectDestroyed.Subscribe(Fusion_OnSubObjectDestroyed);
    g_Ctx->m_FusionClient->OnObjectForceAlive.Subscribe(Fusion_OnObjectForceAlive);
    g_Ctx->m_FusionClient->OnSubObjectForceAlive.Subscribe(Fusion_OnSubObjectForceAlive);
    g_Ctx->m_FusionClient->OnRpc.Subscribe(Fusion_OnRpc);
    g_Ctx->m_FusionClient->OnRpcError.Subscribe(Fusion_OnRpcError);
    g_Ctx->m_FusionClient->OnMapChange.Subscribe(Fusion_OnMapChange);
    g_Ctx->m_FusionClient->OnObjectOwnerAssigned.Subscribe(Fusion_OnObjectOwnerAssigned);
    g_Ctx->m_FusionClient->OnObjectOwnerChanged.Subscribe(Fusion_OnObjectOwnerChanged);
    g_Ctx->m_FusionClient->OnPredictionOverride.Subscribe(Fusion_OnPredictionOverride);
    g_Ctx->m_FusionClient->OnDestroyedMapActor.Subscribe(Fusion_OnDestroyedMapActor);
    g_Ctx->m_FusionClient->GetRealtimeClient().OnLobbyStats.Subscribe(Fusion_OnLobbyStats);
    g_Ctx->m_FusionClient->GetRealtimeClient().OnRoomJoined.Subscribe(Fusion_OnRoomJoined);
    g_Ctx->m_FusionClient->GetRealtimeClient().OnRoomLeft.Subscribe(Fusion_OnRoomLeft);

    g_Ctx->m_FusionDefoldLogOutput = new FusionDefoldLogOutput();

    dmGameObject::HInstance caller_instance = dmScript::CheckGOInstance(L);
    g_Ctx->m_Collection = dmGameObject::GetCollection(caller_instance);
    g_Ctx->m_Scriptc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("scriptc"));
    g_Ctx->m_Spritec = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("spritec"));
    g_Ctx->m_Modelc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("modelc"));
    g_Ctx->m_Collisionobjectc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("collisionobjectc"));
    g_Ctx->m_Particlefxc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("particlefxc"));
    g_Ctx->m_Labelc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("labelc"));
}

/** Initialize Fusion
 * @name init
 * @string app_id
 * @string app_version
 */
static int Init(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);

    dmLogInfo("Init");

    const char* appId = luaL_checkstring(L, 1);
    const char* appVersion = luaL_checkstring(L, 2);
    DoInit(L, appId, appVersion);

    return 0;
}

/** Initialize Fusion from game.project settings
 * @name init_from_settings
 */
static int InitFromSettings(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);

    dmLogInfo("InitFromSettings");

    const char* appId = dmConfigFile::GetString(g_Ctx->m_ConfigFile, "fusion.app_id", "");
    const char* appVersion = dmConfigFile::GetString(g_Ctx->m_ConfigFile, "fusion.app_id", "");
    DoInit(L, appId, appVersion);

    return 0;
}

/** Connect Fusion
 * @name connect
 * @string [user]
 */
static int Connect(lua_State* L)
{
    dmLogInfo("Connect");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    dmLogInfo("Connect IsConnected");
    if (g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is already connected");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmLogInfo("Connect ConnectOptions");
    PhotonMatchmaking::ConnectOptions connectOptions = PhotonMatchmaking::ConnectOptions();
    connectOptions.useBackgroundSendReceiveThread = false;

    const char* username = 0x0;
    if (lua_isstring(L, 1))
    {
        username = luaL_checkstring(L, 1);
    }
    connectOptions.username = (const PhotonCommon::CharType*)username;

    g_Ctx->m_FusionClient->GetRealtimeClient().Connect(connectOptions);
    dmLogInfo("Connect calling connect done");

    // const char* region = dmConfigFile::GetString(g_Ctx->m_ConfigFile, "fusion.default_region", "eu");
    // if (lua_isstring(L, 2))
    // {
    //     region = luaL_checkstring(L, 2);
    // }
    // g_Ctx->m_FusionClient->GetRealtimeClient().SelectRegion(ToStringType(region));

    return 0;
}

/** Disonnect Fusion
 * @name disconnect
 */
static int Disonnect(lua_State* L)
{
    dmLogInfo("Disonnect");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is already disconnected");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);
    g_Ctx->m_FusionClient->GetRealtimeClient().Disconnect();
    return 0;
}


/** Reconnect Fusion
 * @name reconnect
 */
static int Reconnect(lua_State* L)
{
    dmLogInfo("Reconnect");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is already connected");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);
    g_Ctx->m_FusionClient->GetRealtimeClient().Reconnect();
    return 0;
}

/** Start Fusion sync
 * @name start
 */
static int Start(lua_State* L)
{
    dmLogInfo("Start");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is not connected");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom())
    {
        luaL_error(L, "Fusion is not in a room");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);
    g_Ctx->m_FusionClient->Start();
    return 0;
}

/** Stop Fusion sync
 * @name stop
 */
static int Stop(lua_State* L)
{
    dmLogInfo("Stop");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is not connected");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom())
    {
        luaL_error(L, "Fusion is not in a room");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);
    g_Ctx->m_FusionClient->Stop();
    return 0;
}


/** Get connection state
 * @name get_state
 * @treturn number Connection state
 */
static int GetState(lua_State* L)
{
    dmLogInfo("GetState");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);
    PhotonMatchmaking::ConnectionState state = g_Ctx->m_FusionClient->GetRealtimeClient().GetState();
    lua_pushnumber(L, (lua_Number)state);
    return 1;
}

/** Get disconnect cause
 * @name get_disconnect_cause
 * @treturn number Disconnect cause
 */
static int GetDisconnectCause(lua_State* L)
{
    dmLogInfo("GetDisconnectCause");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);
    PhotonMatchmaking::DisconnectCause cause = g_Ctx->m_FusionClient->GetRealtimeClient().GetDisconnectCause();
    lua_pushnumber(L, (lua_Number)cause);
    return 1;
}




static PhotonMatchmaking::CreateRoomOptions ParseCreateRoomOptions(lua_State* L, int index)
{
    dmLogInfo("ParseCreateRoomOptions");
    PhotonMatchmaking::CreateRoomOptions options = PhotonMatchmaking::CreateRoomOptions();
    if (lua_type(L, index) == LUA_TTABLE)
    {
        lua_pushnil(L);
        while (lua_next(L, index) != 0)
        {
            const char* key = luaL_checkstring(L, -2);
            if (strcmp("is_visible", key) == 0)
            {
                options.isVisible = lua_toboolean(L, -1);
            }
            else if (strcmp("is_open", key) == 0)
            {
                options.isOpen = lua_toboolean(L, -1);
            }
            else if (strcmp("max_players", key) == 0)
            {
                options.maxPlayers = lua_tonumber(L, -1);
            }
            else if (strcmp("player_ttl_ms", key) == 0)
            {
                options.playerTtlMs = lua_tonumber(L, -1);
            }
            else if (strcmp("empty_room_ttl_ms", key) == 0)
            {
                options.emptyRoomTtlMs = lua_tonumber(L, -1);
            }
            else if (strcmp("lobby_name", key) == 0)
            {
                const char* lobby_name = lua_tostring(L, -1);
                options.lobbyName = ToStringType(lobby_name);
            }
            else if (strcmp("expected_users", key) == 0)
            {
                LuaTableToStdStringVector(L, lua_gettop(L), options.expectedUsers);
            }
            else if (strcmp("plugins", key) == 0)
            {
                LuaTableToStdStringVector(L, lua_gettop(L), options.plugins);
            }
            else if (strcmp("lobby_properties", key) == 0)
            {
                LuaTableToStdStringVector(L, lua_gettop(L), options.lobbyProperties);
            }
            else if (strcmp("custom_properties", key) == 0)
            {
                LuaTableToPropertyMap(L, lua_gettop(L), options.customProperties);
            }
            else
            {
                dmLogInfo("Unknown room option %s", key);
            }
            lua_pop(L, 1); // pop value
        }
    }
    return options;
}

static PhotonMatchmaking::MatchmakingOptions ParseMatchmakingOptions(lua_State* L, int index)
{
    dmLogInfo("ParseMatchmakingOptions");
    PhotonMatchmaking::MatchmakingOptions options = PhotonMatchmaking::MatchmakingOptions();
    if (lua_type(L, index) == LUA_TTABLE)
    {
        lua_pushnil(L);
        while (lua_next(L, index) != 0)
        {
            const char* key = luaL_checkstring(L, -2);
            if (strcmp("max_players", key) == 0)
            {
                options.maxPlayers = lua_tonumber(L, -1);
            }
            else if (strcmp("lobby_name", key) == 0)
            {
                options.lobbyName = ToStringType(lua_tostring(L, -1));
            }
            else if (strcmp("expected_users", key) == 0)
            {
                LuaTableToStdStringVector(L, lua_gettop(L), options.expectedUsers);
            }
            else
            {
                dmLogInfo("Unknown matchmaking option %s", key);
            }
            lua_pop(L, 1); // pop value
        }
    }
    return options;
}

static PhotonMatchmaking::JoinRoomOptions ParseJoinRoomOptions(lua_State* L, int index)
{
    dmLogInfo("ParseJoinRoomOptions");
    PhotonMatchmaking::JoinRoomOptions options = PhotonMatchmaking::JoinRoomOptions();
    if (lua_type(L, index) == LUA_TTABLE)
    {
        lua_pushnil(L);
        while (lua_next(L, index) != 0)
        {
            const char* key = luaL_checkstring(L, -2);
            if (strcmp("rejoin", key) == 0)
            {
                options.rejoin = lua_toboolean(L, -1);
            }
            else if (strcmp("cache_slice_index", key) == 0)
            {
                options.cacheSliceIndex = lua_tonumber(L, -1);
            }
            else if (strcmp("expected_users", key) == 0)
            {
                LuaTableToStdStringVector(L, -1, options.expectedUsers);
            }
            else
            {
                dmLogInfo("Unknown matchmaking option %s", key);
            }
            lua_pop(L, 1); // pop value
        }
    }
    return options;
}

/** Join or create random room
 * @name join_or_create_room_random
 * @table create_room_options
 * @table matchmaking_options
 */
static int JoinRandomOrCreateRoom(lua_State* L)
{
    dmLogInfo("JoinRandomOrCreateRoom");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is not connected");
        return 0;
    }
    if (g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom())
    {
        luaL_error(L, "Fusion is already in room");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    PhotonMatchmaking::CreateRoomOptions roomOptions = ParseCreateRoomOptions(L, 1);
    PhotonMatchmaking::MatchmakingOptions matchmakingOptions = ParseMatchmakingOptions(L, 2);

    dmLogInfo("JoinRandomOrCreateRoom name = %s", (const char*)roomOptions.lobbyName.c_str());
    dmLogInfo("JoinRandomOrCreateRoom data = %s", (const char*)roomOptions.lobbyName.data());
    g_Ctx->m_FusionClient->GetRealtimeClient().JoinRandomOrCreateRoom(roomOptions, matchmakingOptions);

    return 0;
}

/** Join random room
 * @name join_room_random
 * @table matchmaking_options
 */
static int JoinRandomRoom(lua_State* L)
{
    dmLogInfo("JoinRandomRoom");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is not connected");
        return 0;
    }
    if (g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom())
    {
        luaL_error(L, "Fusion is already in room");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    PhotonMatchmaking::MatchmakingOptions matchmakingOptions = ParseMatchmakingOptions(L, 1);

    dmLogInfo("JoinRandomRoom");
    g_Ctx->m_FusionClient->GetRealtimeClient().JoinRandomRoom(matchmakingOptions);

    return 0;
}

/** Join room
 * @name join_room
 * @string room_name
 * @table join_room_options
 */
static int JoinRoom(lua_State* L)
{
    dmLogInfo("JoinRoom");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is not connected");
        return 0;
    }
    if (g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom())
    {
        luaL_error(L, "Fusion is already in room");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    const char* roomName = luaL_checkstring(L, 1);
    PhotonMatchmaking::JoinRoomOptions joinOptions = ParseJoinRoomOptions(L, 2);

    dmLogInfo("JoinRoom name = %s", roomName);
    g_Ctx->m_FusionClient->GetRealtimeClient().JoinRoom(ToStringType(roomName), joinOptions);

    return 0;
}

/** Join or create room
 * @name join_or_create_room
 * @string room_name
 * @table create_room_options
 * @table join_room_options
 */
static int JoinOrCreateRoom(lua_State* L)
{
    dmLogInfo("JoinOrCreateRoom");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is not connected");
        return 0;
    }
    if (g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom())
    {
        luaL_error(L, "Fusion is already in room");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    const char* roomName = luaL_checkstring(L, 1);
    PhotonMatchmaking::CreateRoomOptions createOptions = ParseCreateRoomOptions(L, 2);
    PhotonMatchmaking::JoinRoomOptions joinOptions = ParseJoinRoomOptions(L, 3);

    dmLogInfo("JoinOrCreateRoom name = %s", roomName);
    g_Ctx->m_FusionClient->GetRealtimeClient().JoinOrCreateRoom(ToStringType(roomName), createOptions, joinOptions);

    return 0;
}


/** Create room
 * @name create_room
 * @string room_name
 * @table create_room_options
 */
static int CreateRoom(lua_State* L)
{
    dmLogInfo("CreateRoom");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is not connected");
        return 0;
    }
    if (g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom())
    {
        luaL_error(L, "Fusion is already in room");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    const char* roomName = luaL_checkstring(L, 1);
    PhotonMatchmaking::CreateRoomOptions createOptions = ParseCreateRoomOptions(L, 2);

    dmLogInfo("CreateRoom name = %s", roomName);
    g_Ctx->m_FusionClient->GetRealtimeClient().CreateRoom(ToStringType(roomName), createOptions);

    return 0;
}

/** Leave room
 * @name leave_room
 * @bool will_come_back
 * @bool send_auth_cookie
 */
static int LeaveRoom(lua_State* L)
{
    dmLogInfo("LeaveRoom");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is not connected");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom())
    {
        luaL_error(L, "Fusion is not in room");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    bool will_come_back = lua_toboolean(L, 1);
    bool send_auth_cookie = lua_toboolean(L, 2);

    g_Ctx->m_FusionClient->GetRealtimeClient().LeaveRoom(will_come_back, send_auth_cookie);

    return 0;
}


/** Check if Fusion is connected
 * @name is_connected
 * @treturn boolean connected
 */
static int IsConnected(lua_State* L)
{
    // dmLogInfo("IsConnected");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);
    bool connected = g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected();
    lua_pushboolean(L, connected);
    return 1;
}


/** Check if Fusion has config and is connected
 * @name is_running
 * @treturn boolean running
 */
static int IsRunning(lua_State* L)
{
    dmLogInfo("IsRunning");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);
    bool running = g_Ctx->m_FusionClient->IsRunning();
    lua_pushboolean(L, running);
    return 1;
}

/** Check if Fusion is started. Fusion is considered started after a call to
 * `fusion.start()` has received a `EventOnFusionStart` event.
 * @name is_started
 * @treturn boolean started
 */
static int IsStarted(lua_State* L)
{
    dmLogInfo("IsStarted");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);
    lua_pushboolean(L, g_Ctx->m_IsStarted);
    return 1;
}


/** Check if Fusion is in a room
 * @name is_in_room
 * @treturn boolean in_room
 */
static int IsInRoom(lua_State* L)
{
    dmLogInfo("IsInRoom");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);
    bool in_room = g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom();
    lua_pushboolean(L, in_room);
    return 1;
}


/** Enable/disable debugging
 * @name enable_debug
 * @boolean enable
 */
static int EnableDebug(lua_State* L)
{
    dmLogInfo("EnableDebug");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);
    bool enable = lua_toboolean(L, 1);
    if (enable)
    {
        PhotonCommon::AddLogOutput(g_Ctx->m_FusionDefoldLogOutput);
        PhotonCommon::SetLogLevelsFromBitmask(0xFF);
        PhotonCommon::LogEnable(PhotonCommon::LogLevel::Trace);
    }
    else
    {
        PhotonCommon::RemoveLogOutput(g_Ctx->m_FusionDefoldLogOutput);
        PhotonCommon::SetLogLevelsFromBitmask(0x0);
        PhotonCommon::LogDisable(PhotonCommon::LogLevel::Trace);
    }
    return 0;
}




/** Register a scene object
 * @name register_scene_object
 * @number scene
 * @string factory_url
 * @number owner_mode
 * @string [id]
 */
static int RegisterSceneObject(lua_State* L)
{
    dmLogInfo("RegisterSceneObject");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    DM_LUA_STACK_CHECK(L, 0);

    FusionCore::Map map = (FusionCore::Map)luaL_checknumber(L, 1);
    dmMessage::URL* factory_url = dmScript::CheckURL(L, 2);
    FusionCore::ObjectOwnerModes ownerMode = (FusionCore::ObjectOwnerModes)luaL_checknumber(L, 3);
    dmhash_t id = ResolveId(L, 4);

    uint8_t header[1000];
    size_t headerLength;
    size_t wordsCount;
    bool ok = BuildObjectHeader(id, factory_url, header, headerLength, wordsCount);
    if (!ok)
    {
        luaL_error(L, "Unable to build object header");
        return 0;
    }

    FusionCore::TypeRef type;
    type.Hash = 0;
    type.WordCount = wordsCount + FusionCore::Object::ExtraTailWords;

    bool alreadyPopulated;
    uint32_t engineFlags = 0;
    int32_t requiredObjectsCount = 0;
    dmLogInfo("Calling CreateSceneObject");
    FusionCore::ObjectRoot* object = g_Ctx->m_FusionClient->CreateMapObject(alreadyPopulated,
                                                                              wordsCount,
                                                                              type,
                                                                              (PhotonCommon::CharType*)header,
                                                                              headerLength,
                                                                              map,
                                                                              factory_url->m_Path,
                                                                              ownerMode,
                                                                              engineFlags,
                                                                              requiredObjectsCount);

    dmLogInfo("Calling CreateSceneObject - done");
    FusionObject* fusion_object = CreateFusionObject(id, object);
    if (!fusion_object)
    {
        luaL_error(L, "Unable to create object");
        return 0;
    }

    if (!alreadyPopulated)
    {
        SerializeFusionObject(fusion_object);
    }
    else
    {
        DeserializeFusionObject(fusion_object);
    }

    return 0;
}

/** Create a networked game object. This will spawn a game object in the same
 * way as when calling factory.create(). The function will also register the
 * spawned object with Fusion as if manually calling register_object()
 * @name spawn
 * @string factory_url
 * @vector3 [position] Initial position of created game object
 * @quat [rotation] Initial rotation of created game object
 * @number scene The scene to which this object belongs
 * @number owner_mode Owner mode of spawned object
 * @treturn hash Id of the spawned game object
 */
static int SpawnObject(lua_State* L)
{
    dmLogInfo("SpawnObject");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    dmGameObject::HInstance caller_instance = dmScript::CheckGOInstance(L);
    dmGameObject::HCollection collection = dmGameObject::GetCollection(caller_instance);

    dmGameSystem::HFactoryWorld factory_world;
    dmGameSystem::HFactoryComponent factory_component;
    dmMessage::URL factory_url;
    dmScript::GetComponentFromLua(L, 1, "factoryc", (dmGameObject::HComponentWorld*)&factory_world, (dmGameObject::HComponent*)&factory_component, &factory_url);

    dmVMath::Point3 position;
    if (!lua_isnoneornil(L, 2))
    {
        position = dmVMath::Point3(*dmScript::CheckVector3(L, 2));
    }
    else
    {
        position = dmGameObject::GetWorldPosition(caller_instance);
    }

    dmVMath::Quat rotation;
    if (!lua_isnoneornil(L, 3))
    {
        rotation = *dmScript::CheckQuat(L, 3);
    }
    else
    {
        rotation = dmGameObject::GetWorldRotation(caller_instance);
    }

    dmGameObject::HPropertyContainer properties = 0;
    dmVMath::Vector3 scale = dmGameObject::GetWorldScale(caller_instance);

    // Since the spawning will invoke any scripts on that new instance,
    // we need a way to restore the state
    dmScript::GetInstance(L);
    int ref = dmScript::Ref(L, LUA_REGISTRYINDEX);

    dmhash_t id = dmGameObject::CreateInstanceId();
    dmGameObject::HInstance instance;
    dmGameObject::Result r = dmGameSystem::CompFactorySpawn(
        factory_world, factory_component, collection,
        id, 
        position, rotation, scale,
        properties, &instance);

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    dmScript::SetInstance(L);
    dmScript::Unref(L, LUA_REGISTRYINDEX, ref);

    if (dmGameObject::RESULT_OK != r)
    {
        luaL_error(L, "Unable to spawn game object");
        return 0;
    }

    uint32_t scene = (uint32_t)luaL_checknumber(L, 4);
    FusionCore::ObjectOwnerModes ownerMode = (FusionCore::ObjectOwnerModes)luaL_checknumber(L, 5);

    r = DoRegisterObject(id, &factory_url, scene, ownerMode);
    if (dmGameObject::RESULT_OK != r)
    {
        luaL_error(L, "Unable to spawn game object");
        dmGameObject::Delete(collection, instance, true);
        return 0;
    }

    dmScript::PushHash(L, id);

    Fusion_OnObjectReady(GetSharedObject(id));

    return 1;
}

/** Destroy a networked object.
 * @name despawn
 * @string [id]
 */
static int DespawnObject(lua_State* L)
{
    dmLogInfo("DespawnObject");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = ResolveId(L, 1);
    const FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        DeleteFusionObject(object);
    }

    return 0;
}


/** Register an object
 * @name register_object
 * @number scene
 * @string factory_url
 * @number owner_mode
 * @string [id]
 */
static int RegisterObject(lua_State* L)
{
    dmLogInfo("RegisterObject");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    uint32_t scene = (uint32_t)luaL_checknumber(L, 1);
    dmMessage::URL* factory_url = dmScript::CheckURL(L, 2);
    FusionCore::ObjectOwnerModes ownerMode = (FusionCore::ObjectOwnerModes)luaL_checknumber(L, 3);
    dmhash_t id = ResolveId(L, 4);

    dmLogInfo("RegisterObject %d", ownerMode);
    dmGameObject::Result r = DoRegisterObject(id, factory_url, scene, ownerMode);
    if (dmGameObject::RESULT_OK != r)
    {
        luaL_error(L, "Unable to create object");
        return 0;
    }

    return 0;
}

/** Unregister a previously registered object
 * @name unregister_object
 * @string [id]
 */
static int UnregisterObject(lua_State* L)
{
    dmLogInfo("UnregisterObject");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = ResolveId(L, 1);
    dmLogInfo("DestroyObject id: %s", dmHashReverseSafe64(id));
    FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        g_Ctx->m_FusionObjects.Erase(id);
        g_Ctx->m_FusionClient->DestroyObjectLocal(object, true);
    }
    return 0;
}

/** Change map
 * @name map_change
 * @number index
 * @number sequence
 * @string data
 */
static int MapChange(lua_State* L)
{
    dmLogInfo("MapChange");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    const char* data = luaL_checkstring(L, 1);
    g_Ctx->m_FusionClient->MapChange(ToCharType(data));

    return 0;
}

/** Send RPC
 * @name rpc
 * @number target_player 0 = all, specific PlayerId = targeted
 * @hash event
 * @string data
 * @treturn boolean ok
 */
static int SendRpc(lua_State* L)
{
    dmLogInfo("SendRpc");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    FusionCore::PlayerId target_player = (FusionCore::PlayerId)luaL_checknumber(L, 1);
    FusionCore::ObjectId target_object;
    dmhash_t event = dmScript::CheckHashOrString(L, 2);

    size_t data_length;
    const char* data = luaL_checklstring(L, 3, &data_length);

    uint64_t id = 1024; // User RPC

    FusionCore::Rpc rpc = g_Ctx->m_FusionClient->CreateUserRpc(id, target_player, target_object, event, data, data_length);

    bool ok = g_Ctx->m_FusionClient->SendUserRpc(rpc);
    lua_pushboolean(L, ok);
    return 1;
}

/** Set event listener
 * @name on_event
 * @function listener
 */
static int OnEvent(lua_State* L)
{
    dmLogInfo("OnEvent");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    g_FusionEventCallback = dmScript::CreateCallback(L, 1);

    return 0;
}

/**
 * Get the player id of the local client
 * @name get_local_player_id
 * @treturn number The player id of the local client
 */
static int GetLocalPlayerId(lua_State* L)
{
    dmLogInfo("GetLocalPlayerId");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    FusionCore::PlayerId id = g_Ctx->m_FusionClient->LocalPlayerId();
    lua_pushinteger(L, id);
    return 1;
}

/**
 * Get the player id of the current owner of an object
 * @name get_owner_id
 * @string [id] Id of the object to get the owner for
 * @treturn number The player id of the object's owner
 */
static int GetOwnerId(lua_State* L)
{
    dmLogInfo("GetOwnerId");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    dmhash_t id = ResolveId(L, 1);
    const FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        FusionCore::PlayerId owner = g_Ctx->m_FusionClient->GetOwner(object);
        lua_pushinteger(L, owner);
    }
    else
    {
        lua_pushnil(L);
    }
    return 1;
}


/**
 * Check if this client has authority over a game object. Use this to decide if
 * user input should be handled or not.
 * @name has_authority
 * @string [id]
 * @treturn boolean authority Returns true if the client has authority
 */
static int HasAuthority(lua_State* L)
{
    dmLogInfo("HasAuthority");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    dmhash_t id = ResolveId(L, 1);
    const FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        bool is_owner = g_Ctx->m_FusionClient->IsOwner(object);
        lua_pushboolean(L, is_owner);
    }
    else
    {
        lua_pushboolean(L, false);
    }
    return 1;
}


/**
 * Check if an object has an owner
 * @name has_owner
 * @string [id] Id of the object
 * @treturn boolean True if the object has an owner
 */
static int HasOwner(lua_State* L)
{
    dmLogInfo("HasOwner");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    dmhash_t id = ResolveId(L, 1);
    const FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        bool has_owner = g_Ctx->m_FusionClient->HasOwner(object);
        lua_pushboolean(L, has_owner);
    }
    else
    {
        lua_pushboolean(L, 0);
    }
    return 1;
}

/**
 * Signal desire for the local client to own an object
 * @name want_authority
 * @boolean claim_ownership
 * @string [id] Id of the object to own
 */
static int WantAuthority(lua_State* L)
{
    dmLogInfo("WantAuthority");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    bool claim = lua_toboolean(L, 1);
    dmhash_t id = ResolveId(L, 2);
    FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        if (claim)
        {
            dmLogInfo("SetWantOwner");
            g_Ctx->m_FusionClient->SetWantOwner(object);
        }
        else
        {
            dmLogInfo("SetDontWantOwner");
            g_Ctx->m_FusionClient->SetDontWantOwner(object);
        }
    }
    else
    {
        luaL_error(L, "Unable to find object");
    }
    return 0;
}

/**
 * Explicitly clear the ownership cooldown
 * @name clear_owner_cooldown
 * @string [id] Id of the object to clear cooldown for
 */
static int ClearOwnerCooldown(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = ResolveId(L, 1);
    FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        g_Ctx->m_FusionClient->ClearOwnerCooldown(object);
    }
    return 0;
}

/**
 * Set the send rate of an object. This decided how much bandwidth to allocate
 * @name set_room_send_rate
 * @number send_rate 
 * @string [id] Id of the object to send rate for
 */
static int SetRoomSendRate(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = ResolveId(L, 2);
    FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        int32_t rate = (int32_t)luaL_checknumber(L, 1);
        g_Ctx->m_FusionClient->SetRoomSendRate(object, rate);
    }
    return 0;
}

/**
 * Set the local send rate divisor for an object. A value of `1` means the
 * object sends every tick (highest rate). A value of `16` means it sends every
 * 16th tick (lowest rate). This is a client-side optimization -- the object
 * still exists on all clients but consumes less bandwidth when you are not the
 * active authority.
 * @name set_local_send_rate
 * @number send_rate 
 * @string [id] Id of the object to send rate for
 */
static int SetLocalSendRate(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = ResolveId(L, 2);
    FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        int32_t rate = (int32_t)luaL_checknumber(L, 1);
        g_Ctx->m_FusionClient->SetLocalSendRate(object, rate);
    }
    return 0;
}

/**
 * Reset the send rate of an object.
 * @name reset_room_send_rate
 * @string [id] Id of the object to reset send rate for
 */
static int ResetRoomSendRate(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = ResolveId(L, 1);
    FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        g_Ctx->m_FusionClient->ResetRoomSendRate(object);
    }
    return 0;
}


/**
 * Get player count
 * @name player_count
 * @treturn number count Number of players
 */
static int PlayerCount(lua_State* L)
{
    dmLogInfo("PlayerCount");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    int32_t count = g_Ctx->m_FusionClient->PlayerCount();
    lua_pushinteger(L, count);
    return 1;
}

/**
 * Get if this client is the room's master
 * @name is_master_client
 * @treturn boolean master True if this client is the room's master
 */
static int IsMasterClient(lua_State* L)
{
    dmLogInfo("IsMasterClient");
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    bool is_master = g_Ctx->m_FusionClient->IsMasterClient();
    lua_pushboolean(L, is_master);
    return 1;
}


/**
 * Get round trip time
 * @name get_rtt
 * @treturn number rtt Round trip time in seconds
 */
static int GetRtt(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    double rtt = g_Ctx->m_FusionClient->GetRtt();
    lua_pushnumber(L, rtt);
    return 1;
}

/**
 * Get raw offset between server time and local time. A positive value means the
 * server clock is ahead of the local clock.
 * @name network_time_diff
 * @treturn number diff Time diff
 */
static int NetworkTimeDiff(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    double diff = g_Ctx->m_FusionClient->NetworkTimeDiff();
    lua_pushnumber(L, diff);
    return 1;
}

/**
 * Set global visibility key for an object
 * @name set_global_interest_key
 * @hash id Object to set global visibility key for
 */
static int SetGlobalInterestKey(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = ResolveId(L, 1);
    FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        g_Ctx->m_FusionClient->SetGlobalInterestKey(object);
    }
    return 0;
}

/**
 * Set area visibility key for an object
 * @name set_area_interest_key
 * @hash key Area key
 * @hash id Object to set area visibility key for
 */
static int SetAreaInterestKey(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t key = dmScript::CheckHashOrString(L, 1);
    dmhash_t id = ResolveId(L, 2);
    FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        g_Ctx->m_FusionClient->SetAreaInterestKey(object, key);
    }
    return 0;
}

/**
 * Set user visibility key for an object
 * @name set_user_interest_key
 * @hash key User key
 * @hash id Object to set user visibility key for
 */
static int SetUserInterestKey(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t key = dmScript::CheckHashOrString(L, 1);
    dmhash_t id = ResolveId(L, 2);
    FusionCore::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        g_Ctx->m_FusionClient->SetUserInterestKey(object, key);
    }
    return 0;
}

/**
 * Set area visibility keys
 * @name set_area_keys
 * @table keys Area keys to set. Stored as a table of key value pairs (area_key->send_rate)
 */
static int SetAreaKeys(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    std::vector<std::tuple<uint64_t, uint8_t>> keys;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0)
    {
        dmhash_t area_key = dmScript::CheckHashOrString(L, -2);
        uint8_t send_rate = (uint8_t)luaL_checknumber(L, -1);
        keys.insert(keys.end(), std::tuple<uint64_t, uint8_t>(area_key, send_rate));
        lua_pop(L, 1); // pop value
    }

    FusionCore::InterestKeySet& key_set = g_Ctx->m_FusionClient->GetLocalInterestKeys();
    key_set.SetAreaKeys(keys);
    return 0;
}

/**
 * Add a user visibility key
 * @name add_user_key
 * @hash key User key to add
 */
static int AddUserKey(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t key = dmScript::CheckHashOrString(L, 1);

    FusionCore::InterestKeySet& key_set = g_Ctx->m_FusionClient->GetLocalInterestKeys();
    key_set.AddUserKey(key);
    return 0;
}

/**
 * Remove a user visibility key from an object
 * @name remove_user_key
 * @hash key User key to remove
 * @hash id Object to remove user visibility key from
 */
static int RemoveUserKey(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t key = dmScript::CheckHashOrString(L, 1);

    FusionCore::InterestKeySet& key_set = g_Ctx->m_FusionClient->GetLocalInterestKeys();
    key_set.RemoveUserKey(key);
    return 0;
}

static const luaL_reg Module_methods[] = {
    { "init", Init },
    { "init_from_settings", InitFromSettings },
    { "connect", Connect },
    { "disconnect", Disonnect },
    { "reconnect", Reconnect },
    { "start", Start },
    { "stop", Stop },
    { "get_state", GetState },
    { "get_disconnect_cause", GetDisconnectCause },

    // room handling
    { "create_room", CreateRoom },
    { "leave_room", LeaveRoom },
    { "join_room", JoinRoom },
    { "join_or_create_room", JoinOrCreateRoom },
    { "join_room_random", JoinRandomRoom },
    { "join_or_create_room_random", JoinRandomOrCreateRoom },


    { "enable_debug", EnableDebug },
    { "get_local_player_id", GetLocalPlayerId },
    { "player_count", PlayerCount },
    { "get_rtt", GetRtt },
    { "network_time_diff", NetworkTimeDiff },
    
    // area of interest
    { "set_global_interest_key", SetGlobalInterestKey },
    { "set_area_interest_key", SetAreaInterestKey },
    { "set_user_interest_key", SetUserInterestKey },
    { "set_area_keys", SetAreaKeys },
    { "add_user_key", AddUserKey },
    { "remove_user_key", RemoveUserKey },

    // lifecycle
    { "spawn", SpawnObject },
    { "despawn", DespawnObject },
    { "register_object", RegisterObject },
    { "register_scene_object", RegisterSceneObject },
    { "unregister_object", UnregisterObject },
    { "map_change", MapChange },

    // rpc and events
    { "rpc", SendRpc },
    { "on_event", OnEvent },

    // connection state
    { "is_connected", IsConnected },
    { "is_started", IsStarted },
    { "is_running", IsRunning },
    { "is_in_room", IsInRoom },
    { "is_master_client", IsMasterClient },

    // ownership
    { "get_owner_id", GetOwnerId },
    { "has_owner", HasOwner },
    { "has_authority", HasAuthority },
    { "want_authority", WantAuthority },
    { "clear_owner_cooldown", ClearOwnerCooldown },
    { "set_local_send_rate", SetLocalSendRate },
    { "set_room_send_rate", SetRoomSendRate },
    { "reset_room_send_rate", ResetRoomSendRate },

    { 0, 0 }
};


static void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);
    luaL_register(L, MODULE_NAME, Module_methods);


    #define SETCONSTANT_NUMBER(name, val) \
    lua_pushnumber(L, (lua_Number) val); \
    lua_setfield(L, -2, #name);

    /**
     * OWNERMODE_TRANSACTION
     * @field OWNERMODE_TRANSACTION
     */
    SETCONSTANT_NUMBER(OWNERMODE_TRANSACTION, FusionCore::ObjectOwnerModes::Transaction)
    /**
     * OWNERMODE_PLAYERATTACHED
     * @field OWNERMODE_PLAYERATTACHED
     */
    SETCONSTANT_NUMBER(OWNERMODE_PLAYERATTACHED, FusionCore::ObjectOwnerModes::PlayerAttached)
    /**
     * OWNERMODE_DYNAMIC
     * @field OWNERMODE_DYNAMIC
     */
    SETCONSTANT_NUMBER(OWNERMODE_DYNAMIC, FusionCore::ObjectOwnerModes::Dynamic)
    /**
     * OWNERMODE_MASTERCLIENT
     * @field OWNERMODE_MASTERCLIENT
     */
    SETCONSTANT_NUMBER(OWNERMODE_MASTERCLIENT, FusionCore::ObjectOwnerModes::MasterClient)
    /**
     * OWNERMODE_GAMEGLOBAL
     * @field OWNERMODE_GAMEGLOBAL
     */
    SETCONSTANT_NUMBER(OWNERMODE_GAMEGLOBAL, FusionCore::ObjectOwnerModes::GameGlobal)

    /**
     * STATE_DISCONNECTED
     * @field STATE_DISCONNECTED
     */
    SETCONSTANT_NUMBER(STATE_DISCONNECTED, PhotonMatchmaking::ConnectionState::Disconnected)
    /**
     * STATE_CONNECTING
     * @field STATE_CONNECTING
     */
    SETCONSTANT_NUMBER(STATE_CONNECTING, PhotonMatchmaking::ConnectionState::Connecting)
    /**
     * STATE_CONNECTED
     * @field STATE_CONNECTED
     */
    SETCONSTANT_NUMBER(STATE_CONNECTED, PhotonMatchmaking::ConnectionState::Connected)
    /**
     * STATE_JOININGROOM
     * @field STATE_JOININGROOM
     */
    SETCONSTANT_NUMBER(STATE_JOININGROOM, PhotonMatchmaking::ConnectionState::JoiningRoom)
    /**
     * STATE_INROOM
     * @field STATE_INROOM
     */
    SETCONSTANT_NUMBER(STATE_INROOM, PhotonMatchmaking::ConnectionState::InRoom)
    /**
     * STATE_LEAVINGROOM
     * @field STATE_LEAVINGROOM
     */
    SETCONSTANT_NUMBER(STATE_LEAVINGROOM, PhotonMatchmaking::ConnectionState::LeavingRoom)
    /**
     * STATE_DISCONNECTING
     * @field STATE_DISCONNECTING
     */
    SETCONSTANT_NUMBER(STATE_DISCONNECTING, PhotonMatchmaking::ConnectionState::Disconnecting)


    /**
     * DISCONNECT_CAUSE_NONE
     * @field DISCONNECT_CAUSE_NONE
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_NONE, PhotonMatchmaking::DisconnectCause::None)
    /**
     * DISCONNECT_CAUSE_DISCONNECTBYSERVERUSERLIMIT
     * @field DISCONNECT_CAUSE_DISCONNECTBYSERVERUSERLIMIT
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_DISCONNECTBYSERVERUSERLIMIT, PhotonMatchmaking::DisconnectCause::DisconnectByServerUserLimit)
    /**
     * DISCONNECT_CAUSE_EXCEPTIONONCONNECT
     * @field DISCONNECT_CAUSE_EXCEPTIONONCONNECT
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_EXCEPTIONONCONNECT, PhotonMatchmaking::DisconnectCause::ExceptionOnConnect)
    /**
     * DISCONNECT_CAUSE_DISCONNECTBYSERVER
     * @field DISCONNECT_CAUSE_DISCONNECTBYSERVER
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_DISCONNECTBYSERVER, PhotonMatchmaking::DisconnectCause::DisconnectByServer)
    /**
     * DISCONNECT_CAUSE_DISCONNECTBYSERVERLOGIC
     * @field DISCONNECT_CAUSE_DISCONNECTBYSERVERLOGIC
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_DISCONNECTBYSERVERLOGIC, PhotonMatchmaking::DisconnectCause::DisconnectByServerLogic)
    /**
     * DISCONNECT_CAUSE_TIMEOUTDISCONNECT
     * @field DISCONNECT_CAUSE_TIMEOUTDISCONNECT
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_TIMEOUTDISCONNECT, PhotonMatchmaking::DisconnectCause::TimeoutDisconnect)
    /**
     * DISCONNECT_CAUSE_EXCEPTION
     * @field DISCONNECT_CAUSE_EXCEPTION
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_EXCEPTION, PhotonMatchmaking::DisconnectCause::Exception)
    /**
     * DISCONNECT_CAUSE_INVALIDAUTHENTICATION
     * @field DISCONNECT_CAUSE_INVALIDAUTHENTICATION
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_INVALIDAUTHENTICATION, PhotonMatchmaking::DisconnectCause::InvalidAuthentication)
    /**
     * DISCONNECT_CAUSE_MAXCCUREACHED
     * @field DISCONNECT_CAUSE_MAXCCUREACHED
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_MAXCCUREACHED, PhotonMatchmaking::DisconnectCause::MaxCCUReached)
    /**
     * DISCONNECT_CAUSE_INVALIDREGION
     * @field DISCONNECT_CAUSE_INVALIDREGION
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_INVALIDREGION, PhotonMatchmaking::DisconnectCause::InvalidRegion)
    /**
     * DISCONNECT_CAUSE_OPERATIONNOTALLOWEDINCURRENTSTATE
     * @field DISCONNECT_CAUSE_OPERATIONNOTALLOWEDINCURRENTSTATE
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_OPERATIONNOTALLOWEDINCURRENTSTATE, PhotonMatchmaking::DisconnectCause::OperationNotAllowedInCurrentState)
    /**
     * DISCONNECT_CAUSE_CUSTOMAUTHENTICATIONFAILED
     * @field DISCONNECT_CAUSE_CUSTOMAUTHENTICATIONFAILED
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_CUSTOMAUTHENTICATIONFAILED, PhotonMatchmaking::DisconnectCause::CustomAuthenticationFailed)
    /**
     * DISCONNECT_CAUSE_CLIENTVERSIONTOOOLD
     * @field DISCONNECT_CAUSE_CLIENTVERSIONTOOOLD
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_CLIENTVERSIONTOOOLD, PhotonMatchmaking::DisconnectCause::ClientVersionTooOld)
    /**
     * DISCONNECT_CAUSE_CLIENTVERSIONINVALID
     * @field DISCONNECT_CAUSE_CLIENTVERSIONINVALID
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_CLIENTVERSIONINVALID, PhotonMatchmaking::DisconnectCause::ClientVersionInvalid)
    /**
     * DISCONNECT_CAUSE_DASHBOARDVERSIONINVALID
     * @field DISCONNECT_CAUSE_DASHBOARDVERSIONINVALID
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_DASHBOARDVERSIONINVALID, PhotonMatchmaking::DisconnectCause::DashboardVersionInvalid)
    /**
     * DISCONNECT_CAUSE_AUTHENTICATIONTICKETEXPIRED
     * @field DISCONNECT_CAUSE_AUTHENTICATIONTICKETEXPIRED
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_AUTHENTICATIONTICKETEXPIRED, PhotonMatchmaking::DisconnectCause::AuthenticationTicketExpired)
    /**
     * DISCONNECT_CAUSE_DISCONNECTBYOPERATIONLIMIT
     * @field DISCONNECT_CAUSE_DISCONNECTBYOPERATIONLIMIT
     */
    SETCONSTANT_NUMBER(DISCONNECT_CAUSE_DISCONNECTBYOPERATIONLIMIT, PhotonMatchmaking::DisconnectCause::DisconnectByOperationLimit)
    #undef SETCONSTANT_NUMBER


    #define SETCONSTANT_HASH(name, val) \
    dmScript::PushHash(L, val); \
    lua_setfield(L, -2, #name);

    /**
     * EVENT_OBJECT_READY
     * @field EVENT_OBJECT_READY
     */
    SETCONSTANT_HASH(EVENT_OBJECT_READY, g_FusionEventOnObjectReady)
    /**
     * EVENT_SUB_OBJECT_CREATED
     * @field EVENT_SUB_OBJECT_CREATED
     */
    SETCONSTANT_HASH(EVENT_SUB_OBJECT_CREATED, g_FusionEventOnSubObjectCreated)
    /**
     * EVENT_OBJECT_DESTROYED
     * @field EVENT_OBJECT_DESTROYED
     */
    SETCONSTANT_HASH(EVENT_OBJECT_DESTROYED, g_FusionEventOnObjectDestroyed)
    /**
     * EVENT_SUB_OBJECT_DESTROYED
     * @field EVENT_SUB_OBJECT_DESTROYED
     */
    SETCONSTANT_HASH(EVENT_SUB_OBJECT_DESTROYED, g_FusionEventOnSubObjectDestroyed)
    /**
     * EVENT_OBJECT_FORCE_ALIVE
     * @field EVENT_OBJECT_FORCE_ALIVE
     */
    SETCONSTANT_HASH(EVENT_OBJECT_FORCE_ALIVE, g_FusionEventOnObjectForceAlive)
    /**
     * EVENT_SUB_OBJECT_FORCE_ALIVE
     * @field EVENT_SUB_OBJECT_FORCE_ALIVE
     */
    SETCONSTANT_HASH(EVENT_SUB_OBJECT_FORCE_ALIVE, g_FusionEventOnSubObjectForceAlive)
    /**
     * EVENT_OBJECT_OWNER_CHANGED
     * @field EVENT_OBJECT_OWNER_CHANGED
     */
    SETCONSTANT_HASH(EVENT_OBJECT_OWNER_CHANGED, g_FusionEventOnObjectOwnerChanged)
    /**
     * EVENT_PREDICTION_OVERRIDE
     * @field EVENT_PREDICTION_OVERRIDE
     */
    SETCONSTANT_HASH(EVENT_PREDICTION_OVERRIDE, g_FusionEventOnPredictionOverride)
    /**
     * EVENT_LOBBY_STATS
     * @field EVENT_LOBBY_STATS
     */
    SETCONSTANT_HASH(EVENT_LOBBY_STATS, g_FusionEventOnLobbyStats)
    /**
     * EVENT_ROOM_JOINED
     * @field EVENT_ROOM_JOINED
     */
    SETCONSTANT_HASH(EVENT_ROOM_JOINED, g_FusionEventOnRoomJoined)
    /**
     * EVENT_ROOM_LEFT
     * @field EVENT_ROOM_LEFT
     */
    SETCONSTANT_HASH(EVENT_ROOM_LEFT, g_FusionEventOnRoomLeft)
    /**
     * EVENT_RPC
     * @field EVENT_RPC
     */
    SETCONSTANT_HASH(EVENT_RPC, g_FusionEventOnRpc)
    /**
     * EVENT_MAP_CHANGE
     * @field EVENT_MAP_CHANGE
     */
    SETCONSTANT_HASH(EVENT_MAP_CHANGE, g_FusionEventOnMapChange)
    /**
     * EVENT_DESTROYED_MAP_ACTOR
     * @field EVENT_DESTROYED_MAP_ACTOR
     */
    SETCONSTANT_HASH(EVENT_DESTROYED_MAP_ACTOR, g_FusionEventOnDestroyedMapActor)
    /**
     * EVENT_INTEREST_ENTER
     * @field EVENT_INTEREST_ENTER
     */
    SETCONSTANT_HASH(EVENT_INTEREST_ENTER, g_FusionEventOnInterestEnter)
    /**
     * EVENT_INTEREST_EXIT
     * @field EVENT_INTEREST_EXIT
     */
    SETCONSTANT_HASH(EVENT_INTEREST_EXIT, g_FusionEventOnInterestExit)
    /**
     * EVENT_FORCED_DISCONNECT
     * @field EVENT_FORCED_DISCONNECT
     */
    SETCONSTANT_HASH(EVENT_FORCED_DISCONNECT, g_FusionEventOnForcedDisconnect)
    /**
     * EVENT_FUSION_START
     * @field EVENT_FUSION_START
     */
    SETCONSTANT_HASH(EVENT_FUSION_START, g_FusionEventOnFusionStart)
    /**
     * EVENT_CONNECTED
     * @field EVENT_CONNECTED
     */
    SETCONSTANT_HASH(EVENT_CONNECTED, g_FusionEventOnConnected)
    /**
     * EVENT_DISCONNECTED
     * @field EVENT_DISCONNECTED
     */
    SETCONSTANT_HASH(EVENT_DISCONNECTED, g_FusionEventOnDisconnected)
    #undef SETCONSTANT_HASH


    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

dmExtension::Result AppInitializeFusion(dmExtension::AppParams* params)
{
    dmLogInfo("AppInitializeFusion");
    return dmExtension::RESULT_OK;
}

dmExtension::Result InitializeFusion(dmExtension::Params* params)
{
    dmLogInfo("InitializeFusion");
    g_Ctx = new FusionCtx();
    g_Ctx->m_Timestamp = dmTime::GetMonotonicTime();
    g_Ctx->m_ResourceFactory = params->m_ResourceFactory;
    g_Ctx->m_ConfigFile = params->m_ConfigFile;
    LuaInit(params->m_L);
    dmLogInfo("Registered %s Extension", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

dmExtension::Result AppFinalizeFusion(dmExtension::AppParams* params)
{
    dmLogInfo("AppFinalizeFusion");
    return dmExtension::RESULT_OK;
}

dmExtension::Result FinalizeFusion(dmExtension::Params* params)
{
    dmLogInfo("FinalizeFusion");
    if (g_Ctx->m_FusionClient)
    {
        g_Ctx->m_FusionClient->Shutdown();
        delete g_Ctx->m_FusionClient;
        g_Ctx->m_FusionClient = 0;
    }

    dmHashTable<dmhash_t, FusionObject*>::Iterator iter = g_Ctx->m_FusionObjects.GetIterator();
    while(iter.Next())
    {
        FusionObject* object = iter.GetValue();
        free(object);
    }

    if (g_FusionEventCallback)
    {
        dmScript::DestroyCallback(g_FusionEventCallback);
        g_FusionEventCallback = 0;
    }

    delete g_Ctx;
    return dmExtension::RESULT_OK;
}

dmExtension::Result UpdateFusion(dmExtension::Params* params)
{
    uint64_t t = dmTime::GetMonotonicTime();
    uint64_t delta = t - g_Ctx->m_Timestamp;
    g_Ctx->m_Timestamp = t;
    double dt = (double)delta / 1000000.0;

    if (g_Ctx->m_FusionClient)
    {
        g_Ctx->m_FusionClient->GetRealtimeClient().Service(true);

        const PhotonMatchmaking::ConnectionState state = g_Ctx->m_FusionClient->GetRealtimeClient().GetState();
        if (state == PhotonMatchmaking::ConnectionState::Connected)
        {
            if (g_Ctx->m_ConnectionState == PhotonMatchmaking::ConnectionState::Connecting)
            {
                CallListener(g_FusionEventOnConnected);
            }
        }
        else if (state == PhotonMatchmaking::ConnectionState::Disconnected)
        {
            if (g_Ctx->m_ConnectionState == PhotonMatchmaking::ConnectionState::Disconnecting)
            {
                CallListener(g_FusionEventOnDisconnected);
            }
        }

        if (g_Ctx->m_FusionClient->IsRunning())
        {
            if (g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom())
            {
                Fusion_TickBeforeFrameEnd();
                g_Ctx->m_FusionClient->UpdateFrameEnd();
                g_Ctx->m_FusionClient->UpdateFrameBegin(dt);
                Fusion_TickAfterFrameBegin(dt);
            }
        }

        g_Ctx->m_ConnectionState = state;
    }

    return dmExtension::RESULT_OK;
}

void OnEventFusion(dmExtension::Params* params, const dmExtension::Event* event)
{
    dmLogInfo("OnEventFusion");
}


#else

static dmExtension::Result AppInitializeFusion(dmExtension::AppParams* params)
{
    dmLogWarning("Registered %s (null) Extension", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

static dmExtension::Result InitializeFusion(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result AppFinalizeFusion(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result FinalizeFusion(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result UpdateFusion(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

static void OnEventFusion(dmExtension::Params* params, const dmExtension::Event* event)
{
}

#endif

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, AppInitializeFusion, AppFinalizeFusion, InitializeFusion, UpdateFusion, OnEventFusion, FinalizeFusion)
