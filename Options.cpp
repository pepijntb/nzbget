/*
 *  This file if part of nzbget
 *
 *  Copyright (C) 2004  Sven Henkel <sidddy@users.sourceforge.net>
 *  Copyright (C) 2007  Andrei Prygounkov <hugbug@users.sourceforge.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Revision$
 * $Date$
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WIN32
#include "win32.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef WIN32
#include <direct.h>
#else
#include <unistd.h>
#include <pwd.h>
#include <getopt.h>
#endif

#include "nzbget.h"
#include "Util.h"
#include "Options.h"
#include "Log.h"
#include "ServerPool.h"
#include "NewsServer.h"
#include "MessageBase.h"

extern float g_fDownloadRate;
extern ServerPool* g_pServerPool;

#ifdef HAVE_GETOPT_LONG
static struct option long_options[] =
    {
	    {"help", no_argument, 0, 'h'},
	    {"configfile", required_argument, 0, 'c'},
	    {"noconfigfile", no_argument, 0, 'n'},
	    {"printconfig", no_argument, 0, 'p'},
	    {"server", no_argument, 0, 's' },
	    {"daemon", no_argument, 0, 'D' },
	    {"version", no_argument, 0, 'v'},
	    {"option", required_argument, 0, 'o'},
	    {"append", no_argument, 0, 'A'},
	    {"list", no_argument, 0, 'L'},
	    {"pause", no_argument, 0, 'P'},
	    {"unpause", no_argument, 0, 'U'},
	    {"rate", required_argument, 0, 'R'},
	    {"dumpdebug", no_argument, 0, 'B'},
	    {"log", required_argument, 0, 'G'},
	    {"top", no_argument, 0, 'T'},
	    {"edit", required_argument, 0, 'E'},
	    {"fileid", required_argument, 0, 'I'},
	    {"connect", no_argument, 0, 'C'},
	    {"quit", no_argument, 0, 'Q'},
#ifdef DEBUG				
	    {"test", no_argument, 0, 't'},
#endif		
	    {0, 0, 0, 0}
    };
#endif

static char short_options[] = "c:hno:psvABDCG:LPUR:TE:I:Q";

// Program options
static const char* OPTION_DESTDIR			= "destdir";
static const char* OPTION_TEMPDIR			= "tempdir";
static const char* OPTION_QUEUEDIR			= "queuedir";
static const char* OPTION_NZBDIR			= "nzbdir";
static const char* OPTION_CREATELOG			= "createlog";
static const char* OPTION_LOGFILE			= "logfile";
static const char* OPTION_APPENDNZBDIR		= "appendnzbdir";
static const char* OPTION_LOCKFILE			= "lockfile";
static const char* OPTION_DAEMONUSERNAME	= "daemonusername";
static const char* OPTION_OUTPUTMODE		= "outputmode";
static const char* OPTION_DUPECHECK			= "dupecheck";
static const char* OPTION_DOWNLOADRATE		= "downloadrate";
static const char* OPTION_RENAMEBROKEN		= "renamebroken";
static const char* OPTION_SERVERIP			= "serverip";
static const char* OPTION_SERVERPORT		= "serverport";
static const char* OPTION_SERVERPASSWORD	= "serverpassword";
static const char* OPTION_CONNECTIONTIMEOUT	= "connectiontimeout";
static const char* OPTION_SAVEQUEUE			= "savequeue";
static const char* OPTION_RELOADQUEUE		= "reloadqueue";
static const char* OPTION_CREATEBROKENLOG	= "createbrokenlog";
static const char* OPTION_RESETLOG			= "resetlog";
static const char* OPTION_DECODER			= "decoder";
static const char* OPTION_RETRIES			= "retries";
static const char* OPTION_RETRYINTERVAL		= "retryinterval";
static const char* OPTION_TERMINATETIMEOUT	= "terminatetimeout";
static const char* OPTION_CONTINUEPARTIAL	= "continuepartial";
static const char* OPTION_LOGBUFFERSIZE		= "logbuffersize";
static const char* OPTION_INFOTARGET		= "infotarget";
static const char* OPTION_WARNINGTARGET		= "warningtarget";
static const char* OPTION_ERRORTARGET		= "errortarget";
static const char* OPTION_DEBUGTARGET		= "debugtarget";
static const char* OPTION_LOADPARS			= "loadpars";
static const char* OPTION_PARCHECK			= "parcheck";
static const char* OPTION_PARREPAIR			= "parrepair";
static const char* OPTION_POSTPROCESS		= "postprocess";
static const char* OPTION_STRICTPARNAME		= "strictparname";

#ifndef WIN32
const char* PossibleConfigLocations[] =
	{
		"~/.nzbget",
		"/etc/nzbget.conf",
		"/usr/etc/nzbget.conf",
		"/usr/local/etc/nzbget.conf",
		"/opt/etc/nzbget.conf",
		NULL
	};
#endif

Options::Options(int argc, char* argv[])
{
	// initialize options with default values

	m_szConfigFilename		= NULL;
	m_szDestDir				= NULL;
	m_szTempDir				= NULL;
	m_szQueueDir			= NULL;
	m_szNzbDir				= NULL;
	m_eInfoTarget			= mtScreen;
	m_eWarningTarget		= mtScreen;
	m_eErrorTarget			= mtScreen;
	m_eDebugTarget			= mtScreen;
	m_eDecoder				= dcUulib;
	m_bPause				= false;
	m_bCreateBrokenLog		= false;
	m_bResetLog				= false;
	m_fDownloadRate			= 0;
	m_iEditQueueAction		= 0;
	m_iEditQueueIDFrom		= 0;
	m_iEditQueueIDTo		= 0;
	m_iEditQueueOffset		= 0;
	m_szArgFilename			= NULL;
	m_iConnectionTimeout	= 0;
	m_iTerminateTimeout		= 0;
	m_bServerMode			= false;
	m_bDaemonMode			= false;
	m_bRemoteClientMode		= false;
	m_bPrintOptions			= false;
	m_bAddTop				= false;
	m_bAppendNZBDir			= false;
	m_bContinuePartial		= false;
	m_bRenameBroken			= false;
	m_bSaveQueue			= false;
	m_bDupeCheck			= false;
	m_iRetries				= 0;
	m_iRetryInterval		= 0;
	m_szServerPort			= 0;
	m_szServerIP			= NULL;
	m_szServerPassword		= NULL;
	m_szLockFile			= NULL;
	m_szDaemonUserName		= NULL;
	m_eOutputMode			= omLoggable;
	m_bReloadQueue			= false;
	m_iLogBufferSize		= 0;
	m_iLogLines				= 0;
	m_bCreateLog			= false;
	m_szLogFile				= NULL;
	m_eLoadPars				= plAll;
	m_bParCheck				= false;
	m_bParRepair			= false;
	m_bTest					= false;
	m_szPostProcess			= NULL;
	m_bStrictParName		= false;
	m_bNoConfig				= false;

	char szFilename[MAX_PATH + 1];
	strncpy(szFilename, argv[0], MAX_PATH + 1);
	szFilename[MAX_PATH] = '\0';
	NormalizePathSeparators(szFilename);
	char* end = strrchr(szFilename, PATH_SEPARATOR);
	if (end) *end = '\0';
	SetOption("APPDIR", szFilename);
		
	InitDefault();
	InitOptFile(argc, argv);
	InitCommandLine(argc, argv);

	if (m_bPrintOptions)
	{
		Dump();
		exit(-1);
	}

	if (argc == 1)
	{
		PrintUsage(argv[0]);
	}
	if (!m_szConfigFilename && !m_bNoConfig)
	{
		if (argc == 1)
		{
			printf("\n");
		}
		printf("No configuration-file found\n");
#ifdef WIN32
		printf("Please put a configuration-file \"nzbget.conf\" into the directory with exe-file\n");
#else
		printf("Please put a configuration-file in one of the following locations:\n");
		int p = 0;
		while (const char* szFilename = PossibleConfigLocations[p++])
		{
			printf("%s\n", szFilename);
		}
#endif
		exit(-1);
	}
	if (argc == 1)
	{
		exit(-1);
	}

	InitOptions();
	InitFileArg(argc, argv);
	
	InitServers();
	CheckOptions();
}

Options::~Options()
{
	if (m_szConfigFilename)
	{
		free(m_szConfigFilename);
	}
	if (m_szDestDir)
	{
		free(m_szDestDir);
	}
	if (m_szTempDir)
	{
		free(m_szTempDir);
	}
	if (m_szQueueDir)
	{
		free(m_szQueueDir);
	}
	if (m_szNzbDir)
	{
		free(m_szNzbDir);
	}
	if (m_szArgFilename)
	{
		free(m_szArgFilename);
	}
	if (m_szServerIP)
	{
		free(m_szServerIP);
	}
	if (m_szServerPassword)
	{
		free(m_szServerPassword);
	}
	if (m_szLogFile)
	{
		free(m_szLogFile);
	}
	if (m_szLockFile)
	{
		free(m_szLockFile);
	}
	if (m_szDaemonUserName)
	{
		free(m_szDaemonUserName);
	}
	if (m_szPostProcess)
	{
		free(m_szPostProcess);
	}

	for (unsigned int i = 0; i < optEntries.size(); i++)
	{
		free(optEntries[i].name);
		free(optEntries[i].value);
	}
	optEntries.clear();
}

void Options::Dump()
{
	if (m_szConfigFilename)
	{
		printf("Configuration-file: %s\n", m_szConfigFilename);
	}
	for (unsigned int i = 0; i < optEntries.size(); i++)
	{
		printf("%s = \"%s\"\n", optEntries[i].name,	optEntries[i].value);
	}
}

void Options::InitDefault()
{
#ifdef WIN32
	SetOption(OPTION_TEMPDIR, "${APPDIR}\\temp");
	SetOption(OPTION_DESTDIR, "${APPDIR}\\dest");
	SetOption(OPTION_QUEUEDIR, "${APPDIR}\\queue");
	SetOption(OPTION_NZBDIR, "${APPDIR}\\nzb");
	SetOption(OPTION_LOGFILE, "${APPDIR}\\nzbget.log");
	SetOption(OPTION_LOCKFILE, "${APPDIR}\\nzbget.lock");
	SetOption(OPTION_DAEMONUSERNAME, "");
#else
	SetOption(OPTION_TEMPDIR, "~/nzbget/temp");
	SetOption(OPTION_DESTDIR, "~/nzbget/dest");
	SetOption(OPTION_QUEUEDIR, "~/nzbget/queue");
	SetOption(OPTION_NZBDIR, "~/nzbget/nzb");
	SetOption(OPTION_LOGFILE, "~/nzbget/nzbget.log");
	SetOption(OPTION_LOCKFILE, "/tmp/nzbget.lock");
	SetOption(OPTION_DAEMONUSERNAME, "root");
#endif
	SetOption(OPTION_CREATELOG, "yes");
	SetOption(OPTION_APPENDNZBDIR, "yes");
	SetOption(OPTION_OUTPUTMODE, "loggable");
	SetOption(OPTION_DUPECHECK, "yes");
	SetOption(OPTION_DOWNLOADRATE, "0");
	SetOption(OPTION_RENAMEBROKEN, "no");
	SetOption(OPTION_SERVERIP, "localhost");
	SetOption(OPTION_SERVERPASSWORD, "tegbzn");
	SetOption(OPTION_SERVERPORT, "6789");
	SetOption(OPTION_CONNECTIONTIMEOUT, "60");
	SetOption(OPTION_SAVEQUEUE, "yes");
	SetOption(OPTION_RELOADQUEUE, "ask");
	SetOption(OPTION_CREATEBROKENLOG, "no");
	SetOption(OPTION_RESETLOG, "no");
	SetOption(OPTION_DECODER, "yEnc");
	SetOption(OPTION_RETRIES, "5");
	SetOption(OPTION_RETRYINTERVAL, "10");
	SetOption(OPTION_TERMINATETIMEOUT, "600");
	SetOption(OPTION_CONTINUEPARTIAL, "no");
	SetOption(OPTION_LOGBUFFERSIZE, "1000");
	SetOption(OPTION_INFOTARGET, "both");
	SetOption(OPTION_WARNINGTARGET, "both");
	SetOption(OPTION_ERRORTARGET, "both");
	SetOption(OPTION_DEBUGTARGET, "none");
	SetOption(OPTION_LOADPARS, "all");
	SetOption(OPTION_PARCHECK, "no");
	SetOption(OPTION_PARREPAIR, "no");	
	SetOption(OPTION_POSTPROCESS, "");
	SetOption(OPTION_STRICTPARNAME, "yes");
}

void Options::InitOptFile(int argc, char* argv[])
{
	while (true)
	{
		int c;

#ifdef HAVE_GETOPT_LONG
		int option_index  = 0;
		c = getopt_long(argc, argv, short_options, long_options, &option_index);
#else
		c = getopt(argc, argv, short_options);
#endif
				
		if (c == -1) break;

		switch (c)
		{
			case 'c':
				m_szConfigFilename = strdup(optarg);
				break;
			case 'n':
				m_szConfigFilename = NULL;
				m_bNoConfig = true;
				return;
			case '?':
				exit(-1);
				break;
		}
	}

	if (!m_szConfigFilename)
	{
		// search for config file in default locations
#ifdef WIN32
		char szFilename[MAX_PATH + 1];
		strncpy(szFilename, argv[0], MAX_PATH + 1);
		szFilename[MAX_PATH] = '\0';
		NormalizePathSeparators(szFilename);
		char* end = strrchr(szFilename, PATH_SEPARATOR);
		if (end) end[1] = '\0';
		strcat(szFilename, "nzbget.conf");

		struct stat buffer;
		if (!stat(szFilename, &buffer) && S_ISREG(buffer.st_mode))
		{
			m_szConfigFilename = strdup(szFilename);
		}
#else
		int p = 0;
		while (const char* szFilename = PossibleConfigLocations[p++])
		{
			// substitute HOME-variable using SetOption
			SetOption("$CONFIGFILENAME", szFilename);
			szFilename = GetOption("$CONFIGFILENAME");

			struct stat buffer;
			if (!stat(szFilename, &buffer) && S_ISREG(buffer.st_mode))
			{
				m_szConfigFilename = strdup(szFilename);
				DelOption("$CONFIGFILENAME");
				break;
			}
			DelOption("$CONFIGFILENAME");
		}
#endif
	}

	if (m_szConfigFilename)
	{
		LoadConfig(m_szConfigFilename);
	}
}

void Options::CheckDir(char** dir, const char* szOptionName)
{
	char* usedir = NULL;
	const char* tempdir = GetOption(szOptionName);
	if (tempdir && strlen(tempdir) > 0)
	{
		int len = strlen(tempdir);
		usedir = (char*) malloc(len + 2);
		strcpy(usedir, tempdir);
		char ch = usedir[len-1];
		if (ch == ALT_PATH_SEPARATOR)
		{
			usedir[len-1] = PATH_SEPARATOR;
		}
		else if (ch != PATH_SEPARATOR)
		{
			usedir[len] = PATH_SEPARATOR;
			usedir[len + 1] = '\0';
		}
		NormalizePathSeparators(usedir);
	}
	else
	{
		abort("FATAL ERROR: Wrong value for option \"%s\"\n", szOptionName);
	}
	// Ensure the dir is created
	mkdir(usedir, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	*dir = usedir;
}

void Options::InitOptions()
{
	CheckDir(&m_szDestDir, OPTION_DESTDIR);
	CheckDir(&m_szTempDir, OPTION_TEMPDIR);
	CheckDir(&m_szQueueDir, OPTION_QUEUEDIR);
	m_szNzbDir = strdup(GetOption(OPTION_NZBDIR));
	m_szPostProcess = strdup(GetOption(OPTION_POSTPROCESS));
	
	m_fDownloadRate			= (float)atof(GetOption(OPTION_DOWNLOADRATE));
	m_iConnectionTimeout	= atoi(GetOption(OPTION_CONNECTIONTIMEOUT));
	m_iTerminateTimeout		= atoi(GetOption(OPTION_TERMINATETIMEOUT));
	m_iRetries				= atoi(GetOption(OPTION_RETRIES));
	m_iRetryInterval		= atoi(GetOption(OPTION_RETRYINTERVAL));
	m_szServerPort			= atoi(GetOption(OPTION_SERVERPORT));
	m_szServerIP			= strdup(GetOption(OPTION_SERVERIP));
	m_szServerPassword		= strdup(GetOption(OPTION_SERVERPASSWORD));
	m_szLockFile			= strdup(GetOption(OPTION_LOCKFILE));
	m_szDaemonUserName		= strdup(GetOption(OPTION_DAEMONUSERNAME));
	m_iLogBufferSize		= atoi(GetOption(OPTION_LOGBUFFERSIZE));
	m_szLogFile				= strdup(GetOption(OPTION_LOGFILE));
	
	const char* BoolNames[] = { "yes", "no", "true", "false", "1", "0", "on", "off", "enable", "disable" };
	const int BoolValues[] = { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 };
	const int BoolCount = 10;
	m_bCreateBrokenLog		= (bool)ParseOptionValue(OPTION_CREATEBROKENLOG, BoolCount, BoolNames, (const int*)BoolValues);
	m_bResetLog				= (bool)ParseOptionValue(OPTION_RESETLOG, BoolCount, BoolNames, (const int*)BoolValues);
	m_bAppendNZBDir			= (bool)ParseOptionValue(OPTION_APPENDNZBDIR, BoolCount, BoolNames, (const int*)BoolValues);
	m_bContinuePartial		= (bool)ParseOptionValue(OPTION_CONTINUEPARTIAL, BoolCount, BoolNames, (const int*)BoolValues);
	m_bRenameBroken			= (bool)ParseOptionValue(OPTION_RENAMEBROKEN, BoolCount, BoolNames, (const int*)BoolValues);
	m_bSaveQueue			= (bool)ParseOptionValue(OPTION_SAVEQUEUE, BoolCount, BoolNames, (const int*)BoolValues);
	m_bDupeCheck			= (bool)ParseOptionValue(OPTION_DUPECHECK, BoolCount, BoolNames, (const int*)BoolValues);
	m_bCreateLog			= (bool)ParseOptionValue(OPTION_CREATELOG, BoolCount, BoolNames, (const int*)BoolValues);
	m_bParCheck				= (bool)ParseOptionValue(OPTION_PARCHECK, BoolCount, BoolNames, (const int*)BoolValues);
	m_bParRepair			= (bool)ParseOptionValue(OPTION_PARREPAIR, BoolCount, BoolNames, (const int*)BoolValues);
	m_bStrictParName		= (bool)ParseOptionValue(OPTION_STRICTPARNAME, BoolCount, BoolNames, (const int*)BoolValues);
	m_bReloadQueue			= (bool)ParseOptionValue(OPTION_RELOADQUEUE, BoolCount, BoolNames, (const int*)BoolValues);

	const char* OutputModeNames[] = { "loggable", "logable", "log", "colored", "color", "ncurses", "curses" };
	const int OutputModeValues[] = { omLoggable, omLoggable, omLoggable, omColored, omColored, omNCurses, omNCurses };
	const int OutputModeCount = 7;
	m_eOutputMode = (EOutputMode)ParseOptionValue(OPTION_OUTPUTMODE, OutputModeCount, OutputModeNames, OutputModeValues);

	const char* DecoderNames[] = { "uulib", "yenc", "none", "ydec", "ydecoder" };
	const int DecoderValues[] = { dcUulib, dcYenc, dcNone, dcYenc, dcYenc };
	const int DecoderCount = 5;
	m_eDecoder = (EDecoder)ParseOptionValue(OPTION_DECODER, DecoderCount, DecoderNames, DecoderValues);
	
	const char* LoadParsNames[] = { "none", "one", "all", "1", "0" };
	const int LoadParsValues[] = { plNone, plOne, plAll, plOne, plNone };
	const int LoadParsCount = 4;
	m_eLoadPars = (ELoadPars)ParseOptionValue(OPTION_LOADPARS, LoadParsCount, LoadParsNames, LoadParsValues);

	const char* TargetNames[] = { "screen", "log", "both", "none" };
	const int TargetValues[] = { mtScreen, mtLog, mtBoth, mtNone };
	const int TargetCount = 4;
	m_eInfoTarget = (EMessageTarget)ParseOptionValue(OPTION_INFOTARGET, TargetCount, TargetNames, TargetValues);
	m_eWarningTarget = (EMessageTarget)ParseOptionValue(OPTION_WARNINGTARGET, TargetCount, TargetNames, TargetValues);
	m_eErrorTarget = (EMessageTarget)ParseOptionValue(OPTION_ERRORTARGET, TargetCount, TargetNames, TargetValues);
	m_eDebugTarget = (EMessageTarget)ParseOptionValue(OPTION_DEBUGTARGET, TargetCount, TargetNames, TargetValues);
}

int Options::ParseOptionValue(const char * OptName, int argc, const char * argn[], const int argv[])
{
	const char* v = GetOption(OptName);
	if (!v)
	{
		abort("FATAL ERROR: Undefined value for option \"%s\"\n", OptName);
	}

	for (int i = 0; i < argc; i++)
	{
		if (!strcasecmp(v, argn[i]))
		{
			return argv[i];
		}
	}
	
	abort("FATAL ERROR: Wrong value \"%s\" for option \"%s\"\n", v, OptName);
	return -1;
}

void Options::InitCommandLine(int argc, char* argv[])
{
	m_eClientOperation = opClientNoOperation; // default

	// reset getopt
	optind = 1;

	while (true)
	{
		int c;

#ifdef HAVE_GETOPT_LONG
		int option_index  = 0;
		c = getopt_long(argc, argv, short_options, long_options, &option_index);
#else
		c = getopt(argc, argv, short_options);
#endif

		if (c == -1) break;

		switch (c)
		{
			case 'h':
				PrintUsage(argv[0]);
				exit(0);
				break;
			case 'v':
				printf("nzbget version: %s\n", VERSION);
				exit(1);
				break;
			case 'p':
				m_bPrintOptions = true;
				break;
			case 'o':
				if (!SetOptionString(optarg))
				{
					abort("FATAL ERROR: could not set option: %s\n", optarg);
				}
				break;
			case 's':
				m_bServerMode = true;
				break;
			case 'D':
				m_bServerMode = true;
				m_bDaemonMode = true;
				break;
			case 'A':
				m_eClientOperation = opClientRequestDownload;
				break;
			case 'L':
				m_eClientOperation = opClientRequestList;
				break;
			case 'P':
				m_eClientOperation = opClientRequestPause;
				break;
			case 'U':
				m_eClientOperation = opClientRequestUnpause;
				break;
			case 'R':
				m_eClientOperation = opClientRequestSetRate;
				m_fSetRate = (float)atof(optarg);
				break;
			case 'B':
				m_eClientOperation = opClientRequestDumpDebug;
				break;
			case 'G':
				m_eClientOperation = opClientRequestLog;
				m_iLogLines = atoi(optarg);
				if (m_iLogLines == 0)
				{
					abort("FATAL ERROR: Could not parse value of option 'G'\n");
				}
				break;
			case 'T':
				m_bAddTop = true;
				break;
			case 'C':
				m_bRemoteClientMode = true;
				break;
			case 'E':
			{
				m_eClientOperation = opClientRequestEditQueue;
				if (!strcasecmp(optarg, "T"))
				{
					m_iEditQueueAction = NZBMessageRequest::eActionMoveTop;
				}
				else if (!strcasecmp(optarg, "B"))
				{
					m_iEditQueueAction = NZBMessageRequest::eActionMoveBottom;
				}
				else if (!strcasecmp(optarg, "P"))
				{
					m_iEditQueueAction = NZBMessageRequest::eActionPause;
				}
				else if (!strcasecmp(optarg, "U"))
				{
					m_iEditQueueAction = NZBMessageRequest::eActionResume;
				}
				else if (!strcasecmp(optarg, "D"))
				{
					m_iEditQueueAction = NZBMessageRequest::eActionDelete;
				}
				else
				{
					m_iEditQueueOffset = atoi(optarg);
					if (m_iEditQueueOffset == 0)
					{
						abort("FATAL ERROR: Could not parse value of option 'E'\n");
					}
					m_iEditQueueAction = NZBMessageRequest::eActionMoveOffset;
				}
				break;
			}
			case 'I':
			{
				const char* p = strchr(optarg, '-');
				if (p)
				{
					char buf[101];
					int maxlen = p - optarg < 100 ? p - optarg : 100;
					strncpy(buf, optarg, maxlen);
					buf[maxlen] = '\0';
					m_iEditQueueIDFrom = atoi(buf);
					m_iEditQueueIDTo = atoi(p + 1);
					if (m_iEditQueueIDFrom <= 0 || m_iEditQueueIDTo <= 0 ||
					        m_iEditQueueIDFrom > m_iEditQueueIDTo)
					{
						abort("FATAL ERROR: wrong value for option 'I'\n");
					}
				}
				else
				{
					m_iEditQueueIDFrom = atoi(optarg);
					if (m_iEditQueueIDFrom <= 0)
					{
						abort("FATAL ERROR: wrong value for option 'I'\n");
					}
					m_iEditQueueIDTo = m_iEditQueueIDFrom;
				}
				break;
			}
			case 'Q':
				m_eClientOperation = opClientRequestShutdown;
				break;
#ifdef DEBUG				
			case 't':
				m_bTest = true;
				break;
#endif				
			case '?':
				exit(-1);
				break;
		}
	}
}

void Options::PrintUsage(char* com)
{
	printf("Usage: %s [switches] [<nzb-file>]\n"
	       "Switches:\n"
	       "  -h, --help                Print this help-message\n"
	       "  -v, --version             Print version and exit\n"
	       "  -c, --configfile <file>   Filename of configuration-file\n"
	       "  -n, --noconfigfile        Prevent loading of configuration-file\n"
	       "                            (required options must be passed with --option)\n"
	       "  -p, --printconfig         Print configuration and exit\n"
	       "  -o, --option <name=value> Set or override option in configuration-file\n"
	       "  -s, --server              Start nzbget as a server in console-mode\n"
	       "  -D, --daemon              Start nzbget as a server in daemon-mode\n"
	       "  -Q, --quit                Shutdown the server\n"
	       "  -A, --append              Send file to the server's download queue\n"
	       "  -C, --connect             Attach client to server\n"
	       "  -L, --list                Request list of downloads from the server\n"
	       "  -P, --pause               Pause downloading on the server\n"
	       "  -U, --unpause             Unpause downloading on the server\n"
	       "  -R, --rate                Set the download rate on the server\n"
	       "  -T, --top                 Add file to the top (begining) of queue\n"
	       "                            (should be used with switch --append)\n"
	       "  -G, --log <lines>         Request last <lines> lines from server's screen-log\n"
	       "  -E, --edit <action>       Edit queue on the server\n"
	       "                            (must be used with switch --fileid):\n"
	       "    where <action> is one of:\n"
	       "      <+offset|-offset>     Move file(s) in queue relative to current position\n"
	       "                            offset is an integer number\n"
	       "      T                     Move file(s) to the top of queue\n"
	       "      B                     Move file(s) to the bottom of queue\n"
	       "      P                     Pause file(s)\n"
	       "      U                     Resume (unpause) file(s)\n"
	       "      D                     Delete file(s)\n"
	       "  -I, --fileid <FileID|FileIDFrom-FileIDTo>   File-id(s) for switch '-E',\n"
	       "                            as printed by switch --list\n"
	       "",
	       com);
}

void Options::InitFileArg(int argc, char* argv[])
{
	if (optind >= argc)
	{
		// no nzb-file passed
		if (!m_bServerMode && !m_bRemoteClientMode &&
		        (m_eClientOperation == opClientNoOperation ||
		         m_eClientOperation == opClientRequestDownload))
		{
			printf("nzb-file not specified\n");
			exit(-1);
		}
	}
	else
	{
		// Check if the file-name is a relative path or an absolute path
		// If the path starts with '/' its an absolute, else relative
		const char* szFileName = argv[optind];

#ifdef WIN32
			m_szArgFilename = strdup(szFileName);
#else
		if (szFileName[0] == '/')
		{
			m_szArgFilename = strdup(szFileName);
		}
		else
		{
			// TEST
			char szFileNameWithPath[1024];
			getcwd(szFileNameWithPath, 1024);
			strcat(szFileNameWithPath, "/");
			strcat(szFileNameWithPath, szFileName);
			m_szArgFilename = strdup(szFileNameWithPath);
		}
#endif

		if (m_bServerMode || m_bRemoteClientMode ||
		        !((m_eClientOperation == opClientRequestDownload)
		          || (m_eClientOperation == opClientNoOperation)))
		{
			printf("nzb-file not needed for this command\n");
			exit(-1);
		}
	}
}

void Options::SetOption(const char* optname, const char* value)
{
	if (GetOption(optname) != NULL)
	{
		DelOption(optname);
	}

	OptEntry newo;
	newo.name = strdup(optname);
	char* curvalue = NULL;

#ifndef WIN32
	if ((value) && (value[0] == '~') && (value[1] == '/'))
	{
		// expand home-dir

		char* home = getenv("HOME");
		if (!home)
		{
			struct passwd *pw = getpwuid(getuid());
			if (pw)
				home = pw->pw_dir;
		}

		if (!home)
		{
			abort("FATAL ERROR: Unable to determine home-directory, option \"%s\"\n", optname);
		}

		char* newvalue = (char*) malloc(strlen(value) + strlen(home) + 10);

		if (home[strlen(home)-1] == '/')
		{
			sprintf(newvalue, "%s%s", home, value + 2);
		}
		else
		{
			sprintf(newvalue, "%s/%s", home, value + 2);
		}

		curvalue = newvalue;
	}
	else
#endif
	{
		curvalue = strdup(value);
	}

	// expand variables
	while (char* dollar = strstr(curvalue, "${"))
	{
		char* end = strchr(dollar, '}');
		if (end)
		{
			int varlen = (int)(end - dollar - 2);
			char variable[101];
			int maxlen = varlen < 100 ? varlen : 100;
			strncpy(variable, dollar + 2, maxlen);
			variable[maxlen] = '\0';
			const char* varvalue = GetOption(variable);
			if (varvalue)
			{
				int newlen = strlen(varvalue);
				char* newvalue = (char*)malloc(strlen(curvalue) - varlen - 3 + newlen + 1);
				strncpy(newvalue, curvalue, dollar - curvalue);
				strncpy(newvalue + (dollar - curvalue), varvalue, newlen);
				strcpy(newvalue + (dollar - curvalue) + newlen, end + 1);
				free(curvalue);
				curvalue = newvalue;
			}
			else
			{
				abort("FATAL ERROR: Variable \"%s\" not found, option \"%s\"\n", variable, optname);
			}
		}
		else
		{
			abort("FATAL ERROR: Syntax error in variable-substitution, option \"%s=%s\"\n", optname, curvalue);
		}
	}

	newo.value = curvalue;

	optEntries.push_back(newo);
}

void Options::DelOption(const char* optname)
{
	for (unsigned int i = 0; i < optEntries.size(); i++)
	{
		if (!strcasecmp(optEntries[i].name, optname))
		{
			free(optEntries[i].name);
			free(optEntries[i].value);
			optEntries.erase(optEntries.begin() + i);
			return;
		}
	}
}

const char* Options::GetOption(const char* optname)
{
	if (!optname)
		return NULL;

	for (unsigned int i = 0; i < optEntries.size(); i++)
	{
		if (!strcasecmp(optEntries[i].name, optname))
		{
			return optEntries[i].value;
		}
	}
	return NULL;
}

void Options::InitServers()
{
	int n = 1;
	while (true)
	{
		char optname[128];

		sprintf(optname, "server%i.level", n);
		const char* nlevel = GetOption(optname);

		sprintf(optname, "server%i.host", n);
		const char* nhost = GetOption(optname);

		sprintf(optname, "server%i.port", n);
		const char* nport = GetOption(optname);

		sprintf(optname, "server%i.username", n);
		const char* nusername = GetOption(optname);

		sprintf(optname, "server%i.password", n);
		const char* npassword = GetOption(optname);

		sprintf(optname, "server%i.connections", n);
		const char* nconnections = GetOption(optname);

		bool definition = nlevel || nhost || nport || nusername || npassword || nconnections;
		bool completed = nlevel && nhost && nport && nusername && npassword && nconnections;

		if (!definition)
		{
			break;
		}

		if (definition && !completed)
		{
			abort("FATAL ERROR: Server definition not complete\n");
		}

		NewsServer* pNewsServer = new NewsServer(nhost, atoi(nport), nusername, npassword, atoi((char*)nconnections), atoi((char*)nlevel));
		g_pServerPool->AddServer(pNewsServer);

		n++;
	}

	g_pServerPool->SetTimeout(GetConnectionTimeout());
	g_pServerPool->InitConnections();
}

void Options::LoadConfig(const char * configfile)
{
	FILE* infile = fopen(configfile, "r");

	if (!infile)
	{
		abort("FATAL ERROR: Could not open file %s\n", configfile);
	}

	int Errors = 0;
	int Line = 0;
	char buf[1024];
	while (fgets(buf, sizeof(buf) - 1, infile))
	{
		Line++;

		if (buf[0] != 0)
		{
			buf[strlen(buf)-1] = 0; // remove traling '\n'
		}

		if (buf[0] == 0 || buf[0] == '#' || strspn(buf, " ") == strlen(buf))
		{
			continue;
		}

		if (!SetOptionString(buf))
		{
			printf("Error in config-file: line %i: %s\n", Line, buf);
			Errors++;
		}
	}

	fclose(infile);

	if (Errors)
	{
		abort("FATAL ERROR: %i Error(s) in config-file detected\n", Errors);
	}
}

bool Options::SetOptionString(const char * option)
{
	const char* eq = strchr(option, '=');
	if (eq)
	{
		char optname[1001];
		char optvalue[1001];
		int maxlen = eq - option < 1000 ? eq - option : 1000;
		strncpy(optname, option, maxlen);
		optname[maxlen] = '\0';
		strncpy(optvalue, eq + 1, 1000);
		optvalue[1000]  = '\0';
		if (strlen(optname) > 0)
		{
			if (!ValidateOptionName(optname))
			{
				abort("FATAL ERROR: Invalid option \"%s\"\n", optname);
			}
			char* optname2 = optname;
			if (optname2[0] ==  '$')
			{
				optname2++;
			}
			SetOption(optname2, optvalue);
		}
		return true;
	}
	else
	{
		return false;
	}
}

bool Options::ValidateOptionName(const char * optname)
{
	const char* v = GetOption(optname);
	if (v)
	{
		// it's predefined option, OK
		return true;
	}

	if (optname[0] == '$')
	{
		// it's variable, OK
		return true;
	}

	if (!strncmp(optname, "server", 6))
	{
		char* p = (char*)optname + 6;
		while (*p >= '0' && *p <= '9') p++;
		if (p &&
			(!strcmp(p, ".level") || !strcmp(p, ".host") ||
			!strcmp(p, ".port") || !strcmp(p, ".username") ||
			!strcmp(p, ".password") || !strcmp(p, ".connections")))
		{
			return true;
		}
	}
	
	return false;
}

void Options::CheckOptions()
{
#ifdef DISABLE_PARCHECK
	if (m_bParCheck)
	{
		abort("FATAL ERROR: Program was compiled without parcheck-support. Invalid value for option \"%s\"\n", OPTION_PARCHECK);
	}
#endif
	
#ifdef DISABLE_CURSES
	if (m_eOutputMode == omNCurses)
	{
		abort("FATAL ERROR: Program was compiled without curses-support. Can not use \"curses\" frontend (option \"%s\")\n", OPTION_OUTPUTMODE);
	}
#endif
}
