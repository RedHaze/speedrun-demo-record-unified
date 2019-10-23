#pragma once

#include <stdio.h>
#include <time.h>
#include <algorithm>
#include <string>

#include "convar.h"
#include "eiface.h"
#include "engine/IEngineSound.h"
#include "engine/iserverplugin.h"
#include "filesystem.h"
#include "interface.h"
#include "tier2/tier2.h"

#include "tier1/utllinkedlist.h"
#include "utlbuffer.h"

#include "cdll_int.h"

// Utility Macros
#if defined(SSDK2007) || defined(SSDK2013)
#define DemRecMsg(color, msg, ...) (ConColorMsg(color, "[Speedrun] " msg, __VA_ARGS__))
#define DemRecMsgSuccess(msg, ...) (DemRecMsg(Color(0, 255, 0, 255), msg, __VA_ARGS__))
#define DemRecMsgWarning(msg, ...) (DemRecMsg(Color(255, 87, 87, 255), msg, __VA_ARGS__))
#define DemRecMsgInfo(msg, ...) (DemRecMsg(Color(255, 165, 0, 255), msg, __VA_ARGS__))
#else
#define DemRecMsg(color, msg, ...) (Msg(msg, __VA_ARGS__))
#define DemRecMsgSuccess(msg, ...) (Msg(msg, __VA_ARGS__))
#define DemRecMsgWarning(msg, ...) (Msg(msg, __VA_ARGS__))
#define DemRecMsgInfo(msg, ...) (Msg(msg, __VA_ARGS__))
#endif

// Common command size
#define CMD_SIZE 256
#define SESSION_DIR_SIZE 256
#define DEMO_NAME_SIZE 256

#define DEMO_LIST_SIZE 8

//---------------------------------------------------------------------------------
// Purpose: plugin class
//---------------------------------------------------------------------------------
class CSpeedrunDemoRecord : public IServerPluginCallbacks
{
    public:
    CSpeedrunDemoRecord();
    ~CSpeedrunDemoRecord();

    // IServerPluginCallbacks methods
    virtual bool Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory);
    virtual void Unload(void);
    virtual void Pause(void){};
    virtual void UnPause(void){};
    virtual const char* GetPluginDescription(void);
    virtual void LevelInit(char const* pMapName);
    virtual void ServerActivate(edict_t* pEdictList, int edictCount, int clientMax);
    virtual void GameFrame(bool simulating){};
    virtual void LevelShutdown(void);
    virtual void ClientActive(edict_t* pEntity);
    virtual void ClientDisconnect(edict_t* pEntity);
    virtual void ClientPutInServer(edict_t* pEntity, char const* playername);
    virtual void SetCommandClient(int index){};
#if defined(SSDK2006)
    virtual void ClientSettingsChanged(edict_t* pEdict){};
    virtual PLUGIN_RESULT ClientConnect(bool* bAllowConnect,
                                        edict_t* pEntity,
                                        const char* pszName,
                                        const char* pszAddress,
                                        char* reject,
                                        int maxrejectlen);
    virtual PLUGIN_RESULT ClientCommand(edict_t* pEntity)
    {
        return PLUGIN_CONTINUE;
    }
    virtual PLUGIN_RESULT NetworkIDValidated(const char* pszUserName, const char* pszNetworkID)
    {
        return PLUGIN_CONTINUE;
    }
#else
    virtual void ClientSettingsChanged(edict_t* pEdict){};
    virtual PLUGIN_RESULT ClientConnect(bool* bAllowConnect,
                                        edict_t* pEntity,
                                        const char* pszName,
                                        const char* pszAddress,
                                        char* reject,
                                        int maxrejectlen);
    virtual PLUGIN_RESULT ClientCommand(edict_t* pEntity, const CCommand& args)
    {
        return PLUGIN_CONTINUE;
    }
    virtual PLUGIN_RESULT NetworkIDValidated(const char* pszUserName, const char* pszNetworkID)
    {
        return PLUGIN_CONTINUE;
    }
    virtual void OnQueryCvarValueFinished(QueryCvarCookie_t iCookie,
                                          edict_t* pPlayerEntity,
                                          EQueryCvarValueStatus eStatus,
                                          const char* pCvarName,
                                          const char* pCvarValue){};

    // added with version 3 of the interface.
    virtual void OnEdictAllocated(edict_t* edict){};
    virtual void OnEdictFreed(const edict_t* edict){};
#endif
};

enum RecordingMode
{
    DEMREC_DISABLED,

    // standard speedrun (deaths/reloads/etc)
    DEMREC_STANDARD,

    // segmenting mode
    // mainly one map, on map level changes do not stop recording
    DEMREC_SEGMENTED
};

// Interfaces from the engine
// helper functions (messaging clients, loading content, making entities, running commands, etc)
IVEngineServer* engine = NULL;
IVEngineClient* clientEngine = NULL;
IEngineSound* soundEngine = NULL;

//Filesystem for I/O, use this instead of fopen and whatnot
IFileSystem* filesystem = NULL;

// GlobalVars
RecordingMode recordMode = DEMREC_DISABLED;
int retries = 0;
std::string lastMapName;
std::string currentMapName;
char sessionDir[SESSION_DIR_SIZE] = {};
char currentDemoName[DEMO_NAME_SIZE] = {};

// Function protos
void findFirstMap();
void pathExists();
void GetDateAndTime(struct tm& ltime);
void ConvertTimeToLocalTime(const time_t& t, struct tm& ltime);
int demoExists(const char* curMap);
