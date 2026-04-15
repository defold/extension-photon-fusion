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


class FusionDefoldLogOutput : public PhotonCommon::LogOutput
{
public:
    FusionDefoldLogOutput() {}
    ~FusionDefoldLogOutput() {};
    void LogTrace(const PhotonCommon::CharType* message)
    {
        dmLogDebug("[internal] %s", (char*)message);
    }

    void LogDebug(const PhotonCommon::CharType* message)
    {
        dmLogDebug("[internal] %s", (char*)message);
    }

    void LogInfo(const PhotonCommon::CharType* message)
    {
        dmLogInfo("[internal] %s", (char*)message);
    }

    void LogWarning(const PhotonCommon::CharType* message)
    {
        dmLogWarning("[internal] %s", (char*)message);
    }

    void LogError(const PhotonCommon::CharType* message)
    {
        dmLogError("[internal] %s", (char*)message);
    }
};


struct FusionObject
{
    dmhash_t                 m_Id;
    SharedMode::ObjectRoot*  m_SharedObject;
    dmVMath::Point3          m_Position;
    dmVMath::Quat            m_Rotation;
    dmVMath::Vector3         m_Scale;
    bool                     m_HasAuthority;
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
    SharedMode::Client*                            m_FusionClient;
    dmHashTable<dmhash_t, FusionObject*>           m_FusionObjects;
    FusionDefoldLogOutput*                         m_FusionDefoldLogOutput;
    dmScript::LuaCallbackInfo*                     m_EventCallback;
    PhotonMatchmaking::ConnectionState             m_ConnectionState;

    // Component type identifiers
    uint32_t                                       m_Scriptc;
    uint32_t                                       m_Spritec;
    uint32_t                                       m_Modelc;
    uint32_t                                       m_Collisionobjectc;
    uint32_t                                       m_Particlefxc;
    uint32_t                                       m_Labelc;

    // Event constants
    dmhash_t                                       m_EventOnObjectCreated;
    dmhash_t                                       m_EventOnSubObjectCreated;
    dmhash_t                                       m_EventOnObjectDestroyed;
    dmhash_t                                       m_EventOnSubObjectDestroyed;
    dmhash_t                                       m_EventOnObjectOwnerChanged;
    dmhash_t                                       m_EventOnObjectPredictionOverride;
    dmhash_t                                       m_EventOnLobbyStats;
    dmhash_t                                       m_EventOnRoomJoined;
    dmhash_t                                       m_EventOnRoomLeft;
    dmhash_t                                       m_EventOnRpc;
    dmhash_t                                       m_EventOnSceneChange;
    dmhash_t                                       m_EventOnDestroyedMapActor;
    dmhash_t                                       m_EventOnInterestEnter;
    dmhash_t                                       m_EventOnInterestExit;
    dmhash_t                                       m_EventOnForcedDisconnect;
    dmhash_t                                       m_EventOnFusionStart;
    dmhash_t                                       m_EventOnConnected;
    dmhash_t                                       m_EventOnDisconnected;

    FusionCtx()
    {
        memset((void*)this, 0, sizeof(*this));
        m_ConnectionState = PhotonMatchmaking::ConnectionState::Disconnected;
        m_EventOnObjectCreated = dmHashString64("OnObjectCreated");
        m_EventOnSubObjectCreated = dmHashString64("OnSubObjectCreated");
        m_EventOnObjectDestroyed = dmHashString64("OnObjectDestroyed");
        m_EventOnSubObjectDestroyed = dmHashString64("OnSubObjectDestroyed");
        m_EventOnObjectOwnerChanged = dmHashString64("OnObjectOwnerChanged");
        m_EventOnObjectPredictionOverride = dmHashString64("OnObjectPredictionOverride");
        m_EventOnLobbyStats = dmHashString64("OnLobbyStats");
        m_EventOnRoomJoined = dmHashString64("OnRoomJoined");
        m_EventOnRoomLeft = dmHashString64("OnRoomLeft");
        m_EventOnRpc = dmHashString64("OnRpc");
        m_EventOnSceneChange = dmHashString64("OnSceneChange");
        m_EventOnDestroyedMapActor = dmHashString64("OnDestroyedMapActor");
        m_EventOnInterestEnter = dmHashString64("OnInterestEnter");
        m_EventOnInterestExit = dmHashString64("OnInterestExit");
        m_EventOnForcedDisconnect = dmHashString64("OnForcedDisconnect");
        m_EventOnFusionStart = dmHashString64("OnFusionStart");
        m_EventOnConnected = dmHashString64("OnConnected");
        m_EventOnDisconnected = dmHashString64("OnDisconnected");
    }
};

FusionCtx* g_Ctx = 0;

/******
 * Helpers
 *************************/

static PhotonCommon::StringType ToStringType(const char* text)
{
    return PhotonCommon::to_string_type((const PhotonCommon::CharType*)text);
}

static void LuaTableToStdStringVector(lua_State* L, int index, std::vector<PhotonCommon::StringType>& v)
{
    if (lua_type(L, index) == LUA_TTABLE)
    {
        size_t len = lua_objlen(L, index);
        for (int i = 0; i < len; i++)
        {
            lua_pushinteger(L, i + 1);
            lua_gettable(L, index);
            const char* s = luaL_checkstring(L, -1);
            v[i] = ToStringType(s);
            lua_pop(L, 1); // pop value
        }
    }
}

static void LuaTableToPropertyMap(lua_State* L, int index, PhotonMatchmaking::PropertyMap map)
{
    if (lua_type(L, index) == LUA_TTABLE)
    {
        lua_pushnil(L);
        while (lua_next(L, index) != 0)
        {
            const char* key = luaL_checkstring(L, -2);
            int type = lua_type(L, -1);
            if (type == LUA_TNUMBER)
            {
                map[ToStringType(key)] = luaL_checknumber(L, -1);
            }
            else if (type == LUA_TBOOLEAN)
            {
                map[ToStringType(key)] = lua_toboolean(L, -1);
            }
            else if (type == LUA_TSTRING)
            {
                map[ToStringType(key)] = ToStringType(luaL_checkstring(L, -1));
            }
            
            lua_pop(L, 1); // pop value
        }
    }
}

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

static void PrintStack(lua_State *L) {
    int top = lua_gettop(L);
    int i;

    dmLogInfo("stack top = %d", top);

    for (i = 1; i <= top; i++) {
        int t = lua_type(L, i);

        switch (t) {
            case LUA_TSTRING:
                dmLogInfo("[%d] %s: '%s'", i, lua_typename(L, t), lua_tostring(L, i));
                break;

            case LUA_TBOOLEAN:
                dmLogInfo("[%d] %s: %s", i, lua_typename(L, t), lua_toboolean(L, i) ? "true" : "false");
                break;

            case LUA_TNUMBER:
                dmLogInfo("[%d] %s: %g", i, lua_typename(L, t), lua_tonumber(L, i));
                break;

            case LUA_TNIL:
                dmLogInfo("[%d] %s: nil", i, lua_typename(L, t));
                break;

            default:
                /* userdata, table, function, thread, lightuserdata */
                dmLogInfo("[%d] %s: %p", i, lua_typename(L, t), lua_topointer(L, i));
                break;
        }
    }
}

static dmhash_t ResolveId(lua_State* L, int index)
{
    if (lua_isnoneornil(L, index))
    {
        dmGameObject::HInstance caller_instance = dmScript::CheckGOInstance(L);
        return dmGameObject::GetIdentifier(caller_instance);
    }
    else
    {
        return dmScript::CheckHashOrString(L, 1);
    }
}

/******
 * Object handlers
 *************************/

static SharedMode::ObjectRoot* GetSharedObject(dmhash_t id)
{
    FusionObject** object = g_Ctx->m_FusionObjects.Get(id);
    if (object)
    {
        return (*object)->m_SharedObject;
    }

    return 0;
}

static bool HasFusionObject(const SharedMode::ObjectRoot* object)
{
    dmHashTable<dmhash_t, FusionObject*>::Iterator iter = g_Ctx->m_FusionObjects.GetIterator();
    while(iter.Next())
    {
        FusionObject* fusion_object = iter.GetValue();
        if (object == fusion_object->m_SharedObject)
        {
            return true;
        }
    }
    return false;
}

static FusionObject* FindFusionObject(const SharedMode::ObjectRoot* object)
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

static dmhash_t DeleteFusionObject(const SharedMode::ObjectRoot* object)
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

static bool CreateGameObject(dmhash_t id, const SharedMode::ObjectRoot* object)
{
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
    dmVMath::Point3 position = dmVMath::Point3(3.0, 0, 0);
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

static FusionObject* CreateFusionObject(dmhash_t id, SharedMode::ObjectRoot* object)
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
    fusion_object->m_HasAuthority = (instance != 0);
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
    const SharedMode::ObjectRoot* object = fusion_object->m_SharedObject;

    dmGameObject::HInstance instance = dmGameObject::GetInstanceFromIdentifier(g_Ctx->m_Collection, id);

    SharedMode::Word *words = object->Words.Ptr;
    dmVMath::Point3 pos = dmGameObject::GetPosition(instance);
    dmVMath::Quat rot = dmGameObject::GetRotation(instance);
    dmVMath::Vector3 scale = dmGameObject::GetScale(instance);

    // if (object->Flags.InterestMode == SharedMode::ObjectInterestModes::Area)
    // {
    //     SharedMode::AOILocation location = g_Ctx->m_FusionClient->CalculateAreaOfInterestLocation(pos.getX(), pos.getY(), pos.getZ());
    //     g_Ctx->m_FusionClient->SetAreaOfInterestLocation(object, location);
    // }

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
    const SharedMode::ObjectRoot* object = fusion_object->m_SharedObject;
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

static dmGameObject::Result DoRegisterObject(dmhash_t id, dmMessage::URL* factory_url, uint32_t scene, SharedMode::ObjectOwnerModes owner_mode)
{
    uint8_t header[1000];
    size_t headerLength;
    size_t wordsCount;
    bool ok = BuildObjectHeader(id, factory_url, header, headerLength, wordsCount);
    if (!ok)
    {
        return dmGameObject::RESULT_INVALID_OPERATION;
    }

    SharedMode::TypeRef type;
    type.Hash = 0;
    type.WordCount = wordsCount + SharedMode::Object::ExtraTailWords;

    SharedMode::ObjectSpecialFlags specialFlags = SharedMode::ObjectSpecialFlags::None;
    SharedMode::ObjectRoot* object = g_Ctx->m_FusionClient->CreateObject(wordsCount,
                                                                         type,
                                                                         (PhotonCommon::CharType*)header,
                                                                         headerLength,
                                                                         scene,
                                                                         owner_mode,
                                                                         specialFlags);

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



static void Fusion_OnObjectReady(SharedMode::ObjectRoot* object)
{
    dmLogInfo("Fusion_OnObjectReady owner: %d local player: %d", object->Owner, g_Ctx->m_FusionClient->LocalPlayerId());

    dmhash_t id = dmGameObject::CreateInstanceId();
    FusionObject* fusion_object = CreateFusionObject(id, object);
    if (fusion_object)
    {
        CallListener(g_Ctx->m_EventOnObjectCreated, fusion_object->m_Id);
    }
    else
    {
        DeleteGameObject(id);
    }
}
static void Fusion_OnSubObjectCreated(SharedMode::ObjectChild* child)
{
    dmLogInfo("Fusion_OnSubObjectCreated - NOT IMPLEMENTED");
    CallListener(g_Ctx->m_EventOnSubObjectCreated);
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

    FusionObject* fusion_object = FindFusionObject(object);
    if (!fusion_object)
    {
        dmLogError("Unable to find object to destroy");
        return;
    }

    dmhash_t id = DeleteFusionObject(object);
    if (id)
    {
        CallListener(g_Ctx->m_EventOnObjectDestroyed, id);
    }
    else
    {
        dmLogError("Unable to find object to destroy");
    }
}
static void Fusion_OnSubObjectDestroyed(SharedMode::ObjectChild* child, const SharedMode::DestroyModes mode)
{
    dmLogInfo("Fusion_OnSubObjectDestroyed - NOT IMPLEMENTED");
    CallListener(g_Ctx->m_EventOnSubObjectDestroyed);
}
static void Fusion_OnObjectOwnerChanged(SharedMode::ObjectRoot* object)
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
        dmScript::PushHash(L, g_Ctx->m_EventOnObjectOwnerChanged);
        lua_newtable(L);
        dmScript::PushHash(L, fusion_object->m_Id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, object->Owner);
        lua_setfield(L, -2, "owner");
        CallListener(L, 3, 0);
    }
}
static void Fusion_OnObjectPredictionOverride(SharedMode::ObjectRoot* object)
{
    dmLogInfo("Fusion_OnObjectPredictionOverride");
    CallListener(g_Ctx->m_EventOnObjectPredictionOverride);
}
static void Fusion_OnLobbyStats(const std::vector<PhotonMatchmaking::LobbyStats>& lobbyStats)
{
    dmLogInfo("Fusion_OnLobbyStats");
    for (int index = 0; index < lobbyStats.size(); index++)
    {
        PhotonMatchmaking::LobbyStats lobby = lobbyStats[index];
        dmLogInfo("Fusion_OnLobbyStats %d %s", index, (char*)lobby.name.c_str());
    }
    CallListener(g_Ctx->m_EventOnLobbyStats);
}
static void Fusion_OnRoomJoined()
{
    dmLogInfo("Fusion_OnRoomJoined local player: %d", g_Ctx->m_FusionClient->LocalPlayerId());
    lua_State* L = SetupListener();
    if(L)
    {
        const char* name = (const char*)g_Ctx->m_FusionClient->GetRealtimeClient().GetCurrentRoom()->GetName().c_str();

        dmScript::PushHash(L, g_Ctx->m_EventOnRoomJoined);
        lua_newtable(L);
        lua_pushstring(L, name);
        lua_setfield(L, -2, "name");
        CallListener(L, 3, 0);
    }
}
static void Fusion_OnRoomLeft()
{
    dmLogInfo("Fusion_OnRoomLeft");
    CallListener(g_Ctx->m_EventOnRoomLeft);
}
static void Fusion_OnRpc(SharedMode::Rpc& rpc)
{
    dmLogInfo("Fusion_OnRpc");
    lua_State* L = SetupListener();
    if(L)
    {
        dmScript::PushHash(L, g_Ctx->m_EventOnRpc);

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
        dmScript::PushHash(L, g_Ctx->m_EventOnSceneChange);

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
void Fusion_OnDestroyedMapActor(uint32_t scene, SharedMode::ObjectId id)
{
    dmLogInfo("Fusion_OnDestroyedMapActor");
    lua_State* L = SetupListener();
    if(L)
    {
        dmScript::PushHash(L, g_Ctx->m_EventOnDestroyedMapActor);

        lua_newtable(L);
        lua_pushinteger(L, scene);
        lua_setfield(L, -2, "scene");
        lua_pushinteger(L, id.Origin);
        lua_setfield(L, -2, "id");

        CallListener(L, 3, 0);
    }
}
void Fusion_OnInterestEnter(SharedMode::ObjectRoot* object)
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
        CallListener(g_Ctx->m_EventOnInterestEnter, fusion_object->m_Id);
    }
}
void Fusion_OnInterestExit(SharedMode::ObjectRoot* object)
{
    dmLogInfo("Fusion_OnInterestExit");
    dmhash_t id = DeleteFusionObject(object);
    if (id)
    {
        CallListener(g_Ctx->m_EventOnInterestExit, id);
    }
}
void Fusion_OnForcedDisconnect(std::string message)
{
    dmLogInfo("Fusion_OnForcedDisconnect");
    CallListener(g_Ctx->m_EventOnForcedDisconnect, message.c_str());
}
void Fusion_OnFusionStart()
{
    dmLogInfo("Fusion_OnFusionStart");
    CallListener(g_Ctx->m_EventOnFusionStart);
}


/******
 * Fusion update lifecycle
 *************************/

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
            SerializeFusionObject(object);
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
    g_Ctx->m_FusionObjects.SetCapacity(100, 100);

    if (g_Ctx->m_FusionClient)
    {
        delete g_Ctx->m_FusionClient;
    }

    PhotonMatchmaking::ClientConstructOptions options = PhotonMatchmaking::ClientConstructOptions();
    options.appId = ToStringType(appId);
    options.appVersion = ToStringType(appVersion);
    PhotonMatchmaking::RealtimeClient* realtimeClient = new PhotonMatchmaking::RealtimeClient(options);

    g_Ctx->m_FusionClient = new SharedMode::Client(*realtimeClient);
    g_Ctx->m_FusionClient->OnForcedDisconnect.Subscribe(Fusion_OnForcedDisconnect);
    g_Ctx->m_FusionClient->OnFusionStart.Subscribe(Fusion_OnFusionStart);
    g_Ctx->m_FusionClient->OnObjectReady.Subscribe(Fusion_OnObjectReady);
    g_Ctx->m_FusionClient->OnInterestEnter.Subscribe(Fusion_OnInterestEnter);
    g_Ctx->m_FusionClient->OnInterestExit.Subscribe(Fusion_OnInterestExit);
    g_Ctx->m_FusionClient->OnSubObjectCreated.Subscribe(Fusion_OnSubObjectCreated);
    g_Ctx->m_FusionClient->OnObjectDestroyed.Subscribe(Fusion_OnObjectDestroyed);
    g_Ctx->m_FusionClient->OnSubObjectDestroyed.Subscribe(Fusion_OnSubObjectDestroyed);
    g_Ctx->m_FusionClient->OnRpc.Subscribe(Fusion_OnRpc);
    g_Ctx->m_FusionClient->OnSceneChange.Subscribe(Fusion_OnSceneChange);
    g_Ctx->m_FusionClient->OnObjectOwnerChanged.Subscribe(Fusion_OnObjectOwnerChanged);
    g_Ctx->m_FusionClient->OnObjectPredictionOverride.Subscribe(Fusion_OnObjectPredictionOverride);
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
    if (g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected())
    {
        luaL_error(L, "Fusion is already connected");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    PhotonMatchmaking::ConnectOptions connectOptions = PhotonMatchmaking::ConnectOptions();
    connectOptions.useBackgroundSendReceiveThread = false;

    const char* username = 0x0;
    if (lua_isstring(L, 1))
    {
        username = luaL_checkstring(L, 1);
    }
    connectOptions.username = (const PhotonCommon::CharType*)username;

    const char* region = dmConfigFile::GetString(g_Ctx->m_ConfigFile, "fusion.default_region", "eu");
    if (lua_isstring(L, 2))
    {
        region = luaL_checkstring(L, 2);
    }

    g_Ctx->m_FusionClient->GetRealtimeClient().Connect(connectOptions);

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
    dmLogInfo("GetState");

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
 * @string room_name
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
    bool connected = g_Ctx->m_FusionClient->GetRealtimeClient().IsConnected();
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
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);
    bool enable = lua_toboolean(L, 1);
    if (enable)
    {
        // g_Ctx->m_FusionClient->Photon().SetLogLevel(ExitGames::Common::DebugLevel::ALL);
        PhotonCommon::AddLogOutput(g_Ctx->m_FusionDefoldLogOutput);
        PhotonCommon::SetLogLevelsFromBitmask(0xFF);
    }
    else
    {
        // g_Ctx->m_FusionClient->Photon().SetLogLevel(ExitGames::Common::DebugLevel::OFF);
        PhotonCommon::RemoveLogOutput(g_Ctx->m_FusionDefoldLogOutput);
        PhotonCommon::SetLogLevelsFromBitmask(0x0);
    }
    return 0;
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
    SharedMode::ObjectOwnerModes ownerMode = (SharedMode::ObjectOwnerModes)luaL_checknumber(L, 4);

    uint8_t header[1000];
    size_t headerLength;
    size_t wordsCount;
    bool ok = BuildObjectHeader(id, factory_url, header, headerLength, wordsCount);
    if (!ok)
    {
        luaL_error(L, "Unable to build object header");
        return 0;
    }

    SharedMode::TypeRef type;
    type.Hash = 0;
    type.WordCount = wordsCount + SharedMode::Object::ExtraTailWords;

    bool alreadyPopulated;
    SharedMode::ObjectSpecialFlags specialFlags = SharedMode::ObjectSpecialFlags::None;
    SharedMode::ObjectRoot* object = g_Ctx->m_FusionClient->CreateSceneObject(alreadyPopulated,
                                                                              wordsCount,
                                                                              type,
                                                                              (PhotonCommon::CharType*)header,
                                                                              headerLength,
                                                                              scene,
                                                                              factory_url->m_Path,
                                                                              ownerMode,
                                                                              specialFlags);
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

/** Create a networked game object
 * @name spawn
 * @string factory_url
 * @vector3 position
 * @quat rotation
 * @number scene
 * @number owner_mode
 * @treturn hash Game object id
 */
static int SpawnObject(lua_State* L)
{
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
    SharedMode::ObjectOwnerModes ownerMode = (SharedMode::ObjectOwnerModes)luaL_checknumber(L, 5);

    r = DoRegisterObject(id, &factory_url, scene, ownerMode);
    if (dmGameObject::RESULT_OK != r)
    {
        luaL_error(L, "Unable to spawn game object");
        dmGameObject::Delete(collection, instance, true);
        return 0;
    }

    dmScript::PushHash(L, id);

    return 1;
}


/** Register an object
 * @name register
 * @string factory_url
 * @string id
 * @number scene
 * @number owner_mode
 */
static int RegisterObject(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmMessage::URL* factory_url = dmScript::CheckURL(L, 1);
    dmhash_t id = dmScript::CheckHashOrString(L, 2);
    uint32_t scene = (uint32_t)luaL_checknumber(L, 3);
    SharedMode::ObjectOwnerModes ownerMode = (SharedMode::ObjectOwnerModes)luaL_checknumber(L, 4);

    dmGameObject::Result r = DoRegisterObject(id, factory_url, scene, ownerMode);
    if (dmGameObject::RESULT_OK != r)
    {
        luaL_error(L, "Unable to create object");
        return 0;
    }

    return 0;
}

/** Unregister a previously registered object
 * @name unregister
 * @string id
 */
static int UnregisterObject(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = dmScript::CheckHashOrString(L, 1);
    dmLogInfo("DestroyObject id: %s", dmHashReverseSafe64(id));
    SharedMode::ObjectRoot* object = GetSharedObject(id);
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
 * @name rpc
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

    uint64_t id = 1024; // User RPC

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
 * @name get_owner_id
 * @string id Id of the object to get the owner for
 * @treturn number The player id of the object's owner
 */
static int GetOwnerId(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    dmhash_t id = ResolveId(L, 1);
    const SharedMode::ObjectRoot* object = GetSharedObject(id);
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

    dmhash_t id = ResolveId(L, 1);
    const SharedMode::ObjectRoot* object = GetSharedObject(id);
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
 * Check if this client has authority over a game object. Use this to decide if
 * user input should be handled or not.
 * @name has_authority
 * @string id
 * @treturn boolean authority Returns true if the client has authority
 */
static int HasAuthority(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 1);

    dmhash_t id = ResolveId(L, 1);
    FusionObject** object = g_Ctx->m_FusionObjects.Get(id);
    lua_pushboolean(L, object ? (*object)->m_HasAuthority : true);

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

    dmhash_t id = ResolveId(L, 1);
    const SharedMode::ObjectRoot* object = GetSharedObject(id);
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
 * @string id Id of the object to own
 */
static int WantAuthority(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    bool claim = lua_toboolean(L, 1);
    dmhash_t id = ResolveId(L, 2);
    SharedMode::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        if (claim)
        {
            g_Ctx->m_FusionClient->SetWantOwner(object);
        }
        else
        {
            g_Ctx->m_FusionClient->SetDontWantOwner(object);
        }
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

    dmhash_t id = ResolveId(L, 1);
    SharedMode::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        g_Ctx->m_FusionClient->ClearOwnerCooldown(object);
    }
    return 0;
}

/**
 * Set the send rate of an objeht. This decided how much bandwidth to allocate
 * @name set_send_rate
 * @string id
 * @number send_rate 
 * @string id Id of the object to send rate for
 */
static int SetSendRate(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = ResolveId(L, 2);
    SharedMode::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        int32_t rate = (int32_t)luaL_checknumber(L, 1);
        g_Ctx->m_FusionClient->SetSendRate(object, rate);
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
 * @string id Id of the object to send rate for
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
    SharedMode::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        int32_t rate = (int32_t)luaL_checknumber(L, 1);
        g_Ctx->m_FusionClient->SetLocalSendRate(object, rate);
    }
    return 0;
}

/**
 * Reset the send rate of an object.
 * @name reset_send_rate
 * @string id Id of the object to reset send rate for
 */
static int ResetSendRate(lua_State* L)
{
    if (!g_Ctx->m_FusionClient)
    {
        luaL_error(L, "No Fusion client");
        return 0;
    }

    DM_LUA_STACK_CHECK(L, 0);

    dmhash_t id = ResolveId(L, 1);
    SharedMode::ObjectRoot* object = GetSharedObject(id);
    if (object)
    {
        g_Ctx->m_FusionClient->ResetSendRate(object);
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
    SharedMode::ObjectRoot* object = GetSharedObject(id);
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
    SharedMode::ObjectRoot* object = GetSharedObject(id);
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
    SharedMode::ObjectRoot* object = GetSharedObject(id);
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

    g_Ctx->m_FusionClient->SetAreaKeys(keys);
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
    g_Ctx->m_FusionClient->AddUserKey(key);
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
    g_Ctx->m_FusionClient->RemoveUserKey(key);
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
    { "register_object", RegisterObject },
    { "register_scene_object", RegisterSceneObject },
    { "unregister_object", UnregisterObject },
    { "scene_change", SceneChange },

    // rpc and events
    { "rpc", SendRpc },
    { "on_event", OnEvent },

    // connection state
    { "is_connected", IsConnected },
    { "is_running", IsRunning },
    { "is_in_room", IsInRoom },
    { "is_master_client", IsMasterClient },

    // ownership
    { "get_owner_id", GetOwnerId },
    { "is_owner", IsOwner },
    { "has_owner", HasOwner },
    { "has_authority", HasAuthority },
    { "want_authority", WantAuthority },
    { "clear_owner_cooldown", ClearOwnerCooldown },
    { "set_local_send_rate", SetLocalSendRate },
    { "set_send_rate", SetSendRate },
    { "reset_send_rate", ResetSendRate },

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
     * OWNERMODE_PLAYERATTACHED
     * @field OWNERMODE_PLAYERATTACHED
     */
    SETCONSTANT(OWNERMODE_PLAYERATTACHED, SharedMode::ObjectOwnerModes::PlayerAttached)
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
     * OWNERMODE_GAMEGLOBAL
     * @field OWNERMODE_GAMEGLOBAL
     */
    SETCONSTANT(OWNERMODE_GAMEGLOBAL, SharedMode::ObjectOwnerModes::GameGlobal)

    /**
     * STATE_DISCONNECTED
     * @field STATE_DISCONNECTED
     */
    SETCONSTANT(STATE_DISCONNECTED, PhotonMatchmaking::ConnectionState::Disconnected)
    /**
     * STATE_CONNECTING
     * @field STATE_CONNECTING
     */
    SETCONSTANT(STATE_CONNECTING, PhotonMatchmaking::ConnectionState::Connecting)
    /**
     * STATE_CONNECTED
     * @field STATE_CONNECTED
     */
    SETCONSTANT(STATE_CONNECTED, PhotonMatchmaking::ConnectionState::Connected)
    /**
     * STATE_JOININGROOM
     * @field STATE_JOININGROOM
     */
    SETCONSTANT(STATE_JOININGROOM, PhotonMatchmaking::ConnectionState::JoiningRoom)
    /**
     * STATE_INROOM
     * @field STATE_INROOM
     */
    SETCONSTANT(STATE_INROOM, PhotonMatchmaking::ConnectionState::InRoom)
    /**
     * STATE_LEAVINGROOM
     * @field STATE_LEAVINGROOM
     */
    SETCONSTANT(STATE_LEAVINGROOM, PhotonMatchmaking::ConnectionState::LeavingRoom)
    /**
     * STATE_DISCONNECTING
     * @field STATE_DISCONNECTING
     */
    SETCONSTANT(STATE_DISCONNECTING, PhotonMatchmaking::ConnectionState::Disconnecting)


    /**
     * DISCONNECT_CAUSE_NONE
     * @field DISCONNECT_CAUSE_NONE
     */
    SETCONSTANT(DISCONNECT_CAUSE_NONE, PhotonMatchmaking::DisconnectCause::None)
    /**
     * DISCONNECT_CAUSE_DISCONNECTBYSERVERUSERLIMIT
     * @field DISCONNECT_CAUSE_DISCONNECTBYSERVERUSERLIMIT
     */
    SETCONSTANT(DISCONNECT_CAUSE_DISCONNECTBYSERVERUSERLIMIT, PhotonMatchmaking::DisconnectCause::DisconnectByServerUserLimit)
    /**
     * DISCONNECT_CAUSE_EXCEPTIONONCONNECT
     * @field DISCONNECT_CAUSE_EXCEPTIONONCONNECT
     */
    SETCONSTANT(DISCONNECT_CAUSE_EXCEPTIONONCONNECT, PhotonMatchmaking::DisconnectCause::ExceptionOnConnect)
    /**
     * DISCONNECT_CAUSE_DISCONNECTBYSERVER
     * @field DISCONNECT_CAUSE_DISCONNECTBYSERVER
     */
    SETCONSTANT(DISCONNECT_CAUSE_DISCONNECTBYSERVER, PhotonMatchmaking::DisconnectCause::DisconnectByServer)
    /**
     * DISCONNECT_CAUSE_DISCONNECTBYSERVERLOGIC
     * @field DISCONNECT_CAUSE_DISCONNECTBYSERVERLOGIC
     */
    SETCONSTANT(DISCONNECT_CAUSE_DISCONNECTBYSERVERLOGIC, PhotonMatchmaking::DisconnectCause::DisconnectByServerLogic)
    /**
     * DISCONNECT_CAUSE_TIMEOUTDISCONNECT
     * @field DISCONNECT_CAUSE_TIMEOUTDISCONNECT
     */
    SETCONSTANT(DISCONNECT_CAUSE_TIMEOUTDISCONNECT, PhotonMatchmaking::DisconnectCause::TimeoutDisconnect)
    /**
     * DISCONNECT_CAUSE_EXCEPTION
     * @field DISCONNECT_CAUSE_EXCEPTION
     */
    SETCONSTANT(DISCONNECT_CAUSE_EXCEPTION, PhotonMatchmaking::DisconnectCause::Exception)
    /**
     * DISCONNECT_CAUSE_INVALIDAUTHENTICATION
     * @field DISCONNECT_CAUSE_INVALIDAUTHENTICATION
     */
    SETCONSTANT(DISCONNECT_CAUSE_INVALIDAUTHENTICATION, PhotonMatchmaking::DisconnectCause::InvalidAuthentication)
    /**
     * DISCONNECT_CAUSE_MAXCCUREACHED
     * @field DISCONNECT_CAUSE_MAXCCUREACHED
     */
    SETCONSTANT(DISCONNECT_CAUSE_MAXCCUREACHED, PhotonMatchmaking::DisconnectCause::MaxCCUReached)
    /**
     * DISCONNECT_CAUSE_INVALIDREGION
     * @field DISCONNECT_CAUSE_INVALIDREGION
     */
    SETCONSTANT(DISCONNECT_CAUSE_INVALIDREGION, PhotonMatchmaking::DisconnectCause::InvalidRegion)
    /**
     * DISCONNECT_CAUSE_OPERATIONNOTALLOWEDINCURRENTSTATE
     * @field DISCONNECT_CAUSE_OPERATIONNOTALLOWEDINCURRENTSTATE
     */
    SETCONSTANT(DISCONNECT_CAUSE_OPERATIONNOTALLOWEDINCURRENTSTATE, PhotonMatchmaking::DisconnectCause::OperationNotAllowedInCurrentState)
    /**
     * DISCONNECT_CAUSE_CUSTOMAUTHENTICATIONFAILED
     * @field DISCONNECT_CAUSE_CUSTOMAUTHENTICATIONFAILED
     */
    SETCONSTANT(DISCONNECT_CAUSE_CUSTOMAUTHENTICATIONFAILED, PhotonMatchmaking::DisconnectCause::CustomAuthenticationFailed)
    /**
     * DISCONNECT_CAUSE_CLIENTVERSIONTOOOLD
     * @field DISCONNECT_CAUSE_CLIENTVERSIONTOOOLD
     */
    SETCONSTANT(DISCONNECT_CAUSE_CLIENTVERSIONTOOOLD, PhotonMatchmaking::DisconnectCause::ClientVersionTooOld)
    /**
     * DISCONNECT_CAUSE_CLIENTVERSIONINVALID
     * @field DISCONNECT_CAUSE_CLIENTVERSIONINVALID
     */
    SETCONSTANT(DISCONNECT_CAUSE_CLIENTVERSIONINVALID, PhotonMatchmaking::DisconnectCause::ClientVersionInvalid)
    /**
     * DISCONNECT_CAUSE_DASHBOARDVERSIONINVALID
     * @field DISCONNECT_CAUSE_DASHBOARDVERSIONINVALID
     */
    SETCONSTANT(DISCONNECT_CAUSE_DASHBOARDVERSIONINVALID, PhotonMatchmaking::DisconnectCause::DashboardVersionInvalid)
    /**
     * DISCONNECT_CAUSE_AUTHENTICATIONTICKETEXPIRED
     * @field DISCONNECT_CAUSE_AUTHENTICATIONTICKETEXPIRED
     */
    SETCONSTANT(DISCONNECT_CAUSE_AUTHENTICATIONTICKETEXPIRED, PhotonMatchmaking::DisconnectCause::AuthenticationTicketExpired)
    /**
     * DISCONNECT_CAUSE_DISCONNECTBYOPERATIONLIMIT
     * @field DISCONNECT_CAUSE_DISCONNECTBYOPERATIONLIMIT
     */
    SETCONSTANT(DISCONNECT_CAUSE_DISCONNECTBYOPERATIONLIMIT, PhotonMatchmaking::DisconnectCause::DisconnectByOperationLimit)


    /**
     * EVENT_OBJECT_CREATED
     * @field EVENT_OBJECT_CREATED
     */
    SETCONSTANT(EVENT_OBJECT_CREATED, g_Ctx->m_EventOnObjectCreated)
    /**
     * EVENT_SUB_OBJECT_CREATED
     * @field EVENT_SUB_OBJECT_CREATED
     */
    SETCONSTANT(EVENT_SUB_OBJECT_CREATED, g_Ctx->m_EventOnSubObjectCreated)
    /**
     * EVENT_OBJECT_DESTROYED
     * @field EVENT_OBJECT_DESTROYED
     */
    SETCONSTANT(EVENT_OBJECT_DESTROYED, g_Ctx->m_EventOnObjectDestroyed)
    /**
     * EVENT_SUB_OBJECT_DESTROYED
     * @field EVENT_SUB_OBJECT_DESTROYED
     */
    SETCONSTANT(EVENT_SUB_OBJECT_DESTROYED, g_Ctx->m_EventOnSubObjectDestroyed)
    /**
     * EVENT_OBJECT_OWNER_CHANGED
     * @field EVENT_OBJECT_OWNER_CHANGED
     */
    SETCONSTANT(EVENT_OBJECT_OWNER_CHANGED, g_Ctx->m_EventOnObjectOwnerChanged)
    /**
     * EVENT_OBJECT_PREDICTION_OVERRIDE
     * @field EVENT_OBJECT_PREDICTION_OVERRIDE
     */
    SETCONSTANT(EVENT_OBJECT_PREDICTION_OVERRIDE, g_Ctx->m_EventOnObjectPredictionOverride)
    /**
     * EVENT_LOBBY_STATS
     * @field EVENT_LOBBY_STATS
     */
    SETCONSTANT(EVENT_LOBBY_STATS, g_Ctx->m_EventOnLobbyStats)
    /**
     * EVENT_ROOM_JOINED
     * @field EVENT_ROOM_JOINED
     */
    SETCONSTANT(EVENT_ROOM_JOINED, g_Ctx->m_EventOnRoomJoined)
    /**
     * EVENT_ROOM_LEFT
     * @field EVENT_ROOM_LEFT
     */
    SETCONSTANT(EVENT_ROOM_LEFT, g_Ctx->m_EventOnRoomLeft)
    /**
     * EVENT_RPC
     * @field EVENT_RPC
     */
    SETCONSTANT(EVENT_RPC, g_Ctx->m_EventOnRpc)
    /**
     * EVENT_SCENE_CHANGE
     * @field EVENT_SCENE_CHANGE
     */
    SETCONSTANT(EVENT_SCENE_CHANGE, g_Ctx->m_EventOnSceneChange)
    /**
     * EVENT_DESTROYED_MAP_ACTOR
     * @field EVENT_DESTROYED_MAP_ACTOR
     */
    SETCONSTANT(EVENT_DESTROYED_MAP_ACTOR, g_Ctx->m_EventOnDestroyedMapActor)
    /**
     * EVENT_INTEREST_ENTER
     * @field EVENT_INTEREST_ENTER
     */
    SETCONSTANT(EVENT_INTEREST_ENTER, g_Ctx->m_EventOnInterestEnter)
    /**
     * EVENT_INTEREST_EXIT
     * @field EVENT_INTEREST_EXIT
     */
    SETCONSTANT(EVENT_INTEREST_EXIT, g_Ctx->m_EventOnInterestExit)
    /**
     * EVENT_FORCED_DISCONNECT
     * @field EVENT_FORCED_DISCONNECT
     */
    SETCONSTANT(EVENT_FORCED_DISCONNECT, g_Ctx->m_EventOnForcedDisconnect)
    /**
     * EVENT_FUSION_START
     * @field EVENT_FUSION_START
     */
    SETCONSTANT(EVENT_FUSION_START, g_Ctx->m_EventOnFusionStart)
    /**
     * EVENT_CONNECTED
     * @field EVENT_CONNECTED
     */
    SETCONSTANT(EVENT_CONNECTED, g_Ctx->m_EventOnConnected)
    /**
     * EVENT_DISCONNECTED
     * @field EVENT_DISCONNECTED
     */
    SETCONSTANT(EVENT_DISCONNECTED, g_Ctx->m_EventOnDisconnected)


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
        g_Ctx->m_FusionClient->GetRealtimeClient().Service(true);

        if (g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom())
        {
            Fusion_TickBeforeFrameEnd();
            g_Ctx->m_FusionClient->UpdateFrameEnd();
            g_Ctx->m_FusionClient->UpdateFrameBegin(dt);
            Fusion_TickAfterFrameBegin(dt);
        }



        const PhotonMatchmaking::ConnectionState state = g_Ctx->m_FusionClient->GetRealtimeClient().GetState();
        if (state == PhotonMatchmaking::ConnectionState::Connected)
        {
            if (g_Ctx->m_ConnectionState == PhotonMatchmaking::ConnectionState::Connecting)
            {
                CallListener(g_Ctx->m_EventOnConnected);
            }
        }
        else if (state == PhotonMatchmaking::ConnectionState::Disconnected)
        {
            if (g_Ctx->m_ConnectionState == PhotonMatchmaking::ConnectionState::Disconnecting)
            {
                CallListener(g_Ctx->m_EventOnDisconnected);
            }
        }

        if (g_Ctx->m_FusionClient->GetRealtimeClient().IsInRoom())
        {
            Fusion_TickBeforeFrameEnd();
            g_Ctx->m_FusionClient->UpdateFrameEnd();
            g_Ctx->m_FusionClient->UpdateFrameBegin(dt);
            Fusion_TickAfterFrameBegin(dt);
        }

        g_Ctx->m_ConnectionState = state;
    }

    return dmExtension::RESULT_OK;
}

dmExtension::Result OnEventFusion(dmExtension::Event* event, dmExtension::Params* params)
{
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
