//===== Copyright © 1996-2008, Valve Corporation, All rights reserved. ======//
//
// $NoKeywords: $
//
//===========================================================================//

#include "speedrun_demorecord.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgoff.h"

#ifdef SSDK2013
#define BOOKMARK_SOUND_FILE "ambient/creatures/teddy.wav"
#endif

// useful helper func
inline bool FStrEq(const char *sz1, const char *sz2)
{
	return(Q_stricmp(sz1, sz2) == 0);
}

#if defined(SSDK2006)
static ICvar *s_pCVar;

class CPluginConVarAccessor : public IConCommandBaseAccessor
{
public:
	virtual bool	RegisterConCommandBase(ConCommandBase *pCommand)
	{
		pCommand->AddFlags(FCVAR_PLUGIN);

		// Unlink from plugin only list
		pCommand->SetNext(0);

		// Link to engine's list instead
		s_pCVar->RegisterConCommandBase(pCommand);
		return true;
	}

};

CPluginConVarAccessor g_ConVarAccessor;

void InitCVars(CreateInterfaceFn cvarFactory)
{
	s_pCVar = (ICvar*)cvarFactory(VENGINE_CVAR_INTERFACE_VERSION, NULL);
	if (s_pCVar)
	{
		ConCommandBaseMgr::OneTimeInit(&g_ConVarAccessor);
	}
}
#endif

//---------------------------------------------------------------------------------
// Purpose: place to hold demos!
//---------------------------------------------------------------------------------
static ConVar speedrun_dir("speedrun_dir", "./", FCVAR_ARCHIVE | FCVAR_DONTRECORD, "Sets the directory for demos to record to.");
static ConVar speedrun_map("speedrun_map", "", FCVAR_ARCHIVE | FCVAR_DONTRECORD, "Sets the first map in the game which will be started when speedrun_start is executed.");
static ConVar speedrun_save("speedrun_save", "", FCVAR_ARCHIVE | FCVAR_DONTRECORD, "If empty, speedrun_start will start using map specifiec in speedrun_map. If save is specified, speedrun_start will start using the save instead of a map. If the specified save does not exist, the speedrun will start using specified map. The save specified MUST BE in the SAVE folder!!");

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
	recordMode = DEMREC_DISABLED;
	retries = 0;
	lastMapName = "UNKNOWN_MAP";
	currentMapName = "UNKNOWN_MAP";
	resumeBuffer.SetBufferType(true, true);

#ifdef SSDK2013
	bookmarkBuffer.SetBufferType(true, true);
#endif
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

	engine = (IVEngineServer*)interfaceFactory(INTERFACEVERSION_VENGINESERVER, NULL);
	clientEngine = (IVEngineClient *)interfaceFactory(VENGINE_CLIENT_INTERFACE_VERSION, NULL);

	// CLIENT_DLL_INTERFACE_VERSION: 4104 = VClient015
	//								 5135 = VClient015
	//							 src 2013 = VClient017

	filesystem = (IFileSystem*)interfaceFactory(FILESYSTEM_INTERFACE_VERSION, NULL);
	gamedll = (IServerGameDLL*)gameServerFactory(INTERFACEVERSION_SERVERGAMEDLL, NULL);

	// get the interfaces we want to use
	if (!(engine && clientEngine && filesystem && g_pFullFileSystem && gamedll))
	{
		return false; // we require all these interface to function
	}

#ifdef SSDK2013
	soundEngine = (IEngineSound*)interfaceFactory(IENGINESOUND_CLIENT_INTERFACE_VERSION, NULL);
	if (!soundEngine) {
		return false;
	}

	soundEngine->PrecacheSound(BOOKMARK_SOUND_FILE);
#endif

#if defined(SSDK2006)
	// register any cvars we have defined
	InitCVars(interfaceFactory);
#else
	ConVar_Register(0);
#endif

	findFirstMap();

	DemRecMsg(Color(0, 255, 0, 255), "Speedrun_demorecord Loaded\n");

	return true;
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is unloaded (turned off)
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::Unload(void)
{
#if !defined(SSDK2006)
	ConVar_Unregister();
#endif

	DisconnectTier2Libraries();
	DisconnectTier1Libraries();
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
	if (recordMode != DEMREC_DISABLED)
	{
		std::string str(pMapName);
		currentMapName = str;
	}
}

//---------------------------------------------------------------------------------
// Purpose: called on level start, when the server is ready to accept client connections
//		edictCount is the number of entities in the level, clientMax is the max client count
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::ServerActivate(edict_t *pEdictList, int edictCount, int clientMax)
{

}

//---------------------------------------------------------------------------------
// Purpose: called on level end (as the server is shutting down or going to a new map)
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::LevelShutdown(void) // !!!!this can get called multiple times per map change
{
	if (recordMode == DEMREC_STANDARD)
	{
		if (clientEngine->IsPlayingDemo() == false && clientEngine->IsRecordingDemo() == true)
		{
			clientEngine->ClientCmd("stop");
		}
	}

	if (currentDemoIdx != -1) {
		nextDemo();
	}

	Msg("SHUTDOWN\n");
}

//---------------------------------------------------------------------------------
// Purpose: called when a client spawns into a server (i.e as they begin to play)
//---------------------------------------------------------------------------------
void CSpeedrunDemoRecord::ClientActive(edict_t *pEntity)
{
	// causes portal demos to crash?
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
// Purpose: called when a client joins a server
//---------------------------------------------------------------------------------
PLUGIN_RESULT CSpeedrunDemoRecord::ClientConnect(bool *bAllowConnect, edict_t *pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen)
{
	if (recordMode != DEMREC_DISABLED)
	{
		if (clientEngine->IsPlayingDemo() == false)
		{
			std::string& curMap = currentMapName;

			if (curMap.find("background") == -1)
			{
				// Max path length is 32000 but 256 is easier on RAM, I read that somewhere not sure if it's true tho.
				// Might want to increase if people have problems...
				char command[CMD_SIZE] = {};

				// Q: Why does game crash when I record a demo?
				// A: Strange character/letters in path OR your path exceeds 256 characters, windows max is 320.
				if (lastMapName == curMap && recordMode == DEMREC_STANDARD)
				{
					retries++;
					Q_snprintf(currentDemoName, DEMO_NAME_SIZE, "%s_%d", curMap.c_str(), retries);
				}
				else
				{
					int storedretries;
					if (recordMode == DEMREC_SEGMENTED)
					{
						storedretries = 0;
					}
					else
					{
						storedretries = demoExists(curMap.c_str());
					}

					if (storedretries != 0)
					{
						retries = storedretries;
						Q_snprintf(currentDemoName, DEMO_NAME_SIZE, "%s_%d", curMap.c_str(), retries);
					}
					else
					{
						retries = 0;
						Q_snprintf(currentDemoName, DEMO_NAME_SIZE, "%s", curMap.c_str());
					}

					lastMapName = curMap;

				}
				Q_snprintf(command, MAX_PATH, "record %s%s\n", sessionDir, currentDemoName);
				clientEngine->ClientCmd(command);
			}

		}
	}
	return PLUGIN_CONTINUE;
}

//---------------------------------------------------------------------------------
// Purpose: Custom Functions & Con Commands
//---------------------------------------------------------------------------------

//---------------------------------------------------------------------------------
// Purpose: checks if chosen directory exists, if not attempts to create folder (please dont hate me noobest)
//---------------------------------------------------------------------------------
int demoExists(const char* curMap)
{
	// 1) Search for demos with map name in filename
	// 2) If none found, return 0
	// 3) If found, check for an _#, if none return a 1, else if # > 1, add 1 to that number and return #+1

	FileFindHandle_t findHandle;
	int retriesTmp = 0;

	char path[MAX_PATH];
	Q_snprintf(path, MAX_PATH, "%s%s*", sessionDir, curMap);

	const char *pFilename = filesystem->FindFirstEx(path, "MOD", &findHandle);
	while (pFilename != NULL)
	{
		pFilename = filesystem->FindNext(findHandle);
		retriesTmp++;
	}
	filesystem->FindClose(findHandle);

	return retriesTmp;
}

//---------------------------------------------------------------------------------
// Purpose: checks if chosen directory exists, if not attempts to create folder (please dont hate me noobest)
//---------------------------------------------------------------------------------
void pathExists()
{
	if (!filesystem->IsDirectory(speedrun_dir.GetString()), "MOD")
	{
		char* path = (char*)speedrun_dir.GetString();
		Q_FixSlashes(path);
		filesystem->CreateDirHierarchy(path, "DEFAULT_WRITE_PATH");
	}
}

//---------------------------------------------------------------------------------
// Purpose: Tries to find first map in maplist.txt that doesnt have the word "background" in it.
// Should only be ran first time? Returns char array
//---------------------------------------------------------------------------------
void findFirstMap()
{
	// check if it is empty true: stop, false: cont
	FileHandle_t firstMapConfig = filesystem->Open("cfg/chapter1.cfg", "r", "MOD");

	// check if it exists is there: cont, dne: stop
	if (firstMapConfig)
	{

		int file_len = filesystem->Size(firstMapConfig);
		char* firstMap = new char[file_len + 1];

		filesystem->ReadLine(firstMap, file_len + 1, firstMapConfig);
		firstMap[file_len] = '\0';
		filesystem->Close(firstMapConfig);

		*std::remove(firstMap, firstMap + strlen(firstMap), '\n') = '\0'; // Remove new lines
		*std::remove(firstMap, firstMap + strlen(firstMap), '\r') = '\0'; // Remove returns

		std::string mapName = std::string(firstMap).substr(std::string(firstMap).find("map ") + 4); // get rid of words "map "

		if (FStrEq(speedrun_map.GetString(), "")) // Will this work? Might have to be FRstrEq(speedrun_map.GetString(), NULL) == true
		{
			speedrun_map.SetValue(mapName.c_str());
		}

		delete[] firstMap;
	}
}

void nextDemo()
{
	if (currentDemoIdx > numDemos) {
		currentDemoIdx = -1;
		return;
	}

	char command[CMD_SIZE] = {};
	Q_snprintf(command, CMD_SIZE, "playdemo %s\n", demoList[currentDemoIdx]);
	clientEngine->ClientCmd(command);
	currentDemoIdx++;
}

// Get date/time: code from SizzlingCalamari's wonderful plugin!
// https://raw.githubusercontent.com/SizzlingCalamari/sizzlingplugins/master/sizzlingrecord/
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
CON_COMMAND_F(speedrun_start, "starts run", FCVAR_DONTRECORD)
{
	// Already recording segments? Throw error
	if (recordMode == DEMREC_SEGMENTED)
	{
		DemRecMsg(Color(0, 255, 0, 255), "[Speedrun] Please stop segmented recording with speedrun_stop.\n");
	}
	else
	{
		// No map or save set? Throw error
		if (!FStrEq(speedrun_map.GetString(), "") && !FStrEq(speedrun_save.GetString(), ""))
		{
			DemRecMsg(Color(255, 87, 87, 255), "[Speedrun] Please set a map with speedrun_map or save with speedrun_save first.\n");
		}
		else
		{
			// Let the user know
			DemRecMsg(Color(0, 255, 0, 255), "[Speedrun] Speedrun starting now...\n");

			// Init standard recording mode
			recordMode = DEMREC_STANDARD;
			lastMapName = "";

			// Get current time
			struct tm ltime;
			ConvertTimeToLocalTime(time(NULL), ltime);

			// Parse time
			char tmpDir[32] = {};
			Q_snprintf(tmpDir, 32, "%04i.%02i.%02i-%02i.%02i.%02i", ltime.tm_year, ltime.tm_mon, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);

			// Create dir
			Q_snprintf(sessionDir, SESSION_DIR_SIZE, "%s%s\\", speedrun_dir.GetString(), tmpDir);
			Q_FixSlashes(sessionDir);
			filesystem->CreateDirHierarchy(sessionDir, "DEFAULT_WRITE_PATH");

			// Store dir in a resume txt file incase of crash
			resumeBuffer.Clear();
			resumeBuffer.Printf("%s", sessionDir);

			// Path to default directory
			char path[MAX_PATH] = {};
			Q_snprintf(path, MAX_PATH, "%sspeedrun_democrecord_resume_info.txt", speedrun_dir.GetString());

			// Print to file, let us know it was successful and play a silly sound :P
			filesystem->AsyncWrite(path, resumeBuffer.Base(), resumeBuffer.TellPut(), false);

			// Check to see if a save is specified in speedrun_save, if not use specified map in speedrun_map
			// Make sure save exisits (only checking in SAVE folder), if none load specified map.
			char tmpSav[MAX_PATH] = {};
			Q_snprintf(tmpSav, MAX_PATH, ".\\SAVE\\%s.sav", speedrun_save.GetString());
			Q_FixSlashes(tmpSav);

			char command[CMD_SIZE] = {};
			if (filesystem->FileExists(tmpSav, "MOD"))
			{
				// Load save else...
				DemRecMsg(Color(0, 255, 0, 255), "[Speedrun] Loading from save...\n");
				Q_snprintf(command, CMD_SIZE, "load %s.sav\n", speedrun_save.GetString());
				clientEngine->ClientCmd(command);
			}
			else
			{
				// Start run
				Q_snprintf(command, CMD_SIZE, "map \"%s\"\n", speedrun_map.GetString());
				engine->ServerCommand(command);
			}
		}
	}
}


CON_COMMAND_F(speedrun_segment, "segmenting mode", FCVAR_DONTRECORD)
{
	if (recordMode != DEMREC_DISABLED) // Already in standard record mode? Throw error!
	{
		DemRecMsg(Color(0, 255, 0, 255), "[Speedrun] Please stop all other speedruns with speedrun_stop.\n");
	}
	else
	{
		// Let the user know
		DemRecMsg(Color(0, 255, 0, 255), "[Speedrun] Segment demo record activated, please reload/load a map to start recording...\n");

		// Create path if it doesnt exist
		pathExists();

		// Init segment recording mode
		recordMode = DEMREC_SEGMENTED;
		Q_snprintf(sessionDir, SESSION_DIR_SIZE, "%s", speedrun_dir.GetString());
	}

}

CON_COMMAND_F(speedrun_resume, "resume a speedrun after a crash", FCVAR_DONTRECORD)
{
	if (recordMode == DEMREC_DISABLED)
	{
		// Path to default directory
		char path[MAX_PATH] = {};
		Q_snprintf(path, MAX_PATH, "%sspeedrun_democrecord_resume_info.txt", speedrun_dir.GetString());
		FileHandle_t resumeFile = filesystem->Open(path, "r", "MOD");
		if (resumeFile)
		{
			int file_len = filesystem->Size(resumeFile);
			char* contents = new char[file_len + 1];

			filesystem->ReadLine(contents, file_len + 1, resumeFile);
			contents[file_len] = '\0';
			filesystem->Close(resumeFile);
			Q_snprintf(sessionDir, SESSION_DIR_SIZE, "%s", contents);

			// Init standard recording mode
			recordMode = DEMREC_STANDARD;
			lastMapName = "";

			DemRecMsg(Color(0, 255, 0, 255), "[Speedrun] Past speedrun successfully loaded, please load your last save now.\n");
			delete[] contents;
		}
		else
		{
			Warning("Error opening speedrun_democrecord_resume_info.txt, cannot resume speedrun!\n");
		}
	}
	else
	{
		DemRecMsg(Color(0, 255, 0, 255), "[Speedrun] Please stop all other speedruns with speedrun_stop before resuming a speedrun.\n");
	}
}

// Bookmark buffer and other parts of this command inspired by SizzlingCalamari, https://github.com/SizzlingCalamari/
// I wasnt too sure what a buffer was until now, pretty useful :D!!
// clientEngine->GetDemoRecordingTick() only in 5135 :/
#ifdef SSDK2013
CON_COMMAND_F(speedrun_bookmark, "create a bookmark for those ep0ch moments.", FCVAR_DONTRECORD)
{
	// You have to be in speedrun and recording demo to do this!
	if (recordMode == DEMREC_DISABLED || clientEngine->IsRecordingDemo() == false)
	{
		DemRecMsg(Color(0, 255, 0, 255), "Please start a speedrun and be ingame.\n");
	}
	else
	{
		// Clear buffer and place a return for ease of reading
		bookmarkBuffer.Clear();
		bookmarkBuffer.Printf("\r\n");

		// get local time
		struct tm ltime;
		ConvertTimeToLocalTime(time(NULL), ltime);

		// Print the bookmark file location and tick to buffer
		bookmarkBuffer.Printf("[%04i/%02i/%02i %02i:%02i] demo: %s%s\r\n", ltime.tm_year, ltime.tm_mon, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, sessionDir, currentDemoName);
		bookmarkBuffer.Printf("\t\t   tick: %d\r\n", clientEngine->GetDemoRecordingTick());

		// Path to default directory
		char path[MAX_PATH] = {};
		Q_snprintf(path, MAX_PATH, "%sspeedrun_democrecord_bookmarks.txt", speedrun_dir.GetString());

		// Print to file, let use know it was successful and play a silly sound :P
		filesystem->AsyncAppend(path, bookmarkBuffer.Base(), bookmarkBuffer.TellPut(), false);
		DemRecMsg(Color(255, 165, 0, 255), "[Speedrun] Bookmarked!\n");
		soundEngine->EmitAmbientSound(BOOKMARK_SOUND_FILE, DEFAULT_SOUND_PACKET_VOLUME);

	}
}
#endif

CON_COMMAND_F(speedrun_stop, "stops run", FCVAR_DONTRECORD)
{
	if (recordMode == DEMREC_DISABLED)
	{
		DemRecMsg(Color(0, 255, 0, 255), "[Speedrun] No speedrun in progress.\n");
	}
	else
	{
		DemRecMsg(Color(0, 255, 0, 255), "[Speedrun] Speedrun will STOP now...\n");

		lastMapName = currentMapName;

		if (clientEngine->IsRecordingDemo() == true)
		{
			clientEngine->ClientCmd("stop");
		}

		if (recordMode == DEMREC_STANDARD)
		{
			// Path to default directory
			char path[MAX_PATH] = {};
			Q_snprintf(path, MAX_PATH, "%sspeedrun_democrecord_resume_info.txt", speedrun_dir.GetString());

			// Delete resume file
			filesystem->RemoveFile(path, "MOD");
		}

		recordMode = DEMREC_DISABLED;
	}
}

CON_COMMAND_F(speedrun_demo_playback, "playback a list of demos then exit", FCVAR_DONTRECORD)
{
	// Parse in all demos (basically copied from source SDK leak)
	int c = args.ArgC() - 1;
	if (c > DEMO_LIST_SIZE)
	{
		Msg("Max %i demos in demo playback\n", DEMO_LIST_SIZE);
		c = DEMO_LIST_SIZE;
	}

	for (int i = 1; i < c + 1; i++) {
		Q_strncpy(demoList[i - 1], args[i], DEMO_NAME_SIZE);
	}

	currentDemoIdx = 0;

	nextDemo();
}


CON_COMMAND(speedrun_version, "prints the version of the empty plugin")
{
	Msg("Version:0.0.6.0\n");
}
