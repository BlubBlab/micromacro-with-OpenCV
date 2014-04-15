/******************************************************************************
	Project: 	MicroMacro
	Author: 	SolarStrike Software
	URL:		www.solarstrike.net
	License:	Modified BSD (see license.txt)
******************************************************************************/

#include "wininclude.h"		// Be sure to include this *before* ncurses!
#include <stdio.h>
#include <string.h>
#include <string>
#include <stdlib.h>
#include <queue>
#include <time.h>
#include <iostream>
#include <conio.h>
#include <Shlwapi.h>

#include "macro.h"
#include "ncurses_lua.h"
#include "filesystem.h"
#include "logger.h"
#include "timer.h"
#include "event.h"
#include "strl.h"
#include "rng.h"
#include "version.h"


extern "C"
{
	#include <lua.h>
	#include <lauxlib.h>
	#include <lualib.h>
}

const char *basicTitle = "MicroMacro v%ld.%ld.%ld";

std::string scriptGUIDialog(std::string);
std::string promptForScript();
void splitArgs(std::string cmd, std::vector<std::string> &);
std::string strReplaceAll(std::string, std::string, std::string);
std::string autoExtension(std::string);
double getConfigFloat(lua_State *, const char *, double);
int getConfigInt(lua_State *, const char *, int);
std::string getConfigString(lua_State *, const char *, std::string);
int loadConfig(const char *);
void printStdHead();
void openLog();
void deleteOldLogs(const char *, unsigned int);
DWORD getUserPriv();
std::string getPrivName(DWORD);
std::string getOsName();
void clearCliScreen();
static BOOL WINAPI consoleControlCallback(DWORD);
int modifyPermission(HANDLE, const char *, bool);

int main(int argc, char **argv)
{
	bool running;
	SetConsoleCtrlHandler(consoleControlCallback, true);
	printStdHead(); // Intro text output

	/* We reset our working directory after every script run
		so we need to make sure we hang onto this */
	char origCWD[MAX_PATH+1];
	GetCurrentDirectory(MAX_PATH,(LPTSTR)&origCWD);

	{ // Initiate our macro singleton
		int success;
		success = Macro::instance()->init();
		if( success != MicroMacro::ERR_OK )
		{
			Logger::instance()->add("Failed to initiate Macro singleton; Error code: %d (%s)\n",
				success, getErrorString(success));
			return success;
		}
	}

	/* Set debug privileges on self */
	if( !modifyPermission(GetCurrentProcess(), "SeDebugPrivilege", true) )
		Logger::instance()->add("Warning: Failed to enable SeDebugPrivilege.");


	{	/* Run configs */
		int success = loadConfig(CONFIG_FILENAME);
		if( success != MicroMacro::ERR_OK )
		{
			fprintf(stderr, "Failed loading config file. Err code: %d (%s)\n",
				success, getErrorString(success));
			system("pause");
			return success;
		}
	}

	// Ensure we seed the RNG
	random(0, 1);

	// Open log file. Also creates log directory (if needed)
	openLog();

	{ // Ensure scripts directory exists
		const char *scriptsDir = Macro::instance()->getSettings()->getString(
			CONFVAR_SCRIPT_DIRECTORY, CONFDEFAULT_SCRIPT_DIRECTORY).c_str();
		if( !directoryExists(scriptsDir) )
		{
			SECURITY_ATTRIBUTES attribs;
			attribs.nLength = sizeof(SECURITY_ATTRIBUTES);
			attribs.bInheritHandle = false;
			attribs.lpSecurityDescriptor = NULL;
			CreateDirectory(scriptsDir, &attribs);
		}
	}

	/* Begin main loop */
	running = true;
	while(running)
	{
		// Reset CWD
		SetCurrentDirectory((LPCTSTR)&origCWD);

		#ifdef DEBUG_LSTATE_STACK
		// Warn to make sure we're not screwing up the stack
		if( lua_gettop(Macro::instance()->getEngine()->getLuaState()) != 0 )
			fprintf(stderr, "[WARN]: lua_gettop() is not 0 (zero).\n");
		#endif

		{ /* Reset window title */
			char title[1024];
			slprintf(title, sizeof(title)-1, basicTitle, AutoVersion::MAJOR, AutoVersion::MINOR, AutoVersion::BUILD);
			SendMessage(Macro::instance()->getAppHwnd(), WM_SETTEXT, (WPARAM)0, (LPARAM)title);
		}

		/* Prompt for script */
		std::vector<std::string> args;
		std::string script = promptForScript();
		if( script == "" )
		{ // We need something to run, duh!
			fprintf(stderr, "Error: You didn\'t even give me a script to run, silly!\n\n");
			continue;
		}
		splitArgs(script, args);

		/* Check for commands */
		if( script == "exit" )
		{ // Days over, let's go home.
			running = false;
			break;
		} else if( script == "clear" )
		{
			clearCliScreen();
			continue;
		} else if( args[0] == "exec" )
		{
			if( args.size() <= 1 ) // If they didn't actually give us anything to do...
				continue;

			// Prep the string, then run it
			size_t fpos = script.find_first_of(' ');
			if( fpos == std::string::npos )
				continue;

			std::string cmd = script.substr(fpos+1);
			printf("Execute string: %s\n\n", cmd.c_str());

			// Run the string
			LuaEngine *E = Macro::instance()->getEngine();
			int success = E->loadString(cmd.c_str());
			if( success != MicroMacro::ERR_OK )
			{
				char buffer[1024];
				slprintf(buffer, sizeof(buffer)-1, "String execution error code: %d (%s)\n%s\n",
					success, getErrorString(success), E->getLastErrorMessage().c_str());
				fprintf(stderr, "%s\n", buffer);
				Logger::instance()->add(buffer);
			}

			// Make sure we re-initialize our Lua state before we move on
			E->reinit();
			continue;
		}

		/* If a relative path, pre-pend the scripts directory */
		if( PathIsRelative(script.c_str()) )
		{
			args[0] = Macro::instance()->getSettings()->getString(
				CONFVAR_SCRIPT_DIRECTORY, CONFDEFAULT_SCRIPT_DIRECTORY) + "/" + args[0];
		}

		/* Correct filename if needed */
		args[0] = autoExtension(args[0]);

		/* Change CWD to script's directory */
		SetCurrentDirectory(getFilePath(args[0], false).c_str());

		/* Run script */
		printf("Running \'%s\'\n\n", script.c_str());
		int success = Macro::instance()->getEngine()->loadFile(getFileName(args[0]).c_str());
		if( success != MicroMacro::ERR_OK )
		{
			LuaEngine *E = Macro::instance()->getEngine();

			char buffer[1024];
			slprintf(buffer, sizeof(buffer)-1, "Load file error code: %d (%s)\n%s\n",
				success, getErrorString(success), E->getLastErrorMessage().c_str());
			fprintf(stderr, "%s\n", buffer);
			Logger::instance()->add(buffer);

			E->reinit();
			continue;
		}


		/* Run initialization callback */
		success = Macro::instance()->getEngine()->runInit(&args);
		if( success != MicroMacro::ERR_OK )
		{
			LuaEngine *E = Macro::instance()->getEngine();

			char buffer[1024];
			slprintf(buffer, sizeof(buffer)-1, "Failed to run init function, err code: %d (%s)\n%s\n",
				success, getErrorString(success), E->getLastErrorMessage().c_str());
			fprintf(stderr, "%s\n", buffer);
			Logger::instance()->add(buffer);

			E->reinit();
			continue;
		}

		/* Begin script main loop */
		//Macro::instance()->flushEvents();
		Macro::instance()->getHid()->poll();
		Macro::instance()->pollForegroundWindow();
		TimeType lastTime = getNow();
		TimeType now;
		int runState = success;
		while( runState == MicroMacro::ERR_OK )
		{
			#ifdef DEBUG_LSTATE_STACK
			// Warn to make sure we're not screwing up the stack
			if( lua_gettop(Macro::instance()->getEngine()->getLuaState()) != 0 )
				fprintf(stderr, "[WARN]: lua_gettop() is not 0 (zero).\n");
			#endif

			// Update window focus
			Macro::instance()->pollForegroundWindow();

			// Handle keyboard held queue
			Macro::instance()->getHid()->handleKeyHeldQueue();

			// Handle keyboard input
			Macro::instance()->handleHidInput();

			// Handle hotkeys
			Hid *phid = Macro::instance()->getHid();
			if( (Macro::instance()->getForegroundWindow() == Macro::instance()->getAppHwnd() &&
				phid->pressed('L') && phid->isDown(VK_CONTROL)) // Local CTRL+L
				|| ((phid->pressed('L') && phid->isDown(VK_CONTROL) && phid->isDown(VK_SHIFT) &&
				phid->isDown(VK_MENU))) ) // Global CTRL SHIFT ALT L
			{
				printf("\nScript forcibly terminated.\n");
				break;
			}

			// Handle events
			success = Macro::instance()->handleEvents();
			if( success != MicroMacro::ERR_OK )
			{
				LuaEngine *E = Macro::instance()->getEngine();

				char buffer[1024];
				slprintf(buffer, sizeof(buffer)-1, "Failed to run event function, err code: %d (%s)\n%s\n",
					success, getErrorString(success), E->getLastErrorMessage().c_str());
				fprintf(stderr, "%s\n", buffer);
				Logger::instance()->add(buffer);

				// Pass to event function
				Event e;
				e.type = EVENT_ERROR;
				e.msg = E->getLastErrorMessage();
				E->runEvent(e);

				break;
			}

			// Get delta time
			now = getNow();
			double dt = deltaTime(now, lastTime);

			// Run main callback
			runState = Macro::instance()->getEngine()->runMain(dt);

			if( runState == MicroMacro::ERR_CLOSE )
			{ // Script requested to end
				printf("\nScript requested termination.\n");
				break;
			}
			else if( runState != MicroMacro::ERR_OK )
			{ // An actual error occurred
				LuaEngine *E = Macro::instance()->getEngine();

				char buffer[1024];
				slprintf(buffer, sizeof(buffer)-1, "Error in main loop. Error code %d (%s)\n%s\n",
					runState, getErrorString(runState), E->getLastErrorMessage().c_str());
				fprintf(stderr, "%s\n", buffer);
				Logger::instance()->add(buffer);

				// Pass to event function
				Event e;
				e.type = EVENT_ERROR;
				e.msg = E->getLastErrorMessage();
				E->runEvent(e);

				break;
			}

			lastTime = now; // Record time
			Sleep(1);
		}

		// Shut down Ncurses
		if( Ncurses_lua::is_initialized() )
			Ncurses_lua::cleanup(Macro::instance()->getEngine()->getLuaState());

		/* Do cleanup, reinit */
		Macro::instance()->getEngine()->reinit();
	}
	Logger::instance()->add("All done. Closing down.\n");

	printf("Shutting down; execution finished.\n");
    return 0;
}

std::string scriptGUIDialog(std::string defaultFilename)
{
	const char *filter = "All Files\0*.*\0Lua files\0*.lua\0\0";

	// Make sure to use backslashes!
	defaultFilename = fixSlashes(defaultFilename, SLASHES_TO_WINDOWS);

	// We will use these buffers to store some data later.
	char cwdBuffer[MAX_PATH+1];
	char fileBuffer[MAX_PATH+1];
	char pathBuffer[MAX_PATH+1];

    /* NOTE: The dialog will modify the CWD, so we must restore it when done. */
    GetCurrentDirectory(MAX_PATH,(LPTSTR)&cwdBuffer);

	// Copy some default data into the buffers, prep OFN struct
	strlcpy(fileBuffer, getFileName(defaultFilename).c_str(), MAX_PATH);
	if( ::getFilePath(defaultFilename, false) == "" )
	{ // Assume scripts directory
		std::string buff = cwdBuffer;
		buff += "/";
		buff += Macro::instance()->getSettings()->getString(CONFVAR_SCRIPT_DIRECTORY, CONFDEFAULT_SCRIPT_DIRECTORY);
		strlcpy(pathBuffer, buff.c_str(), MAX_PATH);
	}
	else
		strlcpy(pathBuffer, fixSlashes(::getFilePath(defaultFilename, true), SLASHES_TO_WINDOWS).c_str(), MAX_PATH);

    OPENFILENAME ofn;
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = Macro::instance()->getAppHwnd();
    ofn.hInstance = NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrCustomFilter = NULL;
    ofn.nMaxCustFilter = 0;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = (LPSTR)&fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = pathBuffer;
    ofn.lpstrTitle = NULL;
    ofn.Flags = (DWORD)(OFN_ENABLESIZING);
    ofn.nFileOffset = (WORD)NULL;
    ofn.nFileExtension = (WORD)NULL;
    ofn.lpstrDefExt = NULL;
    ofn.lCustData = (WORD)NULL;
    ofn.lpfnHook = NULL;
    ofn.lpTemplateName = NULL;
    ofn.pvReserved = NULL;
    ofn.dwReserved = (WORD)NULL;
    ofn.FlagsEx = (DWORD)0;

	// Show the dialog
	std::string retval = "";
    int success = GetOpenFileName(&ofn);
    if( success )
    { // User clicked OK
		retval = ofn.lpstrFile;
    }

	// Reset CWD
    SetCurrentDirectory((LPCTSTR)&cwdBuffer);

	/* Attempt to un-fuck message queue
		A Windows bug causes GetOpenFileName to screw up the input queue
		which prevents GetKeyboardState() from functioning properly
	*/
	MSG msg;
	while( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) > 0 )
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return retval;
}

std::string promptForScript()
{
	static std::string previousScript = "";

	// Clear keyboard buffer
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	FlushConsoleInputBuffer(hStdin);

	printf("Please enter the script name to run.\n");
	printf("Type in \'exit\' (without quotes) to exit.\n");
	printf("Script> ");

	std::string fullcmd;
	std::cin.clear();
	getline(std::cin, fullcmd);
	std::cin.clear();

	if( fullcmd == "" )
		fullcmd = scriptGUIDialog(previousScript);

	previousScript = fullcmd; // Remember this.
	return fullcmd;
}

void splitArgs(std::string cmd, std::vector<std::string> &args)
{
	unsigned int startpos = 0;
	unsigned int lastpos = 0;

	// Take quotes, replace sub-spaces
	startpos = cmd.find("\"");
	while( startpos != std::string::npos )
	{
		// Find the next quote
		unsigned int nextpos = cmd.find("\"", startpos+1);
		if( nextpos - startpos > 0 )
		{
			// Grab substring, remove quotes, substitute spaces
			std::string substr = cmd.substr(startpos+1, nextpos - startpos - 1);
			substr = strReplaceAll(substr, " ", "$_SPACE_$");
			// Now pop it back into place.
			cmd.replace(startpos, nextpos - startpos + 1, substr);
			lastpos = nextpos+1;
			startpos = cmd.find("\"", lastpos);
		}
		else
			break;
	}

	// Now split by spaces
	startpos = cmd.find(' ');
	args.push_back( strReplaceAll(cmd.substr(0, startpos), "$_SPACE_$", " ") );

	lastpos = startpos;
	while(startpos != std::string::npos)
	{
		startpos = cmd.substr(lastpos+1).find(' ');
		std::string tmp = cmd.substr(lastpos+1, startpos);
		if( tmp != "" )
		{
			args.push_back( strReplaceAll(tmp, "$_SPACE_$", " ") );
		}
		lastpos += startpos +1;
	}
}

std::string strReplaceAll(std::string instr, std::string search, std::string replace)
{
	// First we need to handle subquotes
	unsigned int i = instr.find(search);
	while( i != std::string::npos )
	{
		instr.replace(i, search.length(), replace);
		i = instr.find(search, i+replace.length());
	}

	return instr;
}

/* If filename is not found and does not have an extension, try adding one.
	If it works, return it. If still not found, return original filename.
*/
std::string autoExtension(std::string filename)
{
	if( fileExists(filename.c_str()) )
		return filename;

	if( fileExists((filename + ".lua").c_str()) )
		return filename + ".lua";

	return filename;
}


/* ===========================================================================
	Config stuff
   =========================================================================*/

double getConfigFloat(lua_State *L, const char *key, double defaultValue)
{
	lua_getglobal(L, key);
	double num = 0;

	if( lua_isnumber(L, -1) )
		num = lua_tonumber(L, -1);
	else
		num = defaultValue;

	lua_pop(L, 1);
	return num;
}

int getConfigInt(lua_State *L, const char *key, int defaultValue)
{
	lua_getglobal(L, key);
	int num = 0;

	if( lua_isnumber(L, -1) )
		num = lua_tointeger(L, -1);
	else
		num = defaultValue;

	lua_pop(L, 1);
	return num;
}

std::string getConfigString(lua_State *L, const char *key, std::string defaultValue)
{
	lua_getglobal(L, key);

	if( lua_isstring(L, -1) )
		return lua_tostring(L, -1);
	else
		return defaultValue;
}

// Load a config file, copy its contents to the settings manager
int loadConfig(const char *filename)
{
	lua_State *lstate = luaL_newstate();
	if( !lstate )
		return MicroMacro::ERR_INIT_FAIL;

	// Load it, run it, check it.
	int retval = MicroMacro::ERR_OK;
	int failstate = luaL_loadfile(lstate, filename);
	if( !failstate )
		failstate = lua_pcall(lstate, 0, 0, 0);

	if( failstate )
	{
		switch(failstate) {
			case LUA_ERRRUN:		retval = MicroMacro::ERR_RUN;		break;
			case LUA_ERRMEM:		retval = MicroMacro::ERR_MEM;		break;
			case LUA_ERRSYNTAX:		retval = MicroMacro::ERR_SYNTAX;	break;
			case LUA_ERRFILE:		retval = MicroMacro::ERR_FILE;		break;
			case LUA_ERRERR:		retval = MicroMacro::ERR_ERR;		break;
			default:				retval = MicroMacro::ERR_UNKNOWN;	break;
		}

		const char *err = lua_tostring(lstate, -1);
		fprintf(stderr, "%s\n", err);
		lua_close(lstate);
		return retval;
	}


	// Copy from the settings file into our settings manager
	double fval;
	std::string szval;
	Settings *psettings = Macro::instance()->getSettings();

	fval = getConfigInt(lstate, CONFVAR_MEMORY_STRING_BUFFER_SIZE, CONFDEFAULT_MEMORY_STRING_BUFFER_SIZE);
	psettings->setInt(CONFVAR_MEMORY_STRING_BUFFER_SIZE, fval);

	fval = getConfigInt(lstate, CONFVAR_LOG_REMOVAL_DAYS, CONFDEFAULT_LOG_REMOVAL_DAYS);
	psettings->setInt(CONFVAR_LOG_REMOVAL_DAYS, fval);

	szval = getConfigString(lstate, CONFVAR_LOG_DIRECTORY, CONFDEFAULT_LOG_DIRECTORY);
	psettings->setString(CONFVAR_LOG_DIRECTORY, szval);

	szval = getConfigString(lstate, CONFVAR_SCRIPT_DIRECTORY, CONFDEFAULT_SCRIPT_DIRECTORY);
	psettings->setString(CONFVAR_SCRIPT_DIRECTORY, szval);

	lua_close(lstate);
	return retval;
}



/* ===========================================================================
	Output stuff
   =========================================================================*/

// Just dump the intro text
void printStdHead()
{
	WORD color = 3; // Green
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO csbi;

	// Get current text settings
	GetConsoleScreenBufferInfo(handle, &csbi);

	// Change settings, output
	SetConsoleTextAttribute(handle, color);
	printf(basicTitle, AutoVersion::MAJOR, AutoVersion::MINOR, AutoVersion::BUILD);
	printf("\nSolarStrike Software\thttp://www.solarstrike.net\n");

	// Revert to original text settings
	color = csbi.wAttributes;
	SetConsoleTextAttribute(handle, color);
}

// Clear the screen, much like the cls command
void clearCliScreen()
{
	HANDLE stdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	COORD coord = {0, 0};
	DWORD count;

	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(stdOut, &csbi);

	FillConsoleOutputCharacter(stdOut, ' ', csbi.dwSize.X * csbi.dwSize.Y,
		coord, &count);

	SetConsoleCursorPosition(stdOut, coord);
}



/* ===========================================================================
	Logging stuff
   =========================================================================*/

/*
	Just like you imagine, it deletes log files older than
	daysToDelete in days that exist in the given path
*/
void deleteOldLogs(const char *path, unsigned int daysToDelete)
{
	std::vector<std::string> files = getDirectory(path, "txt");
	FILETIME ft_now;
	GetSystemTimeAsFileTime(&ft_now);

	for(unsigned int i = 0; i < files.size(); i++)
	{
		// Get timestamp from file
		bool success;
		char fname[MAX_PATH];
		WIN32_FILE_ATTRIBUTE_DATA fad;
		slprintf(fname, sizeof(fname)-1, "%s/%s", path, files.at(i).c_str());
		success = GetFileAttributesEx(fname, GetFileExInfoStandard, &fad);

		// If it fails for some reason, just move on to the next
		if( !success )
			continue;

		/* Note: Do *NOT* mix TimeTypes from getNow() with FILETIMEs
			or you're going to have a bad time.
		*/
		unsigned int seconds = filetimeDelta(&ft_now, &fad.ftLastAccessTime);
		unsigned int days = seconds/(24*60*60);

		if( days >= daysToDelete )
			DeleteFile(fname);
	}
}

// Just opens the log file, dumps some basic info
void openLog()
{
	Settings *psettings = Macro::instance()->getSettings();
	const char *logDir = psettings->getString(CONFVAR_LOG_DIRECTORY).c_str();
	unsigned int logRemovalDays = (unsigned int)psettings->getInt(CONFVAR_LOG_REMOVAL_DAYS);

	// Ensure log directory exists
	if( !directoryExists(logDir) )
	{
		SECURITY_ATTRIBUTES attribs;
		attribs.nLength = sizeof(SECURITY_ATTRIBUTES);
		attribs.bInheritHandle = false;
		attribs.lpSecurityDescriptor = NULL;
		CreateDirectory(logDir, &attribs);
	}

	// Remove old logs, open a new log file
	if( logRemovalDays > 0 )
		deleteOldLogs(logDir, logRemovalDays); // Remove old stuff before creating a new log

	char logfileName[MAX_PATH];
	char szTime[256];
	time_t rawtime;
	struct tm * timeinfo;
	time( &rawtime );
	timeinfo = localtime(&rawtime);

	// Iterate through, increasing ID, to find what we will name our log
	bool nameFound = false;
	unsigned int fileCount = 1;
	while( !nameFound )
	{
		strftime(szTime, sizeof(szTime)-1, "%Y-%m-%d", timeinfo);
		slprintf(logfileName, sizeof(logfileName)-1,
			"%s/%s-%02u.txt", logDir, szTime, fileCount);

		nameFound = !fileExists(logfileName);
		++fileCount;
	}
	Logger::instance()->open(logfileName);
	if( Logger::instance()->is_open() )
		fprintf(stdout, "Logging to %s\n\n", logfileName);
	else
		fprintf(stdout, "Failed to open file for logging.\n\n");



	// Get CPU info
	HKEY hKey;
	char szProcessorSpeed[32];
	char szProcessorName[256];
	int rError = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
		"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
		0, KEY_READ, &hKey);
	if( rError == ERROR_SUCCESS )
	{
		DWORD mhz = _MAX_PATH;
		DWORD buffSize;

		// Name
		buffSize = sizeof(szProcessorName) - 1;
		RegQueryValueEx(hKey, "ProcessorNameString", NULL, NULL, (BYTE *)&szProcessorName, &buffSize);
		slprintf((char*)&szProcessorName, sizeof(szProcessorName)-1, "%s", szProcessorName);

		// Speed
		buffSize = sizeof(mhz);
		RegQueryValueEx(hKey, "~MHz", NULL, NULL, (BYTE *)&mhz, &buffSize);
		slprintf((char*)&szProcessorSpeed, sizeof(szProcessorSpeed)-1, "@%dMHz", (int)mhz);
	}

	// Get Windows & user info
	DWORD userGroup = getUserPriv();
	std::string userGroupName = getPrivName(userGroup);

	// Now print out basic info to it
	char splitLine80[81];
	memset((char*)&splitLine80, '-', 80);
	splitLine80[79] = '\n';
	splitLine80[80] = 0;

	Logger::instance()->add("MicroMacro version %s (%s)\n", AutoVersion::FULLVERSION_STRING, AutoVersion::STATUS);
	Logger::instance()->add("%s %s, %s\n", szProcessorName, szProcessorSpeed, getOsName().c_str());
	Logger::instance()->add("User privilege: %s\n", userGroupName.c_str());
	Logger::instance()->add_raw((char *)&splitLine80);
	Logger::instance()->add_raw("\n\n");
}

// Helper to get the user's privilege level
DWORD getUserPriv()
{
	LPUSER_INFO_1 ui1 = NULL;
	wchar_t username[UNLEN+1];
	DWORD usernameSize = UNLEN + 1;
	NET_API_STATUS status;

	GetUserNameW((WCHAR *)&username, &usernameSize);
	status = NetUserGetInfo(NULL, (WCHAR *)&username, (DWORD)1, (LPBYTE *)&ui1);

	if( ui1 == NULL || status != NERR_Success )
	{ /* If some error occurs, assume guest */
		return USER_PRIV_GUEST;
	}

	return ui1->usri1_priv;
}

// Helper to get a group name of a privilege level
std::string getPrivName(DWORD priv)
{
	switch(priv)
	{
		case USER_PRIV_GUEST:
			return "Guest";
		break;
		case USER_PRIV_USER:
			return "User";
		break;
		case USER_PRIV_ADMIN:
			return "Administrator";
		break;
		default:
			return "Unknown";
		break;
	}
}

// Returns the name of the OS as a string
std::string getOsName()
{
	//std::string servicepacksz = "";
	OSVERSIONINFOEX osvi;

	ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	GetVersionEx( (OSVERSIONINFO*)&osvi);

	// version info from:
	// http://msdn2.microsoft.com/en-us/library/ms724834.aspx
	if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 0) // 4, 0
		return std::string("Windows 95");
	if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 10) // 4, 10
		return std::string("Windows 98");
	if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 90) // 4, 90
		return std::string("Windows ME");
	if ( osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 0 ) // 5, 0
		return std::string("Windows 2000");
	if ( osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 1 ) // 5, 1
		return std::string("Windows XP");
	if ( osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 2 ) // 5, 2
	{
		if( (osvi.wProductType == VER_NT_WORKSTATION) )
			return std::string("Windows XP 64-bit");

		if( osvi.wSuiteMask == 0x00008000 )
			return std::string("Windows Home Server");

		if( GetSystemMetrics(SM_SERVERR2) == 0 )
			return std::string("Windows Server 2003");
		else
			return std::string("Windows Server 2003 R2");
	}
	if ( osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 0 ) // 6, 0
	{
		if( osvi.wProductType == VER_NT_WORKSTATION )
			return std::string("Windows Vista");
		else
			return std::string("Windows Server 2008");
	}
	if ( osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 1 ) // 6, 1
	{
		if( osvi.wProductType == VER_NT_WORKSTATION )
			return std::string("Windows 7");
		else
			return std::string("Windows Server 2008 R2");
	}

	// unknown OS
	char buffer[256];
	slprintf((char*)&buffer, sizeof(buffer)-1, "Unknown Windows OS v%i.%i",
		(unsigned int)osvi.dwMajorVersion, (unsigned int)osvi.dwMinorVersion);
	return std::string(buffer);
}

// Capture important events, such as force shutdown
static BOOL WINAPI consoleControlCallback(DWORD dwCtrlType)
{
	switch(dwCtrlType)
	{
		case CTRL_C_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			/* We pretty much have to call exit(0) here.
				Would be nice to be able to just set mainRunning = false
				and let it exit graciously, but, that's WIN32 for you.
			*/
			// Close. Down. EVERYTHING.
			Macro::instance()->cleanup();
			Logger::instance()->add("Process forcefully terminated (Win32 callback)\n");
			exit(EXIT_SUCCESS);
			return true;
		break;
	}
	return false;
}

/*	Obviously, it changes our permission level for a process.
	This is called to grant SeDebugPrivilege.
*/
int modifyPermission(HANDLE hProcess, const char *PrivName, bool allow)
{
	HANDLE hToken;
	LUID sedebugnameValue;
	TOKEN_PRIVILEGES tkp;

	if ( !OpenProcessToken(hProcess, TOKEN_ADJUST_PRIVILEGES
						   | TOKEN_QUERY,&hToken ) )
	{
		CloseHandle(hToken);
		return false;
	}

	//Don't bother looking up and adjusting privilege if removing rights
	if(allow)
	{
		if ( !LookupPrivilegeValue( NULL, PrivName, &sedebugnameValue ) )
		{
			CloseHandle( hToken );
			return false;
		}

		tkp.PrivilegeCount = 1;
		tkp.Privileges[0].Luid = sedebugnameValue;
		tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	}

	if (AdjustTokenPrivileges( hToken, false, &tkp, sizeof tkp,
							   NULL, NULL ) == 0)
	{
		CloseHandle(hToken);
		return false;
	}

	CloseHandle(hToken);
	return true;
}