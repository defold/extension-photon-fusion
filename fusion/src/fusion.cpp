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
#include <dmsdk/gamesys/components/comp_factory.h>
#include <dmsdk/gameobject/gameobject_props.h>

#include "photon_extension_defines.h"

#if PHOTON_PLATFORM_SUPPORTED

#include "Client.h"
#include "LogUtils.h"
#include "LogOutput.h"

typedef int(*FusionUpdateFn)(double dt);


class FusionDefoldLogOutput : public SharedMode::Logging::LogOutput
{
public:
    FusionDefoldLogOutput() {}
    ~FusionDefoldLogOutput() {};
    char buffer[2000];
    void LogTrace(const wchar_t* message)
    {
        wcstombs(buffer, message, 2000);
        dmLogDebug("%s", buffer);
    }

    void LogDebug(const wchar_t* message)
    {
        wcstombs(buffer, message, 2000);
        dmLogDebug("%s", buffer);
    }

    void LogInfo(const wchar_t* message)
    {
        wcstombs(buffer, message, 2000);
        dmLogInfo("%s", buffer);
    }

    void LogWarning(const wchar_t* message)
    {
        wcstombs(buffer, message, 2000);
        dmLogWarning("%s", buffer);
    }

    void LogError(const wchar_t* message)
    {
        wcstombs(buffer, message, 2000);
        dmLogError("%s", buffer);
    }
};


struct FusionRemoteObject
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
    dmGameObject::HCollection                      m_MainCollection;
    uint64_t                                       m_Timestamp;
    SharedMode::Client*                            m_FusionClient;
    dmHashTable<dmhash_t, SharedMode::ObjectRoot*> m_FusionLocalObjects;
    dmHashTable<dmhash_t, FusionRemoteObject*>     m_FusionRemoteObjects;
    FusionDefoldLogOutput*                         m_FusionDefoldLogOutput;
    FusionUpdateFn                                 m_UpdateFn;

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
    return *(uint32_t*)&f;
}
static float DecompressFloat(int32_t f)
{
    return *(float*)&f;
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
    words[0] = (uint32_t)((h & 0xFFFFFFFF00000000) >> 32);
    words[1] = (uint32_t)((h & 0x00000000FFFFFFFF) >> 0);
    return 2;
}
static size_t PopHash(SharedMode::Word* words, dmhash_t* out)
{
    *out = (dmhash_t)(
        ((uint64_t)words[0]) << 32 |
        ((uint64_t)words[1]) << 0
        );
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

/******
 * Fusion callbacks
 *************************/

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

    dmLogInfo("socket %s, path %s, fragment %s", dmHashReverseSafe64(socket), dmHashReverseSafe64(path), dmHashReverseSafe64(fragment));

    //
    // get factory component
    dmGameObject::HInstance factory_go = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_MainCollection, path);
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
            world, factory, g_Ctx->m_MainCollection,
            id, 
            position, rotation, scale,
            properties, &instance);

    dmGameObject::PropertyContainerDestroy(properties);

    if (dmGameObject::RESULT_OK != r)
    {
        dmLogError("Unable to spawn collection");
        return;
    }


    //
    // add game object to list
    if (g_Ctx->m_FusionRemoteObjects.Full())
    {
        g_Ctx->m_FusionRemoteObjects.OffsetCapacity(100);
    }
    FusionRemoteObject* remote_object = (FusionRemoteObject*)malloc(sizeof(FusionRemoteObject));
    remote_object->m_SharedObject = object;
    g_Ctx->m_FusionRemoteObjects.Put(id, remote_object);
}
static void Fusion_OnSubObjectCreated(const SharedMode::ObjectChild* child)
{
    dmLogInfo("Fusion_OnSubObjectCreated");
}
// TODO Handle DestroyMode
static void Fusion_OnObjectDestroyed(const SharedMode::ObjectRoot* object, const SharedMode::DestroyModes mode)
{
    dmLogInfo("Fusion_OnObjectDestroyed owner: %d local player: %d", object->Owner, g_Ctx->m_FusionClient->LocalPlayerId());

    dmHashTable<dmhash_t, FusionRemoteObject*>::Iterator iter = g_Ctx->m_FusionRemoteObjects.GetIterator();
    while(iter.Next())
    {
        dmhash_t id = iter.GetKey();
        FusionRemoteObject* remote_object = iter.GetValue();
        dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_MainCollection, id);
        if (object == remote_object->m_SharedObject)
        {
            dmLogInfo("Found object to destroy %s", dmHashReverseSafe64(id));
            dmGameObject::Delete(g_Ctx->m_MainCollection, instance, true);
            g_Ctx->m_FusionRemoteObjects.Erase(id);
            free(remote_object);
            return;
        }
    }
    dmLogError("Unable to find object to destroy");
}
static void Fusion_OnObjectOwnerChanged(const SharedMode::ObjectRoot* obj)
{
    dmLogInfo("Fusion_OnObjectOwnerChanged owner: %d local player: %d", obj->Owner, g_Ctx->m_FusionClient->LocalPlayerId());
}
static void Fusion_OnObjectPredictionOverride(const SharedMode::ObjectRoot* obj)
{
    dmLogInfo("Fusion_OnObjectPredictionOverride");
}
static void Fusion_OnRoomJoin()
{
    dmLogInfo("Fusion_OnRoomJoin local player: %d", g_Ctx->m_FusionClient->LocalPlayerId());
    const char* name = g_Ctx->m_FusionClient->Photon().LoadBalancingClient().getCurrentlyJoinedRoom().getName().UTF8Representation().cstr();
    dmLogInfo("%s", name);
}
static void Fusion_OnRoomLeave()
{
    dmLogInfo("Fusion_OnRoomLeave");
}
static void Fusion_OnRpc(const SharedMode::Rpc& rpc)
{
    dmLogInfo("Fusion_OnRpc");
}
static void Fusion_OnSceneChange(uint32_t index, uint32_t sequence, SharedMode::Data data)
{
    dmLogInfo("Fusion_OnSceneChange");
}



/******
 * Fusion update lifecycle
 *************************/

void Fusion_TickBeforeFrameEnd()
{
    dmLogInfo("TickBeforeFrameEnd");

    const uint32_t scriptc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_MainCollection, dmHashString64("scriptc"));
    const uint32_t spritec = dmGameObject::GetComponentTypeIndex(g_Ctx->m_MainCollection, dmHashString64("spritec"));
    const uint32_t modelc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_MainCollection, dmHashString64("modelc"));

    dmHashTable<dmhash_t, SharedMode::ObjectRoot*>::Iterator iter = g_Ctx->m_FusionLocalObjects.GetIterator();
    while(iter.Next())
    {
        dmhash_t id = iter.GetKey();
        dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_MainCollection, id);
        SharedMode::ObjectRoot* object = iter.GetValue();

        SharedMode::Word *words = object->Words.Ptr;
        dmVMath::Point3 pos = dmGameObject::GetPosition(instance);
        dmVMath::Quat rot = dmGameObject::GetRotation(instance);
        dmVMath::Vector3 scale = dmGameObject::GetScale(instance);
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
        dmLogInfo("object %s:%s#%s has %d components", dmHashReverseSafe64(socket), dmHashReverseSafe64(path), dmHashReverseSafe64(fragment), component_count);
        for (int i = 0; i < component_count; i++)
        {
            dmhash_t component_id;
            uint32_t component_type;
            header_offset += PopHash(header + header_offset, &component_id);
            header_offset += PopUint32(header + header_offset, &component_type);
            dmLogInfo("  component %d with id %s has type %d", i, dmHashReverseSafe64(component_id), component_type);

            if (component_type == spritec)
            {

            }
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
void Fusion_TickAfterFrameBegin(double dt)
{
    // TODO Respect object->IgnoreProperties?
    // dmLogInfo("Fusion_TickAfterFrameBegin");
    dmHashTable<dmhash_t, FusionRemoteObject*>::Iterator iter = g_Ctx->m_FusionRemoteObjects.GetIterator();
    while(iter.Next())
    {
        dmhash_t id = iter.GetKey();
        dmLogInfo("Fusion_TickAfterFrameBegin %s", dmHashReverseSafe64(id));
        dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_MainCollection, id);
        FusionRemoteObject* remote_object = iter.GetValue();
        SharedMode::ObjectRoot* object = remote_object->m_SharedObject;

        SharedMode::Word *words = object->Words.Ptr;
        if (words == 0x0)
        {
            continue;
        }
        size_t word_offset = 0;

        word_offset += PopPoint3(words + word_offset, &remote_object->m_Position);
        // dmGameObject::SetPosition(instance, remote_object->m_Position);

        dmVMath::Point3 curpos = dmGameObject::GetPosition(instance);
        dmVMath::Point3 newpos = LerpPoint(0.1, curpos, remote_object->m_Position);
        dmGameObject::SetPosition(instance, newpos);

        word_offset += PopQuat(words + word_offset, &remote_object->m_Rotation);
        // dmGameObject::SetRotation(instance, remote_object->m_Rotation);
        dmVMath::Quat currot = dmGameObject::GetRotation(instance);
        dmVMath::Quat newrot = dmVMath::Slerp(0.1, currot, remote_object->m_Rotation);
        dmGameObject::SetRotation(instance, newrot);
        
        word_offset += PopVector3(words + word_offset, &remote_object->m_Scale);
        // dmGameObject::SetScale(instance, remote_object->m_Scale);
        dmVMath::Vector3 curscl = dmGameObject::GetScale(instance);
        dmVMath::Vector3 newscl = dmVMath::Lerp(0.1, curscl, remote_object->m_Scale);
        dmGameObject::SetScale(instance, newscl);
    }
}





int Fusion_Update(double dt)
{
    return 0;
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


    g_Ctx->m_FusionLocalObjects.SetCapacity(100, 100);
    g_Ctx->m_FusionRemoteObjects.SetCapacity(100, 100);


    if (g_Ctx->m_FusionClient)
    {
        delete g_Ctx->m_FusionClient;
    }
    g_Ctx->m_FusionClient = new SharedMode::Client(appId, appVersion);
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
    g_Ctx->m_UpdateFn = &Fusion_Update;

    const char* main_collection_path = dmConfigFile::GetString(g_Ctx->m_ConfigFile, "bootstrap.main_collection", 0);
    dmLogInfo("main collection %s", main_collection_path);
    dmResource::Result res = dmResource::Get(g_Ctx->m_ResourceFactory, main_collection_path, (void**) &g_Ctx->m_MainCollection);
    if (dmResource::RESULT_OK != res)
    {
        dmLogError("Failed to get main collection '%s'", main_collection_path);
        return 0;
    }
    assert(g_Ctx->m_MainCollection != 0);

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
    g_Ctx->m_FusionClient->ConnectCloud(region, userid, server);
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
    g_Ctx->m_FusionClient->Photon().JoinOrCreateRoomRandom(name, options);
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

    dmLogInfo("RegisterObject local player id: %d", g_Ctx->m_FusionClient->LocalPlayerId());

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    uint32_t scene = (uint32_t)luaL_checknumber(L, 2);
    dmMessage::URL* factory_url = dmScript::CheckURL(L, 3);
    bool master = g_Ctx->m_FusionClient->IsMasterClient();


    uint8_t header[1000];
    size_t headerLength = 0;
    headerLength += PushHash(header + headerLength, factory_url->m_Socket);
    headerLength += PushHash(header + headerLength, factory_url->m_Path);
    headerLength += PushHash(header + headerLength, factory_url->m_Fragment);

    size_t componentCountOffset = headerLength;
    headerLength += PushUint16(header + headerLength, 0);

    const uint32_t scriptc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_MainCollection, dmHashString64("scriptc"));
    const uint32_t spritec = dmGameObject::GetComponentTypeIndex(g_Ctx->m_MainCollection, dmHashString64("spritec"));
    const uint32_t modelc = dmGameObject::GetComponentTypeIndex(g_Ctx->m_MainCollection, dmHashString64("modelc"));
    dmLogInfo("scriptc %d spritec %d modelc %d", scriptc, spritec, modelc);

    // pos, rot, scale
    size_t words = 3 + 4 + 3;

    dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_MainCollection, id);
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
            return 0;
        }

        dmLogInfo("Got first component '%s' with type %d", dmHashReverseSafe64(component_id), component_type);
        headerLength += PushHash(header + headerLength, component_id);
        headerLength += PushUint32(header + headerLength, component_type);

        header[componentCountOffset];
        if (component_type == scriptc)
        {
            words += 0;
        }
        else if (component_type == modelc)
        {
            words += 0;
        }
        else if (component_type == spritec)
        {
            // animation (2), hflip, vflip
            words += 4;
        }
        else
        {

        }

        component_index++;
    }

    // write component count
    headerLength += PushUint16(header + componentCountOffset, component_index);

    SharedMode::TypeRef type;
    type.Hash = 0;
    type.WordCount = words;

    SharedMode::ObjectSettingsFlags objectSettingsFlags = SharedMode::ObjectSettingsFlags::None;
    SharedMode::ObjectOwnerModes objectOwnerMode = master ? SharedMode::ObjectOwnerModes::MasterClient : SharedMode::ObjectOwnerModes::Dynamic;
    SharedMode::ObjectInterestModes objectInterestModes = SharedMode::ObjectInterestModes::All;
    SharedMode::ObjectFlags objectFlags = SharedMode::ObjectFlags(objectSettingsFlags, objectOwnerMode, objectInterestModes);

    SharedMode::ObjectRoot* object = g_Ctx->m_FusionClient->CreateObject(words, type, (char*)header, headerLength, scene, objectFlags);

    if (g_Ctx->m_FusionLocalObjects.Full())
    {
        g_Ctx->m_FusionLocalObjects.OffsetCapacity(100);
    }
    g_Ctx->m_FusionLocalObjects.Put(id, object);
    dmLogInfo("RegisterObject %p", object)

    return 0;
}

/** Destroy an object
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

    dmLogInfo("DestroyObject local player id: %d", g_Ctx->m_FusionClient->LocalPlayerId());

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    SharedMode::ObjectRoot* object = *g_Ctx->m_FusionLocalObjects.Get(id);
    bool ok = g_Ctx->m_FusionClient->DestroyObjectLocal(object, true);
    g_Ctx->m_FusionLocalObjects.Erase(id);

    return 0;
}

static const luaL_reg Module_methods[] = {
    { "init", Init },
    { "connect", Connect },
    { "join_or_create_room_random", JoinOrCreateRoomRandom },
    { "register", RegisterObject },
    { "destroy", DestroyObject },
    { "is_connected", IsConnected },
    { "is_running", IsRunning },
    { "is_in_room", IsInRoom },
    { "is_joining_or_in_room", IsJoiningOrInRoom },
    { "enable_debug", EnableDebug },
    { 0, 0 }
};


static void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);
    luaL_register(L, MODULE_NAME, Module_methods);

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

    dmHashTable<dmhash_t, FusionRemoteObject*>::Iterator iter = g_Ctx->m_FusionRemoteObjects.GetIterator();
    while(iter.Next())
    {
        FusionRemoteObject* remote_object = iter.GetValue();
        free(remote_object);
    }

    if (g_Ctx->m_MainCollection)
    {
        dmResource::Release(g_Ctx->m_ResourceFactory, g_Ctx->m_MainCollection);
        g_Ctx->m_MainCollection = 0;
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

    // int res = g_Ctx->m_UpdateFn(dt);
    if (g_Ctx->m_FusionClient)
    {
        if (g_Ctx->m_FusionClient->Photon().IsInRoom())
        {
            Fusion_TickBeforeFrameEnd();
            g_Ctx->m_FusionClient->UpdateFrameEnd();
            g_Ctx->m_FusionClient->UpdateFrameBegin(dt);
            Fusion_TickAfterFrameBegin(dt);
        }
        else
        {
            g_Ctx->m_FusionClient->UpdateFrameEnd();
            g_Ctx->m_FusionClient->UpdateFrameBegin(dt);
        }

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
