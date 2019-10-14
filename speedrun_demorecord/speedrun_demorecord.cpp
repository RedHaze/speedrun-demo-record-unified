//===== Copyright © 1996-2008, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
// +Bookmark mode?
// +Fix demo overwrite
// -Timer
// -Check for valid directory ./lol/ or ./ or ./lol/lol2/ NOT! /lol or /lol/ or lol
// +Speedrun resume on game crash, make text file with current map/ retry/ session dir at start of level? what if it crashes on level change?? this will be kinda hard to make.
//  kinda made, wanna incorporate ability to load last save automatically
// $NoKeywords: $
//
//===========================================================================//

#include <stdio.h>
#include <string>
#include <algorithm>
#include <time.h>

#include "interface.h"
#include "filesystem.h"
#include "eiface.h"
#include "engine/iserverplugin.h"
#include "game/server/iplayerinfo.h"
#include "engine/IEngineSound.h"
#include "convar.h"
#include "tier2/tier2.h"

#include "tier1/utllinkedlist.h"
#include "utlbuffer.h"

#include "cdll_int.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define BOOKMARK_SOUND_FILE "ambient/creatures/teddy.wav"
//#define NEW_ENGINE
// MAKE SURE TO CHANGE CDLL_INT.H TO NEW ENGINE VERSION BEFORE COMPILING NEW ENGINE!

// Interfaces from the engine
IVEngineServer	*engine = NULL; // helper functions (messaging clients, loading content, making entities, running commands, etc)
IVEngineClient	*clientEngine = NULL;
IEngineSound	*soundEngine = NULL;
IFileSystem		*filesystem = NULL; //Filesystem for I/O, use this instead of fopen and whatnot
IPlayerInfoManager *playerinfomanager = NULL; // game dll interface to interact with players
CGlobalVars *gpGlobals = NULL;

//GlobalVars
int recordMode;  //Record Modes
//-1 = disabled
// 0 = standard speedrun (deaths/reloads/etc)
// 1 = segmenting mode (mainly one map, on map level changes do not stop recording!)
int retrys;
std::string lastMapName;
char sessionDir[256];
char currentDemoName[256];
CUtlBuffer bookmarkBuffer;
CUtlBuffer resumeBuffer;

//Function Init
void findFirstMap();
void pathExists();
void GetDateAndTime(struct tm &ltime);
void ConvertTimeToLocalTime(const time_t &t, struct tm &ltime);
int demoExists(const char* curMap);

// useful helper func
inline bool FStrEq(const char *sz1, const char *sz2)
{
	return(Q_stricmp(sz1, sz2) == 0);
}
//---------------------------------------------------------------------------------
// Purpose: plugin class
//---------------------------------------------------------------------------------
class CSpeedrunDemoRecord : public IServerPluginCallbacks
{
public:
	CSpeedrunDemoRecord();
	~CSpeedrunDemoRecord();

	// IServerPluginCallbacks methods
	virtual bool			Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory);
	virtual void			Unload(void);
	virtual void			Pause(void);
	virtual void			UnPause(void);
	virtual const char     *GetPluginDescription(void);
	virtual void			LevelInit(char const *pMapName);
	virtual void			ServerActivate(edict_t *pEdictList, int edictCount, int clientMax);
	virtual void			GameFrame(bool simulating);
	virtual void			LevelShutdown(void);
	virtual void			ClientActive(edict_t *pEntity);
	virtual void			ClientDisconnect(edict_t *pEntity);
	virtual void			ClientPutInServer(edict_t *pEntity, char const *playername);
	virtual void			SetCommandClient(int index);
	virtual void			ClientSettingsChanged(edict_t *pEdict);
	virtual PLUGIN_RESULT	ClientConnect(bool *bAllowConnect, edict_t *pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen);
	virtual PLUGIN_RESULT	ClientCommand(edict_t *pEntity, const CCommand &args);
	virtual PLUGIN_RESULT	NetworkIDValidated(const char *pszUserName, const char *pszNetworkID);
	virtual void			OnQueryCvarValueFinished(QueryCvarCookie_t iCookie, edict_t *pPlayerEntity, EQueryCvarValueStatus eStatus, const char *pCvarName, const char *pCvarValue);

	// added with version 3 of the interface.
	virtual void			OnEdictAllocated(edict_t *edict);
	virtual void			OnEdictFreed(const edict_t *edict);

	virtual int GetCommandIndex() { return m_iClientCommandIndex; }
private:
	int m_iClientCommandIndex;

};

//http://hlssmod.net/he_code/game/client/cdll_client_int.cpp??
//---------------------------------------------------------------------------------
// Purpose: place to hold demos!
//---------------------------------------------------------------------------------
static ConVar speedrun_dir("speedrun_dir", "./", FCVAR_ARCHIVE, "Sets the directory for demos to record to.");
static ConVar speedrun_map("speedrun_map", "", FCVAR_ARCHIVE, "Sets the first map in the game which will be started when speedrun_start is executed.");
static ConVar speedrun_save("speedrun_save", "", FCVAR_ARCHIVE, "If empty, speedrun_start will start using map specifiec in speedrun_map. If save is specified, speedrun_start will start using the save instead of a map. If the specified save does not exist, the speedrun will start using specified map. The save specified MUST BE in the SAVE folder!!");

// 
// The plugin is a static singleton that is exported as an interface
//
CSpeedrunDemoRecord g_SpeedrunDemoRecord;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CSpeedrunDemoRecord, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_SpeedrunDemoRecord);

//---------------------------------------------------------------------------------
// Purpose: constructor/destructor
//---------------------------------------------------------------------------------
CSpeedrunDemoRecord::CSpeedrunDemoRecord()
{
	m_iClientCommandIndex = 0;
	recordMode = -1;
	retrys = 0;
	lastMapName = "";
	bookmarkBuffer.SetBufferType(true, true);
	resumeBuffer.SetBufferType(true, true);
}

CSpeedrunDemoRecord::~CSpeedrunDemoRecord()
{
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is loaded, load the interface we need from the engine
//---------------------------------------------------------------------------------
bool CSpeedrunDemoRecord::Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory)
{
	ConnectTier1Libraries(&interfaceFactory, 1);
	ConnectTier2Libraries(&interfaceFactory, 1);

	playerinfomanager = (IPlayerInfoManager *)gameServerFactory(INTERFACEVERSION_PLAYERINFOMANAGER, NULL);
	if (!playerinfomanager)
	{
		Warning("Unable to load playerinfomanager, ignoring\n"); // this isn't fatal, we just won't be able to access specific player data
	}

	engine = (IVEngineServer*)interfaceFactory(INTERFACEVERSION_VENGINESERVER, NULL);
	clientEngine = (IVEngineClient *)interfaceFactory(VENGINE_CLIENT_INTERFACE_VERSION, NULL);
	soundEngine = (IEngineSound*)interfaceFactory(IENGINESOUND_CLIENT_INTERFACE_VERSION, NULL);

	//CLIENT_DLL_INTERFACE_VERSION: 4104 = VClient015
	//								5135 = VClient015
	//							src 2013 = VClient017

	filesystem = (IFileSystem*)interfaceFactory(FILESYSTEM_INTERFACE_VERSION, NULL);

	// get the interfaces we want to use
	if (!(engine && clientEngine && soundEngine && filesystem && g_pFullFileSystem))
	{
		return false; // we require all these interface to function
	}

	if (playerinfomanager)
	{
		gpGlobals = playerinfomanager->GetGlobalVars();
	}

	soundEngine->PrecacheSound(BOOKMARK_SOUND_FILE);

	ConVar_Register(0);

	findFirstMap();

	ConColorMsg(Color(0, 255, 0, 255), "Speedrun_demorecord Loaded\n");

	return true;
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is unloaded (turned off)
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::Unload(void)
{
	ConVar_Unregister();
	DisconnectTier2Libraries();
	DisconnectTier1Libraries();
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is paused (i.e should stop running but isn't unloaded)
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::Pause(void)
{
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is unpaused (i.e should start executing again)
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::UnPause(void)
{
}

//---------------------------------------------------------------------------------
// Purpose: the name of this plugin, returned in "plugin_print" command
//---------------------------------------------------------------------------------
const char *CSpeedrunDemoRecord::GetPluginDescription(void)
{
	return "Speedrun Demo Record, Maxx";
}

//---------------------------------------------------------------------------------
// Purpose: called on level start, store info to txt file
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::LevelInit(char const *pMapName)
{

	/*
	if(recordMode == 0)
	{
	resumeBuffer.Clear();
	resumeBuffer.Printf("%s\r\n", pMapName);
	resumeBuffer.Printf("%s\r\n", sessionDir);
	//resumeBuffer.Printf("%s\r\n", );
	//retrys //not up to date, rely on last demo recorded function?

	//Path to default directory
	char path[256] = {};
	V_snprintf( path, 256, "%sspeedrun_democrecord_resume_info.txt", speedrun_dir.GetString() );

	//Print to file, let use know it was successful and play a silly sound :P
	filesystem->AsyncWrite(path, resumeBuffer.Base(), resumeBuffer.TellPut(), false);
	}*/
}

//---------------------------------------------------------------------------------
// Purpose: called on level start, when the server is ready to accept client connections
//		edictCount is the number of entities in the level, clientMax is the max client count
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::ServerActivate(edict_t *pEdictList, int edictCount, int clientMax)
{

}

//---------------------------------------------------------------------------------
// Purpose: called once per server frame, do recurring work here (like checking for timeouts)
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::GameFrame(bool simulating)
{

}

//---------------------------------------------------------------------------------
// Purpose: called on level end (as the server is shutting down or going to a new map)
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::LevelShutdown(void) // !!!!this can get called multiple times per map change
{
	if (recordMode > -1)
	{
		if (recordMode == 0)
		{
			if (clientEngine->IsPlayingDemo() == false && clientEngine->IsRecordingDemo() == true)
			{
				//totalTicks += clientEngine->GetDemoRecordingTick();
				char command[256] = {};
				V_snprintf(command, 256, "stop");
				clientEngine->ClientCmd(command);
			}
		}
	}
}

//---------------------------------------------------------------------------------
// Purpose: called when a client spawns into a server (i.e as they begin to play)
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::ClientActive(edict_t *pEntity)
{
	//causes portal demos to crash?
}

//---------------------------------------------------------------------------------
// Purpose: called when a client leaves a server (or is timed out)
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::ClientDisconnect(edict_t *pEntity)
{

}

//---------------------------------------------------------------------------------
// Purpose: called on 
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::ClientPutInServer(edict_t *pEntity, char const *playername)
{

}

//---------------------------------------------------------------------------------
// Purpose: called on level start
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::SetCommandClient(int index)
{
	m_iClientCommandIndex = index;
}

//---------------------------------------------------------------------------------
// Purpose: called on level start
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::ClientSettingsChanged(edict_t *pEdict)
{

}

//---------------------------------------------------------------------------------
// Purpose: called when a client joins a server
//---------------------------------------------------------------------------------
PLUGIN_RESULT CSpeedrunDemoRecord::ClientConnect(bool *bAllowConnect, edict_t *pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen)
{
	if (recordMode > -1)
	{
		if (clientEngine->IsPlayingDemo() == false)
		{
			std::string curMap = STRING(gpGlobals->mapname);

			if (curMap.find("background") == -1)
			{
				char command[256] = {}; //Max path length is 32000 but 256 is easier on RAM, I read that somewhere not sure if it's true tho. Might want to increase if people have problems...
				//Q: Why does game crash when I record a demo? A: Strange character/letters in path OR your path exceeds 256 characters, windows max is 320 (wat)!
				if (lastMapName == curMap && recordMode == 0)
				{
					retrys++;
					V_snprintf(currentDemoName, 256, "%s_%d", curMap.c_str(), retrys);
				}
				else
				{
					int storedRetrys;
					if (recordMode == 1)
					{
						storedRetrys = 0;
					}
					else
					{
						storedRetrys = demoExists(curMap.c_str());
					}

					if (storedRetrys != 0)
					{
						retrys = storedRetrys;
						V_snprintf(currentDemoName, 256, "%s_%d", curMap.c_str(), retrys);
					}
					else
					{
						retrys = 0;
						V_snprintf(currentDemoName, 256, "%s", curMap.c_str());
					}

					lastMapName = curMap;

				}
				V_snprintf(command, 256, "record %s%s\n", sessionDir, currentDemoName);
				clientEngine->ClientCmd(command);
			}

		}
	}
	return PLUGIN_CONTINUE;
}

//---------------------------------------------------------------------------------
// Purpose: called when a client types in a command (only a subset of commands however, not CON_COMMAND's)
//---------------------------------------------------------------------------------
PLUGIN_RESULT CSpeedrunDemoRecord::ClientCommand(edict_t *pEntity, const CCommand &args)
{
	return PLUGIN_CONTINUE;
}

//---------------------------------------------------------------------------------
// Purpose: called when a client is authenticated
//---------------------------------------------------------------------------------
PLUGIN_RESULT CSpeedrunDemoRecord::NetworkIDValidated(const char *pszUserName, const char *pszNetworkID)
{
	return PLUGIN_CONTINUE;
}

//---------------------------------------------------------------------------------
// Purpose: called when a cvar value query is finished
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::OnQueryCvarValueFinished(QueryCvarCookie_t iCookie, edict_t *pPlayerEntity, EQueryCvarValueStatus eStatus, const char *pCvarName, const char *pCvarValue)
{

}
void CSpeedrunDemoRecord::OnEdictAllocated(edict_t *edict)
{

}
void CSpeedrunDemoRecord::OnEdictFreed(const edict_t *edict)
{

}

//---------------------------------------------------------------------------------
// Purpose: Custom Functions & Con Commands
//---------------------------------------------------------------------------------

//---------------------------------------------------------------------------------
// Purpose: checks if chosen directory exists, if not attempts to create folder (please dont hate me noobest)
//---------------------------------------------------------------------------------
//1) Search for demos with map name in filename
//2) If none found, return 0
//3) If found, check for an _#, if none return a 1, else if # > 1, add 1 to that number and return #+1
int demoExists(const char* curMap)
{
	FileFindHandle_t findHandle;
	int retrysTmp = 0;

	char path[256];
	V_snprintf(path, 256, "%s%s*", sessionDir, curMap);

	const char *pFilename = filesystem->FindFirstEx(path, "MOD", &findHandle);
	while (pFilename != NULL)
	{
		pFilename = filesystem->FindNext(findHandle);
		retrysTmp++;
	}
	filesystem->FindClose(findHandle);

	return retrysTmp;
}

//---------------------------------------------------------------------------------
// Purpose: checks if chosen directory exists, if not attempts to create folder (please dont hate me noobest)
//---------------------------------------------------------------------------------
void pathExists()
{
	if (!filesystem->IsDirectory(speedrun_dir.GetString()), "MOD")
	{
		char* path = (char*)speedrun_dir.GetString();
		V_FixSlashes(path);
		filesystem->CreateDirHierarchy(path, "DEFAULT_WRITE_PATH");
	}
}

//---------------------------------------------------------------------------------
// Purpose: Tries to find first map in maplist.txt that doesnt have the word "background" in it.
// Should only be ran first time? Returns char array
//---------------------------------------------------------------------------------
void findFirstMap()
{
	//check if it is empty true: stop, false: cont
	FileHandle_t firstMapConfig = filesystem->Open("cfg/chapter1.cfg", "r", "MOD");
	if (firstMapConfig) //check if it exists is there: cont, dne: stop
	{

		int file_len = filesystem->Size(firstMapConfig);
		char* firstMap = new char[file_len + 1];

		filesystem->ReadLine(firstMap, file_len + 1, firstMapConfig);
		firstMap[file_len] = '\0';
		filesystem->Close(firstMapConfig);

		*std::remove(firstMap, firstMap + strlen(firstMap), '\n') = '\0'; //Remove new lines
		*std::remove(firstMap, firstMap + strlen(firstMap), '\r') = '\0'; //Remove returns

		std::string mapName = std::string(firstMap).substr(std::string(firstMap).find("map ") + 4); //get rid of words "map "

		if (FStrEq(speedrun_map.GetString(), "")) //Will this work? Might have to be FRstrEq(speedrun_map.GetString(), NULL) == true
		{
			speedrun_map.SetValue(mapName.c_str());
		}

		delete[] firstMap;
	}
}

//Get date/time: code from SizzlingCalamari's wonderful plugin! https://raw.githubusercontent.com/SizzlingCalamari/sizzlingplugins/master/sizzlingrecord/
void GetDateAndTime(struct tm &ltime)
{
	// get the time as an int64
	time_t t = time(NULL);

	ConvertTimeToLocalTime(t, ltime);
}

void ConvertTimeToLocalTime(const time_t &t, struct tm &ltime)
{
	// convert it to a struct of time values
	ltime = *localtime(&t);

	// normalize the year and month
	ltime.tm_year = ltime.tm_year + 1900;
	ltime.tm_mon = ltime.tm_mon + 1;
}

//---------------------------------------------------------------------------------
// Purpose: Con Commands
//---------------------------------------------------------------------------------
CON_COMMAND(speedrun_start, "starts run")
{
	if (recordMode == 1) //Already recording segments? Throw error
	{
		ConColorMsg(Color(0, 255, 0, 255), "[Speedrun] Please stop all other speedruns with speedrun_stop.\n");
	}
	else
	{
		if (FStrEq(speedrun_map.GetString(), "")) //No map set? Throw error!
		{
			ConColorMsg(Color(255, 87, 87, 255), "[Speedrun] Please stop all other speedruns with speedrun_stop.\n");
		}
		else
		{
			//Let the user know
			ConColorMsg(Color(0, 255, 0, 255), "[Speedrun] Speedrun starting now...\n");

			//Init standard recording mode
			recordMode = 0;
			lastMapName = "";

			//Get current time
			struct tm ltime;
			ConvertTimeToLocalTime(time(NULL), ltime);

			//Parse time
			char tmpDir[32] = {};
			V_snprintf(tmpDir, 32, "%04i.%02i.%02i-%02i.%02i.%02i", ltime.tm_year, ltime.tm_mon, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);

			//Create dir
			V_snprintf(sessionDir, 256, "%s%s\\", speedrun_dir.GetString(), tmpDir);
			V_FixSlashes(sessionDir);
			filesystem->CreateDirHierarchy(sessionDir, "DEFAULT_WRITE_PATH");

			//Store dir in a resume txt file incase of crash
			resumeBuffer.Clear();
			resumeBuffer.Printf("%s", sessionDir);

			//Path to default directory
			char path[256] = {};
			V_snprintf(path, 256, "%sspeedrun_democrecord_resume_info.txt", speedrun_dir.GetString());

			//Print to file, let us know it was successful and play a silly sound :P
			filesystem->AsyncWrite(path, resumeBuffer.Base(), resumeBuffer.TellPut(), false);

			//Check to see if a save is specified in speedrun_save, if not use specified map in speedrun_map
			//Make sure save exisits (only checking in SAVE folder), if none load specified map.
			char tmpSav[32] = {};
			V_snprintf(tmpSav, 32, ".\\SAVE\\%s.sav", speedrun_save.GetString());
			V_FixSlashes(tmpSav);

			char command[256] = {};
			if (filesystem->FileExists(tmpSav, "MOD"))
			{
				//Load save else...
				ConColorMsg(Color(0, 255, 0, 255), "[Speedrun] Loading from save...\n");
				V_snprintf(command, 256, "load %s.sav\n", speedrun_save.GetString());
				clientEngine->ClientCmd(command);
			}
			else
			{
				//Start run
				V_snprintf(command, 256, "map \"%s\"\n", speedrun_map.GetString());
				engine->ServerCommand(command);
			}
		}
	}
}


CON_COMMAND(speedrun_segment, "segmenting mode")
{
	if (recordMode > -1) //Already in standard record mode? Throw error!
	{
		ConColorMsg(Color(0, 255, 0, 255), "[Speedrun] Please stop all other speedruns with speedrun_stop.\n");
	}
	else
	{
		//Let the user know
		ConColorMsg(Color(0, 255, 0, 255), "[Speedrun] Segment demo record activated, please reload/load a map to start recording...\n");

		//Create path if it doesnt exist
		pathExists();

		//Init segment recording mode
		recordMode = 1;
		V_snprintf(sessionDir, 256, "%s", speedrun_dir.GetString());
	}

}

CON_COMMAND(speedrun_resume, "resume a speedrun after a crash")
{
	if (recordMode == -1)
	{
		//Path to default directory
		char path[256] = {};
		V_snprintf(path, 256, "%sspeedrun_democrecord_resume_info.txt", speedrun_dir.GetString());
		FileHandle_t resumeFile = filesystem->Open(path, "r", "MOD");
		if (resumeFile)
		{
			int file_len = filesystem->Size(resumeFile);
			char* contents = new char[file_len + 1];

			filesystem->ReadLine(contents, file_len + 1, resumeFile);
			contents[file_len] = '\0';
			filesystem->Close(resumeFile);
			V_snprintf(sessionDir, 256, "%s", contents);

			//Init standard recording mode
			recordMode = 0;
			lastMapName = "";

			ConColorMsg(Color(0, 255, 0, 255), "[Speedrun] Past speedrun successfully loaded, please load your last save now.\n");
			delete[] contents;
		}
		else
		{
			Warning("Error opening speedrun_democrecord_resume_info.txt, cannot resume speedrun!\n");
		}
	}
	else
	{
		ConColorMsg(Color(0, 255, 0, 255), "[Speedrun] Please stop all other speedruns with speedrun_stop before resuming a speedrun.\n");
	}
}

//Bookmark buffer and other parts of this command inspired by SizzlingCalamari, https://github.com/SizzlingCalamari/
//I wasnt too sure what a buffer was until now, pretty useful :D!!
//clientEngine->GetDemoRecordingTick() only in 5135 :/
#ifdef NEW_ENGINE
CON_COMMAND(speedrun_bookmark, "create a bookmark for those ep0ch moments.")
{
	if (recordMode == -1 || clientEngine->IsRecordingDemo() == false) //You have to be in speedrun and recording demo to do this!
	{
		ConColorMsg(Color(0, 255, 0, 255), "Please start a speedrun and be ingame.\n");
	}
	else
	{
		//Clear buffer and place a return for ease of reading
		bookmarkBuffer.Clear();
		bookmarkBuffer.Printf("\r\n");

		//get local time
		struct tm ltime;
		ConvertTimeToLocalTime(time(NULL), ltime);

		//Print the bookmark file location and tick to buffer
		bookmarkBuffer.Printf("[%04i/%02i/%02i %02i:%02i] demo: %s%s\r\n", ltime.tm_year, ltime.tm_mon, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, sessionDir, currentDemoName);
		bookmarkBuffer.Printf("\t\t   tick: %d\r\n", clientEngine->GetDemoRecordingTick());

		//Path to default directory
		char path[256] = {};
		V_snprintf(path, 256, "%sspeedrun_democrecord_bookmarks.txt", speedrun_dir.GetString());

		//Print to file, let use know it was successful and play a silly sound :P
		filesystem->AsyncAppend(path, bookmarkBuffer.Base(), bookmarkBuffer.TellPut(), false);
		ConColorMsg(Color(255, 165, 0, 255), "[Speedrun] Bookmarked!\n");
		soundEngine->EmitAmbientSound(BOOKMARK_SOUND_FILE, DEFAULT_SOUND_PACKET_VOLUME);

	}
}
#endif

CON_COMMAND(speedrun_stop, "stops run")
{
	if (recordMode == -1)
	{
		ConColorMsg(Color(0, 255, 0, 255), "[Speedrun] No speedrun in progress.\n");
	}
	else
	{
		ConColorMsg(Color(0, 255, 0, 255), "[Speedrun] Speedrun will STOP now...\n");

		//recordMode = -1;
		lastMapName = STRING(gpGlobals->mapname);

		if (clientEngine->IsRecordingDemo() == true)
		{
			char command[256] = {};
			V_snprintf(command, 256, "stop");
			clientEngine->ClientCmd(command);
		}

		if (recordMode == 0)
		{
			//Path to default directory
			char path[256] = {};
			V_snprintf(path, 256, "%sspeedrun_democrecord_resume_info.txt", speedrun_dir.GetString());

			//Delete resume file
			filesystem->RemoveFile(path, "MOD");
		}

		recordMode = -1;

		/*
		totalTicks += clientEngine->GetDemoRecordingTick();

		//Gocnak's code from ghosting is used below
		float time = totalTicks * 0.015;
		int hours = (int)(time / 3600.0f);
		int minutes = (int)(((time / 3600.0f) - hours) * 60.0f);
		int seconds = (int)(((((time / 3600.0f) - hours) * 60.0f) - minutes) * 60.0f);
		int millis = (int)(((((((time / 3600.0f) - hours) * 60.0f) - minutes) * 60.0f) - seconds) * 10000.0f);

		ConColorMsg( Color(0, 255, 0, 255), "[Speedrun] Your total time is: %02d:%02d:%02d:%02d\n", hours, minutes, seconds, millis );
		ConColorMsg( Color(0, 255, 0, 255), "[Speedrun] Ticks: %d\n", totalTicks );
		*/

	}
}


CON_COMMAND(speedrun_version, "prints the version of the empty plugin")
{
	Msg("Version:0.0.5.1\n");
}
//NEW IN 0.0.5.0
//+Fixed speedrun_stop, made it actually stop the speedrun...
//+Ability to load saves instead of load map using speedrun_start

//NEW IN 0.0.5.2
//-Delete incomplete runs.
//-Update notifier

//NEW IN 0.0.5.3
//-Rewrite? One DLL for all versions of source :D
/*
4:48 PM - YaLTeR: Since you can't just do TehAddress = &(iface->Function);
4:48 PM - YaLTeR: You have to do TehAddress = (FunctionTypedef) (*(uintptr_t**)iface)[index]
4:49 PM - maxxuss [AiRe]: wat
4:48 PM - YaLTeR: And if the iface has some different vtable for some reason then rip
4:49 PM - YaLTeR: So I might as well just end up doing it there a proper way
4:49 PM - YaLTeR: But regarding the interfaces that you use properly
4:49 PM - YaLTeR: Like ICvar or IEngineClient or whatnot

4:50 PM - YaLTeR: My solution is as such: get the OE interface headers and rename everything to like IEngineClient-OE
4:50 PM - YaLTeR: THen try to obtain IEngineClient
4:50 PM - YaLTeR: And just use whatever is not null
4:50 PM - YaLTeR: If that's NULL, try to obtain the OE version


*/