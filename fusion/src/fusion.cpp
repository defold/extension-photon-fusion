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
#include "LogUtils.h"
#include "LogOutput.h"


class FusionDefoldLogOutput : public SharedMode::Logging::LogOutput
{
public:
    FusionDefoldLogOutput() {}
    ~FusionDefoldLogOutput() {};
    void LogTrace(const SharedMode::CharType* message)
    {
        dmLogDebug("%s", (char*)message);
    }

    void LogDebug(const SharedMode::CharType* message)
    {
        dmLogDebug("%s", (char*)message);
    }

    void LogInfo(const SharedMode::CharType* message)
    {
        dmLogInfo("%s", (char*)message);
    }

    void LogWarning(const SharedMode::CharType* message)
    {
        dmLogWarning("%s", (char*)message);
    }

    void LogError(const SharedMode::CharType* message)
    {
        dmLogError("%s", (char*)message);
    }
};


struct FusionObject
{
    SharedMode::ObjectRoot*  m_SharedObject;
    dmVMath::Point3          m_Position;
    dmVMath::Quat            m_Rotation;
    dmVMath::Vector3         m_Scale;
};

struct FusionCtx
{
    dmResource::HFactory                           m_ResourceFactory;
    dmConfigFile::HConfig                          m_ConfigFile;
    dmGameObject::HCollection                      m_Collection;
    uint64_t                                       m_Timestamp;
    SharedMode::Client*                            m_FusionClient;
    dmHashTable<dmhash_t, FusionObject*>           m_FusionObjects;
    FusionDefoldLogOutput*                         m_FusionDefoldLogOutput;
    dmScript::LuaCallbackInfo*                     m_EventCallback;

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

/******
 * Helpers
 *************************/

static int32_t CompressFloat(float f)
{
    int32_t result;
    memcpy(&result, &f, sizeof(float));
    return result;
    // return *(uint32_t*)&f;
}
static float DecompressFloat(int32_t f)
{
    float result;
    memcpy(&result, &f, sizeof(float));
    return result;
    // return *(float*)&f;
}

static size_t PushFloat(SharedMode::Word* words, float f)
{
    words[0] = CompressFloat(f);
    return 1;
}
static size_t PopFloat(SharedMode::Word* words, float* out)
{
    *out = DecompressFloat(words[0]);
    return 1;
}
static size_t PushPoint3(SharedMode::Word* words, dmVMath::Point3& p3)
{
    words[0] = CompressFloat(p3.getX());
    words[1] = CompressFloat(p3.getY());
    words[2] = CompressFloat(p3.getZ());
    return 3;
}
static size_t PopPoint3(SharedMode::Word* words, dmVMath::Point3* out)
{
    out->setX(DecompressFloat(words[0]));
    out->setY(DecompressFloat(words[1]));
    out->setZ(DecompressFloat(words[2]));
    return 3;
}
static size_t PushVector3(SharedMode::Word* words, dmVMath::Vector3& v3)
{
    words[0] = CompressFloat(v3.getX());
    words[1] = CompressFloat(v3.getY());
    words[2] = CompressFloat(v3.getZ());
    return 3;
}
static size_t PopVector3(SharedMode::Word* words, dmVMath::Vector3* out)
{
    out->setX(DecompressFloat(words[0]));
    out->setY(DecompressFloat(words[1]));
    out->setZ(DecompressFloat(words[2]));
    return 3;
}
static size_t PushQuat(SharedMode::Word* words, dmVMath::Quat& q)
{
    words[0] = CompressFloat(q.getX());
    words[1] = CompressFloat(q.getY());
    words[2] = CompressFloat(q.getZ());
    words[3] = CompressFloat(q.getW());
    return 4;
}
static size_t PopQuat(SharedMode::Word* words, dmVMath::Quat* out)
{
    out->setX(DecompressFloat(words[0]));
    out->setY(DecompressFloat(words[1]));
    out->setZ(DecompressFloat(words[2]));
    out->setW(DecompressFloat(words[3]));
    return 4;
}
static size_t PushUint32(SharedMode::Word* words, uint32_t i)
{
    words[0] = i;
    return 1;
}
static size_t PopUint32(SharedMode::Word* words, uint32_t* out)
{
    *out = (uint32_t)words[0];
    return 1;
}
static size_t PushHash(SharedMode::Word* words, dmhash_t h)
{
    int32_t high = (int32_t)((h >> 32) & 0xFFFFFFFFu);
    int32_t low = (int32_t)(h & 0xFFFFFFFFu);
    words[0] = high;
    words[1] = low;
    return 2;
}
static size_t PopHash(SharedMode::Word* words, dmhash_t* out)
{
    uint64_t high = (uint32_t)(words[0]);
    uint64_t low  = (uint32_t)(words[1]);
    dmhash_t h = (dmhash_t)((high << 32) | low);
    *out = h;
    return 2;
}

static size_t PushHash(uint8_t* a, dmhash_t h)
{
    a[0] = (h & 0xFF00000000000000) >> 56;
    a[1] = (h & 0x00FF000000000000) >> 48;
    a[2] = (h & 0x0000FF0000000000) >> 40;
    a[3] = (h & 0x000000FF00000000) >> 32;
    a[4] = (h & 0x00000000FF000000) >> 24;
    a[5] = (h & 0x0000000000FF0000) >> 16;
    a[6] = (h & 0x000000000000FF00) >> 8;
    a[7] = (h & 0x00000000000000FF) >> 0;
    return 8;
}
static size_t PopHash(uint8_t* a, dmhash_t* out)
{
    *out = (dmhash_t)(
        ((uint64_t)a[0]) << 56 |
        ((uint64_t)a[1]) << 48 |
        ((uint64_t)a[2]) << 40 |
        ((uint64_t)a[3]) << 32 |
        ((uint64_t)a[4]) << 24 |
        ((uint64_t)a[5]) << 16 |
        ((uint64_t)a[6]) << 8 |
        ((uint64_t)a[7]) << 0
        );
    return 8;
}
static size_t PushUint32(uint8_t* a, uint32_t i)
{
    a[0] = (i & 0xFF000000) >> 24;
    a[1] = (i & 0x00FF0000) >> 16;
    a[2] = (i & 0x0000FF00) >> 8;
    a[3] = (i & 0x000000FF) >> 0;
    return 4;
}
static size_t PopUint32(uint8_t* a, uint32_t* out)
{
    *out = (uint32_t)(
        ((uint64_t)a[0]) << 24 |
        ((uint64_t)a[1]) << 16 |
        ((uint64_t)a[2]) << 8 |
        ((uint64_t)a[3]) << 0
        );
    return 4;
}
static size_t PushUint16(uint8_t* a, uint16_t i)
{
    a[0] = (i & 0x0000FF00) >> 8;
    a[1] = (i & 0x000000FF) >> 0;
    return 2;
}
static size_t PopUint16(uint8_t* a, uint16_t* out)
{
    *out = (uint16_t)(
        ((uint64_t)a[0]) << 8 |
        ((uint64_t)a[1]) << 0
        );
    return 2;
}
static size_t PushUint8(uint8_t* a, uint8_t i)
{
    a[0] = i;
    return 1;
}
static size_t PopUint8(uint8_t* a, uint8_t* out)
{
    *out = (uint8_t)(a[0]);
    return 1;
}

static SharedMode::ObjectRoot* GetObject(dmhash_t id)
{
    FusionObject** object = g_Ctx->m_FusionObjects.Get(id);
    if (object)
    {
        return (*object)->m_SharedObject;
    }

    return 0;
}


/******
 * Fusion callbacks
 *************************/

static lua_State* SetupListener()
{
    if (!dmScript::IsCallbackValid(g_Ctx->m_EventCallback))
    {
        return 0;
    }

    lua_State* L = dmScript::GetCallbackLuaContext(g_Ctx->m_EventCallback);
    if (!dmScript::SetupCallback(g_Ctx->m_EventCallback))
    {
        dmLogError("Failed to setup callback");
        return 0;
    }
    return L;
}

static int CallListener(lua_State* L, int nargs, int nresults)
{
    int result = dmScript::PCall(L, nargs, nresults);
    dmScript::TeardownCallback(g_Ctx->m_EventCallback);
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

static FusionObject* CreateFusionObject(dmhash_t id, SharedMode::ObjectRoot* object)
{
    if (g_Ctx->m_FusionObjects.Full())
    {
        g_Ctx->m_FusionObjects.OffsetCapacity(100);
    }
    FusionObject* fusion_object = (FusionObject*)malloc(sizeof(FusionObject));
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
    g_Ctx->m_FusionObjects.Put(id, fusion_object);
    return fusion_object;
}


static void Fusion_OnObjectCreated(SharedMode::ObjectRoot* object)
{
    dmLogInfo("Fusion_OnObjectCreated owner: %d local player: %d", object->Owner, g_Ctx->m_FusionClient->LocalPlayerId());

    uint64_t socket;
    uint64_t path;
    uint64_t fragment;

    uint8_t* header = object->Header.Ptr;
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
        return;
    }
    dmGameSystem::HFactoryWorld world;
    dmGameSystem::HFactoryComponent factory;
    uint32_t component_type_index;
    dmGameObject::Result r = dmGameObject::GetComponent(factory_go, fragment, &component_type_index, (dmGameObject::HComponent*)&factory, (dmGameObject::HComponentWorld*)&world);
    if (dmGameObject::RESULT_OK != r)
    {
        dmLogError("Unable to get component %s", dmHashReverseSafe64(fragment));
        return;
    }


    //
    // spawn gameobject
    dmVMath::Point3 position = dmVMath::Point3(3.0, 0, 0);
    dmVMath::Quat rotation = dmVMath::Quat::identity();
    dmVMath::Vector3 scale = dmVMath::Vector3(1.0, 1.0, 1.0);
    dmhash_t id = dmGameObject::CreateInstanceId();
    dmGameObject::HInstance instance;

    dmGameObject::PropertyContainerBuilderParams params;
    params.m_BoolCount = 1;
    dmGameObject::HPropertyContainerBuilder builder = dmGameObject::PropertyContainerCreateBuilder(params);
    dmGameObject::PropertyContainerPushBool(builder, dmHashString64("remote_object"), true);
    dmGameObject::HPropertyContainer properties = dmGameObject::PropertyContainerCreate(builder);

    r = dmGameSystem::CompFactorySpawn(
            world, factory, g_Ctx->m_Collection,
            id, 
            position, rotation, scale,
            properties, &instance);

    dmGameObject::PropertyContainerDestroy(properties);

    if (dmGameObject::RESULT_OK != r)
    {
        dmLogError("Unable to spawn collection");
        return;
    }

    CreateFusionObject(id, object);
    CallListener(dmHashString64("OnObjectCreated"), id);
}
static void Fusion_OnSubObjectCreated(const SharedMode::ObjectChild* child)
{
    dmLogInfo("Fusion_OnSubObjectCreated");
    CallListener(dmHashString64("OnSubObjectCreated"));
}

static void Fusion_OnObjectDestroyed(const SharedMode::ObjectRoot* object, const SharedMode::DestroyModes mode)
{
    dmLogInfo("Fusion_OnObjectDestroyed owner: %d local player: %d", object->Owner, g_Ctx->m_FusionClient->LocalPlayerId());

    if (mode == SharedMode::DestroyModes::Local)
    {
        dmLogInfo("DestroyModes Local");
        return;
    }
    // if (mode == SharedMode::DestroyModes::Remote)
    // {
    //     dmLogInfo("DestroyModes Remote");
    // }
    // else if (mode == SharedMode::DestroyModes::SceneChange)
    // {
    //     dmLogInfo("DestroyModes SceneChange");
    // }
    // else if (mode == SharedMode::DestroyModes::Shutdown)
    // {
    //     dmLogInfo("DestroyModes Shutdown");
    // }

    dmHashTable<dmhash_t, FusionObject*>::Iterator iter = g_Ctx->m_FusionObjects.GetIterator();
    while(iter.Next())
    {
        dmhash_t id = iter.GetKey();
        FusionObject* fusion_object = iter.GetValue();
        dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_Collection, id);
        if (object == fusion_object->m_SharedObject)
        {
            dmLogInfo("Found object to destroy %s", dmHashReverseSafe64(id));
            dmGameObject::Delete(g_Ctx->m_Collection, instance, true);
            g_Ctx->m_FusionObjects.Erase(id);
            free(fusion_object);

            CallListener(dmHashString64("OnObjectDestroyed"), id);
            return;
        }
    }
    dmLogError("Unable to find object to destroy");
}
static void Fusion_OnObjectOwnerChanged(const SharedMode::ObjectRoot* obj)
{
    dmLogInfo("Fusion_OnObjectOwnerChanged owner: %d local player: %d", obj->Owner, g_Ctx->m_FusionClient->LocalPlayerId());
    CallListener(dmHashString64("OnObjectOwnerChanged"));
}
static void Fusion_OnObjectPredictionOverride(const SharedMode::ObjectRoot* obj)
{
    dmLogInfo("Fusion_OnObjectPredictionOverride");
    CallListener(dmHashString64("OnObjectPredictionOverride"));
}
static void Fusion_OnRoomJoin()
{
    dmLogInfo("Fusion_OnRoomJoin local player: %d", g_Ctx->m_FusionClient->LocalPlayerId());
    const char* name = g_Ctx->m_FusionClient->Photon().LoadBalancingClient().getCurrentlyJoinedRoom().getName().UTF8Representation().cstr();
    dmLogInfo("%s", name);
    CallListener(dmHashString64("OnRoomJoin"), name);
}
static void Fusion_OnRoomLeave()
{
    dmLogInfo("Fusion_OnRoomLeave");
    CallListener(dmHashString64("OnRoomLeave"));
}
static void Fusion_OnRpc(const SharedMode::Rpc& rpc)
{
    dmLogInfo("Fusion_OnRpc");
    lua_State* L = SetupListener();
    if(L)
    {
        dmScript::PushHash(L, dmHashString64("OnRpc"));

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
        dmScript::PushHash(L, dmhash_t(rpc.DescriptorTypeHash));
        lua_setfield(L, -2, "descriptor_type");
        dmScript::PushHash(L, dmhash_t(rpc.EventHash));
        lua_setfield(L, -2, "event");
        lua_pushlstring(L, (char*)rpc.Bytes.Ptr, rpc.Bytes.Length);
        lua_setfield(L, -2, "bytes");

        CallListener(L, 3, 0);
    }
}
static void Fusion_OnSceneChange(uint32_t index, uint32_t sequence, SharedMode::Data data)
{
    dmLogInfo("Fusion_OnSceneChange");

    lua_State* L = SetupListener();
    if(L)
    {
        dmScript::PushHash(L, dmHashString64("OnSceneChange"));

        lua_newtable(L);
        lua_pushinteger(L, index);
        lua_setfield(L, -2, "index");
        lua_pushinteger(L, sequence);
        lua_setfield(L, -2, "sequence");
        lua_pushlstring(L, (char*)data.Ptr, data.Length);
        lua_setfield(L, -2, "data");

        CallListener(L, 3, 0);
    }
}



/******
 * Fusion update lifecycle
 *************************/

static void SerializeObject(dmhash_t id, FusionObject* fusion_object)
{
    SharedMode::ObjectRoot* object = fusion_object->m_SharedObject;

    dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_Collection, id);

    SharedMode::Word *words = object->Words.Ptr;
    dmVMath::Point3 pos = dmGameObject::GetPosition(instance);
    dmVMath::Quat rot = dmGameObject::GetRotation(instance);
    dmVMath::Vector3 scale = dmGameObject::GetScale(instance);

    if (object->Flags.InterestMode == SharedMode::ObjectInterestModes::Area)
    {
        SharedMode::AOILocation location = g_Ctx->m_FusionClient->CalculateAreaOfInterestLocation(pos.getX(), pos.getY(), pos.getZ());
        g_Ctx->m_FusionClient->SetAreaOfInterestLocation(object, location);
    }

    size_t word_offset = 0;
    word_offset += PushPoint3(words + word_offset, pos);
    word_offset += PushQuat(words + word_offset, rot);
    word_offset += PushVector3(words + word_offset, scale);

    uint64_t socket;
    uint64_t path;
    uint64_t fragment;

    uint8_t* header = object->Header.Ptr;
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
static void DeserializeObject(dmhash_t id, FusionObject* fusion_object)
{

    SharedMode::ObjectRoot* object = fusion_object->m_SharedObject;
    // if (!g_Ctx->m_FusionClient->HasBeenUpdatedByPlugin(object))
    // {
    //     LerpObjectTransform(id, fusion_object);
    //     return;
    // }

    SharedMode::Word *words = object->Words.Ptr;
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

    uint8_t* header = object->Header.Ptr;
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
    }
}

// send object properties
void Fusion_TickBeforeFrameEnd()
{
    // dmLogInfo("TickBeforeFrameEnd");
    dmHashTable<dmhash_t, FusionObject*>::Iterator iter = g_Ctx->m_FusionObjects.GetIterator();
    while(iter.Next())
    {
        dmhash_t id = iter.GetKey();
        FusionObject* object = iter.GetValue();
        if (g_Ctx->m_FusionClient->IsOwner(object->m_SharedObject))
        {
            SerializeObject(id, object);
        }
    }
}

// updated object properties with received data
void Fusion_TickAfterFrameBegin(double dt)
{
    // dmLogInfo("Fusion_TickAfterFrameBegin");
    dmHashTable<dmhash_t, FusionObject*>::Iterator iter = g_Ctx->m_FusionObjects.GetIterator();
    while(iter.Next())
    {
        dmhash_t id = iter.GetKey();
        FusionObject* object = iter.GetValue();
        if (!g_Ctx->m_FusionClient->IsOwner(object->m_SharedObject))
        {
            DeserializeObject(id, object);
        }
    }
}




/******
 * Fusion Lua API functions
 *************************/

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


    g_Ctx->m_FusionObjects.SetCapacity(100, 100);


    if (g_Ctx->m_FusionClient)
    {
        delete g_Ctx->m_FusionClient;
    }
    g_Ctx->m_FusionClient = new SharedMode::Client((const SharedMode::CharType*)appId, (const SharedMode::CharType*)appVersion);
    g_Ctx->m_FusionClient->OnObjectCreated = Fusion_OnObjectCreated;
    g_Ctx->m_FusionClient->OnSubObjectCreated = Fusion_OnSubObjectCreated;
    g_Ctx->m_FusionClient->OnObjectDestroyed = Fusion_OnObjectDestroyed;
    g_Ctx->m_FusionClient->OnRoomJoin = Fusion_OnRoomJoin;
    g_Ctx->m_FusionClient->OnRoomLeave = Fusion_OnRoomLeave;
    g_Ctx->m_FusionClient->OnRpc = Fusion_OnRpc;
    g_Ctx->m_FusionClient->OnSceneChange = Fusion_OnSceneChange;
    g_Ctx->m_FusionClient->OnObjectOwnerChanged = Fusion_OnObjectOwnerChanged;
    g_Ctx->m_FusionClient->OnObjectPredictionOverride = Fusion_OnObjectPredictionOverride;

    g_Ctx->m_FusionDefoldLogOutput = new FusionDefoldLogOutput();

    dmGameObject::HInstance caller_instance = dmScript::CheckGOInstance(L);
    g_Ctx->m_Collection = dmGameObject::GetCollection(caller_instance);
    g_Ctx->m_Scriptc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("scriptc"));
    g_Ctx->m_Spritec = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("spritec"));
    g_Ctx->m_Modelc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("modelc"));
    g_Ctx->m_Collisionobjectc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("collisionobjectc"));
    g_Ctx->m_Particlefxc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("particlefxc"));
    g_Ctx->m_Labelc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_Collection, dmHashString64("labelc"));

    return 0;
}

/** Connect Fusion
 * @name connect
 * @string region
 * @string user
 * @string server
 */
static int Connect(lua_State* L)
{
    dmLogInfo("Connect");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (g_Ctx->m_FusionClient->Photon().IsConnected())
    {
        luaL_error(L, "Fusion is already disconnected");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    const char* region = 0x0;
    if (lua_isstring(L, 1))
    {
        region = luaL_checkstring(L, 1);
    }

    const char* userid = 0x0;
    if (lua_isstring(L, 2))
    {
        userid = luaL_checkstring(L, 2);
    }

    const char* server = 0x0;
    if (lua_isstring(L, 3))
    {
        server = luaL_checkstring(L, 3);
    }

    dmLogInfo("Calling ConnectCloud with region '%s' user '%s' and server '%s'", region, userid, server);
    g_Ctx->m_FusionClient->ConnectCloud((const SharedMode::CharType*)region, (const SharedMode::CharType*)userid, (const SharedMode::CharType*)server);
    return 0;
}


/** Join or create random room
 * @name join_or_create_room_random
 * @string room_name
 */
static int JoinOrCreateRoomRandom(lua_State* L)
{
    dmLogInfo("JoinRoomRandom");

    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    if (!g_Ctx->m_FusionClient->Photon().IsConnected())
    {
        luaL_error(L, "Fusion is not connected");
        return 0;
    }
    if (g_Ctx->m_FusionClient->Photon().IsJoiningOrInRoom())
    {
        luaL_error(L, "Fusion is already joining or in room");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);
    const char* name = luaL_checkstring(L, 1);
    ExitGames::LoadBalancing::RoomOptions options = ExitGames::LoadBalancing::RoomOptions();
    options.setIsVisible(true);
    options.setIsOpen(true);
    options.setMaxPlayers(4);
    dmLogInfo("JoinRoomRandom name = %s", name);
    g_Ctx->m_FusionClient->Photon().JoinOrCreateRoomRandom((const SharedMode::CharType*)name, options);
    return 0;
}


/** Check if Fusion connected
 * @name is_connected
 * @treturn boolean connected
 */
static int IsConnected(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);
    bool connected = g_Ctx->m_FusionClient->Photon().IsConnected();
    lua_pushboolean(L, connected);
    return 1;
}


/** Check if Fusion is running
 * @name is_running
 * @treturn boolean running
 */
static int IsRunning(lua_State* L)
{
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


/** Check if Fusion is in a room
 * @name is_in_room
 * @treturn boolean in_room
 */
static int IsInRoom(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);
    bool in_room = g_Ctx->m_FusionClient->Photon().IsInRoom();
    lua_pushboolean(L, in_room);
    return 1;
}


/** Check if Fusion is joining or in a room
 * @name is_joining_or_in_room
 * @treturn boolean running
 */
static int IsJoiningOrInRoom(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);
    bool joining_or_in_room = g_Ctx->m_FusionClient->Photon().IsJoiningOrInRoom();
    lua_pushboolean(L, joining_or_in_room);
    return 1;
}


/** Enable/disable debugging
 * @name enable_debugging
 * @boolean enable
 */
static int EnableDebug(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);
    bool enable = lua_toboolean(L, 1);
    if (enable)
    {
        g_Ctx->m_FusionClient->Photon().SetLogLevel(ExitGames::Common::DebugLevel::ALL);
        SharedMode::Logging::AddLogOutput(g_Ctx->m_FusionDefoldLogOutput);
        SharedMode::Logging::SetLogLevelsFromBitmask(0xFF);
    }
    else
    {
        g_Ctx->m_FusionClient->Photon().SetLogLevel(ExitGames::Common::DebugLevel::OFF);
        SharedMode::Logging::RemoveLogOutput(g_Ctx->m_FusionDefoldLogOutput);
        SharedMode::Logging::SetLogLevelsFromBitmask(0x0);
    }
    return 0;
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
        else
        {

        }

        component_index++;
    }

    // write component count
    headerLength += PushUint16(header + componentCountOffset, component_index);
    return true;
}


static SharedMode::ObjectFlags CheckObjectFlags(lua_State* L, int index)
{
    SharedMode::ObjectSettingsFlags objectSettingsFlags = SharedMode::ObjectSettingsFlags::None;
    SharedMode::ObjectOwnerModes objectOwnerMode = SharedMode::ObjectOwnerModes::Dynamic;
    SharedMode::ObjectInterestModes objectInterestMode = SharedMode::ObjectInterestModes::All;

    if (lua_type(L, index) == LUA_TTABLE)
    {
        lua_pushvalue(L, index);
        lua_pushnil(L);
        while (lua_next(L, -2) != 0)
        {
            const char* key = luaL_checkstring(L, -2);
            if (dmStrCaseCmp(key, "settings") == 0)
            {
                objectSettingsFlags = (SharedMode::ObjectSettingsFlags)luaL_checknumber(L, -1);
            }
            else if (dmStrCaseCmp(key, "owner_mode") == 0)
            {
                objectOwnerMode = (SharedMode::ObjectOwnerModes)luaL_checknumber(L, -1);
            }
            else if (dmStrCaseCmp(key, "interest_mode") == 0)
            {
                objectInterestMode = (SharedMode::ObjectInterestModes)luaL_checknumber(L, -1);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }

    return SharedMode::ObjectFlags(objectSettingsFlags, objectOwnerMode, objectInterestMode);
}

/** Register a scene object
 * @name register
 * @string id
 * @number scene
 */
static int RegisterSceneObject(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }
    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    uint32_t scene = (uint32_t)luaL_checknumber(L, 2);
    dmMessage::URL* factory_url = dmScript::CheckURL(L, 3);
    SharedMode::ObjectFlags objectFlags = CheckObjectFlags(L, 4);

    uint8_t header[1000];
    size_t headerLength;
    size_t wordsCount;
    bool ok = BuildObjectHeader(id, factory_url, header, headerLength, wordsCount);
    if (!ok)
    {
        return 0;
    }

    SharedMode::TypeRef type;
    type.Hash = 0;
    type.WordCount = wordsCount + SharedMode::Object::ExtraTailWords;


    bool alreadyPopulated;
    SharedMode::ObjectRoot* object = g_Ctx->m_FusionClient->CreateSceneObject(alreadyPopulated, wordsCount, type, (SharedMode::CharType*)header, headerLength, scene, factory_url->m_Path, objectFlags);
    FusionObject* fusion_object = CreateFusionObject(id, object);
    if (!alreadyPopulated)
    {
        SerializeObject(id, fusion_object);
    }
    else
    {
        DeserializeObject(id, fusion_object);
    }

    return 0;
}

/** Register an object
 * @name register
 * @string id
 * @number scene
 */
static int RegisterObject(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    uint32_t scene = (uint32_t)luaL_checknumber(L, 2);
    dmMessage::URL* factory_url = dmScript::CheckURL(L, 3);
    SharedMode::ObjectFlags objectFlags = CheckObjectFlags(L, 4);

    uint8_t header[1000];
    size_t headerLength;
    size_t wordsCount;
    bool ok = BuildObjectHeader(id, factory_url, header, headerLength, wordsCount);
    if (!ok)
    {
        return 0;
    }

    SharedMode::TypeRef type;
    type.Hash = 0;
    type.WordCount = wordsCount + SharedMode::Object::ExtraTailWords;

    SharedMode::ObjectRoot* object = g_Ctx->m_FusionClient->CreateObject(wordsCount, type, (SharedMode::CharType*)header, headerLength, scene, objectFlags);
    CreateFusionObject(id, object);

    return 0;
}

/** Destroy a local object
 * @name destroy
 * @string id
 */
static int DestroyObject(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    dmLogInfo("DestroyObject id: %s", dmHashReverseSafe64(id));
    SharedMode::ObjectRoot* object = GetObject(id);
    if (object)
    {
        g_Ctx->m_FusionObjects.Erase(id);
        bool ok = g_Ctx->m_FusionClient->DestroyObjectLocal(object, true);
    }
    return 0;
}

/** Change scene
 * @name scene_change
 * @number index
 * @number sequence
 * @string data
 */
static int SceneChange(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    uint32_t index = (uint32_t)luaL_checknumber(L, 1);
    uint32_t sequence = (uint32_t)luaL_checknumber(L, 2);
    const char* data = luaL_checkstring(L, 3);

    // bool ok = g_Ctx->m_FusionClient->SceneChange(object, true);

    return 0;
}

/** Send RPC
 * @name send_rpc
 * @number target_player 0 = all, specific PlayerId = targeted
 * @hash descriptor
 * @hash event
 * @string data
 * @treturn boolean ok
 */
static int SendRpc(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    SharedMode::PlayerId target_player = (SharedMode::PlayerId)luaL_checknumber(L, 1);
    SharedMode::ObjectId target_object;
    dmhash_t descriptor_type = dmScript::CheckHashOrString(L, 2);
    dmhash_t event = dmScript::CheckHashOrString(L, 3);

    size_t data_length;
    const char* data = luaL_checklstring(L, 4, &data_length);

    uint64_t id = 1024;

    SharedMode::Rpc rpc = g_Ctx->m_FusionClient->CreateUserRpc(id, target_player, target_object, descriptor_type, event, data, data_length);

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
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    g_Ctx->m_EventCallback = dmScript::CreateCallback(L, 1);

    return 0;
}

/**
 * Get the player id of the local client
 * @name get_local_player_id
 * @treturn number The player id of the local client
 */
static int GetLocalPlayerId(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    SharedMode::PlayerId id = g_Ctx->m_FusionClient->LocalPlayerId();
    lua_pushinteger(L, id);
    return 1;
}

/**
 * Get the player id of the current owner of an object
 * @name get_owner
 * @string id Id of the object to get the owner for
 * @treturn number The player id of the object's owner
 */
static int GetOwner(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    SharedMode::ObjectRoot* object = GetObject(id);
    if (object)
    {
        SharedMode::PlayerId owner = g_Ctx->m_FusionClient->GetOwner(object);
        lua_pushinteger(L, owner);
    }
    else
    {
        lua_pushnil(L);
    }
    return 1;
}


/**
 * Check if the local client is the owner of an object
 * @name is_owner
 * @string id Id of the object
 * @treturn boolean True if the local client is the owner of the object
 */
static int IsOwner(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    SharedMode::ObjectRoot* object = GetObject(id);
    if (object)
    {
        bool is_owner = g_Ctx->m_FusionClient->IsOwner(object);
        lua_pushboolean(L, is_owner);
    }
    else
    {
        lua_pushboolean(L, 0);
    }
    return 1;
}

/**
 * Check if an object has an owner
 * @name has_owner
 * @string id Id of the object
 * @treturn boolean True if the object has an owner
 */
static int HasOwner(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    SharedMode::ObjectRoot* object = GetObject(id);
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
 * @name set_want_owner
 * @string id Id of the object to own
 */
static int SetWantOwner(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    SharedMode::ObjectRoot* object = GetObject(id);
    if (object)
    {
        g_Ctx->m_FusionClient->SetWantOwner(object);
    }
    return 0;
}

/**
 * Signal desire for the local client to no longer own an object
 * @name set_dont_want_owner
 * @string id Id of the object to disown
 */
static int SetDontWantOwner(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    SharedMode::ObjectRoot* object = GetObject(id);
    if (object)
    {
        g_Ctx->m_FusionClient->SetDontWantOwner(object);
    }
    return 0;
}

/**
 * Explicitly clear the ownership cooldown
 * @name clear_owner_cooldown
 * @string id Id of the object to clear cooldown for
 */
static int ClearOwnerCooldown(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    SharedMode::ObjectRoot* object = GetObject(id);
    if (object)
    {
        g_Ctx->m_FusionClient->ClearOwnerCooldown(object);
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
 * Check if the room has AOI enabled..
 * @name area_of_interest_used
 * @treturn boolean aoi_used True if the room/session has AOI enabled
 */
static int AreaOfInterestUsed(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    bool used = g_Ctx->m_FusionClient->AreaOfInterestUsed();
    lua_pushboolean(L, used);
    return 1;
}

/**
 * Get the AOI cell size. Configured on a room level.
 * @name area_of_interest_cell_size
 * @treturn number size The cell size, uniform across all three axis.
 */
static int AreaOfInterestCellSize(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    int32_t size = g_Ctx->m_FusionClient->AreaOfInterestCellSize();
    lua_pushinteger(L, size);
    return 1;
}

static const luaL_reg Module_methods[] = {
    { "init", Init },
    { "connect", Connect },
    { "join_or_create_room_random", JoinOrCreateRoomRandom },
    { "enable_debug", EnableDebug },
    { "get_local_player_id", GetLocalPlayerId },
    { "player_count", PlayerCount },
    { "get_rtt", GetRtt },
    { "network_time_diff", NetworkTimeDiff },
    
    // area of interest
    { "area_of_interest_used", AreaOfInterestUsed },
    { "area_of_interest_cell_size", AreaOfInterestCellSize },

    // lifecycle
    { "register_object", RegisterObject },
    { "register_scene_object", RegisterSceneObject },
    { "destroy", DestroyObject },
    { "scene_change", SceneChange },

    // rpc and events
    { "send_rpc", SendRpc },
    { "on_event", OnEvent },

    // connection state
    { "is_connected", IsConnected },
    { "is_running", IsRunning },
    { "is_in_room", IsInRoom },
    { "is_joining_or_in_room", IsJoiningOrInRoom },
    { "is_master_client", IsMasterClient },

    // ownership
    { "get_owner", GetOwner },
    { "is_owner", IsOwner },
    { "has_owner", HasOwner },
    { "set_want_owner", SetWantOwner },
    { "set_dont_want_owner", SetDontWantOwner },
    { "clear_owner_cooldown", ClearOwnerCooldown },

    { 0, 0 }
};


static void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);
    luaL_register(L, MODULE_NAME, Module_methods);


    #define SETCONSTANT(name, val) \
    lua_pushnumber(L, (lua_Number) val); \
    lua_setfield(L, -2, #name);

    /**
     * OWNERMODE_TRANSACTION
     * @field OWNERMODE_TRANSACTION
     */
    SETCONSTANT(OWNERMODE_TRANSACTION, SharedMode::ObjectOwnerModes::Transaction)
    /**
     * OWNERMODE_DYNAMIC
     * @field OWNERMODE_DYNAMIC
     */
    SETCONSTANT(OWNERMODE_DYNAMIC, SharedMode::ObjectOwnerModes::Dynamic)
    /**
     * OWNERMODE_MASTERCLIENT
     * @field OWNERMODE_MASTERCLIENT
     */
    SETCONSTANT(OWNERMODE_MASTERCLIENT, SharedMode::ObjectOwnerModes::MasterClient)


    /**
     * OBJECTSETTINGS_NONE
     * @field OBJECTSETTINGS_NONE
     */
    SETCONSTANT(OBJECTSETTINGS_NONE, SharedMode::ObjectSettingsFlags::None)
    /**
     * OBJECTSETTINGS_OWNERLEAVESOWNERTONONE
     * @field OBJECTSETTINGS_OWNERLEAVESOWNERTONONE
     */
    SETCONSTANT(OBJECTSETTINGS_OWNERLEAVESOWNERTONONE, SharedMode::ObjectSettingsFlags::OwnerLeavesOwnerToNone)
    /**
     * OBJECTSETTINGS_ISGLOBALINSTANCE
     * @field OBJECTSETTINGS_ISGLOBALINSTANCE
     */
    SETCONSTANT(OBJECTSETTINGS_ISGLOBALINSTANCE, SharedMode::ObjectSettingsFlags::IsGlobalInstance)


    /**
     * INTERESTMODE_ALL
     * @field INTERESTMODE_ALL
     */
    SETCONSTANT(INTERESTMODE_ALL, SharedMode::ObjectInterestModes::All)
    /**
     * INTERESTMODE_AREA
     * @field INTERESTMODE_AREA
     */
    SETCONSTANT(INTERESTMODE_AREA, SharedMode::ObjectInterestModes::Area)
    /**
     * INTERESTMODE_ASSIGNED
     * @field INTERESTMODE_ASSIGNED
     */
    SETCONSTANT(INTERESTMODE_ASSIGNED, SharedMode::ObjectInterestModes::Assigned)

    #undef SETCONSTANT

    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

dmExtension::Result AppInitializeFusion(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

dmExtension::Result InitializeFusion(dmExtension::Params* params)
{
    dmLogInfo("InitializeFusion");
    LuaInit(params->m_L);
    g_Ctx = new FusionCtx();
    g_Ctx->m_Timestamp = dmTime::GetMonotonicTime();
    g_Ctx->m_ResourceFactory = params->m_ResourceFactory;
    g_Ctx->m_ConfigFile = params->m_ConfigFile;
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
        g_Ctx->m_FusionClient->Shutdown(false);
        delete g_Ctx->m_FusionClient;
        g_Ctx->m_FusionClient = 0;
    }

    dmHashTable<dmhash_t, FusionObject*>::Iterator iter = g_Ctx->m_FusionObjects.GetIterator();
    while(iter.Next())
    {
        FusionObject* object = iter.GetValue();
        free(object);
    }

    if (g_Ctx->m_EventCallback)
    {
        dmScript::DestroyCallback(g_Ctx->m_EventCallback);
        g_Ctx->m_EventCallback = 0;
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
        g_Ctx->m_FusionClient->Photon().Service(true);
        if (g_Ctx->m_FusionClient->Photon().IsInRoom())
        {
            Fusion_TickBeforeFrameEnd();
            g_Ctx->m_FusionClient->UpdateFrameEnd();
            g_Ctx->m_FusionClient->UpdateFrameBegin(dt);
            Fusion_TickAfterFrameBegin(dt);
        }
        // else
        // {
        //     g_Ctx->m_FusionClient->UpdateFrameEnd();
        //     g_Ctx->m_FusionClient->UpdateFrameBegin(dt);
        // }
    }

    return dmExtension::RESULT_OK;
}

dmExtension::Result OnEventFusion(dmExtension::Event* event, dmExtension::Params* params)
{
    dmLogInfo("OnEventFusion");
    return dmExtension::RESULT_OK;
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

static dmExtension::Result OnEventFusion(dmExtension::ExtensionEvent* event, dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

#endif

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, AppInitializeFusion, AppFinalizeFusion, InitializeFusion, UpdateFusion, OnEventFusion, FinalizeFusion)
