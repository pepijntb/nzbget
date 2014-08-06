/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2007-2014 Andrey Prygunkov <hugbug@users.sourceforge.net>
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * $Revision$
 * $Date$
 *
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef WIN32
#include "win32.h"
#endif

#ifndef DISABLE_PARCHECK

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#ifdef WIN32
#include <par2cmdline.h>
#include <par2repairer.h>
#else
#include <unistd.h>
#include <libpar2/par2cmdline.h>
#include <libpar2/par2repairer.h>
#endif
#include <algorithm>

#include "nzbget.h"
#include "ParChecker.h"
#include "ParCoordinator.h"
#include "Log.h"
#include "Options.h"
#include "Util.h"

extern Options* g_pOptions;

const char* Par2CmdLineErrStr[] = { "OK",
	"data files are damaged and there is enough recovery data available to repair them",
	"data files are damaged and there is insufficient recovery data available to be able to repair them",
	"there was something wrong with the command line arguments",
	"the PAR2 files did not contain sufficient information about the data files to be able to verify them",
	"repair completed but the data files still appear to be damaged",
	"an error occured when accessing files",
	"internal error occurred",
	"out of memory" };


class Repairer : public Par2Repairer
{
private:
    CommandLine	commandLine;
	ParChecker*	m_pOwner;

protected:
	virtual bool ScanDataFile(DiskFile *diskfile, Par2RepairerSourceFile* &sourcefile,
		MatchType &matchtype, MD5Hash &hashfull, MD5Hash &hash16k, u32 &count);

public:
				Repairer(ParChecker* pOwner) { m_pOwner = pOwner; }
	Result		PreProcess(const char *szParFilename);
	Result		Process(bool dorepair);

	friend class ParChecker;
};

Result Repairer::PreProcess(const char *szParFilename)
{
	if (g_pOptions->GetParScan() == Options::psFull)
	{
		char szWildcardParam[1024];
		strncpy(szWildcardParam, szParFilename, 1024);
		szWildcardParam[1024-1] = '\0';
		char* szBasename = Util::BaseFileName(szWildcardParam);
		if (szBasename != szWildcardParam && strlen(szBasename) > 0)
		{
			szBasename[0] = '*';
			szBasename[1] = '\0';
		}

		const char* argv[] = { "par2", "r", "-v", "-v", szParFilename, szWildcardParam };
		if (!commandLine.Parse(6, (char**)argv))
		{
			return eInvalidCommandLineArguments;
		}
	}
	else
	{
		const char* argv[] = { "par2", "r", "-v", "-v", szParFilename };
		if (!commandLine.Parse(5, (char**)argv))
		{
			return eInvalidCommandLineArguments;
		}
	}

	return Par2Repairer::PreProcess(commandLine);
}

Result Repairer::Process(bool dorepair)
{
	return Par2Repairer::Process(commandLine, dorepair);
}


bool Repairer::ScanDataFile(DiskFile *diskfile, Par2RepairerSourceFile* &sourcefile,
	MatchType &matchtype, MD5Hash &hashfull, MD5Hash &hash16k, u32 &count)
{
	if (g_pOptions->GetParQuick() && sourcefile)
	{
		string path;
		string name;
		DiskFile::SplitFilename(diskfile->FileName(), path, name);

		sig_filename.emit(name);

		int iAvailableBlocks = sourcefile->BlockCount();
		ParChecker::EFileStatus eFileStatus = m_pOwner->VerifyDataFile(diskfile, sourcefile, &iAvailableBlocks);
		if (eFileStatus != ParChecker::fsUnknown)
		{
			sig_done.emit(name, iAvailableBlocks, sourcefile->BlockCount());
			sig_progress.emit(1000.0);
			matchtype = eFileStatus == ParChecker::fsSuccess ? eFullMatch : ParChecker::fsPartial ? ePartialMatch : eNoMatch;
			return true;
		}
	}

	return Par2Repairer::ScanDataFile(diskfile, sourcefile, matchtype, hashfull, hash16k, count);
}


class MissingFilesComparator
{
private:
	const char* m_szBaseParFilename;
public:
	MissingFilesComparator(const char* szBaseParFilename) : m_szBaseParFilename(szBaseParFilename) {}
	bool operator()(CommandLine::ExtraFile* pFirst, CommandLine::ExtraFile* pSecond) const;
};


/*
 * Files with the same name as in par-file (and a differnt extension) are
 * placed at the top of the list to be scanned first.
 */
bool MissingFilesComparator::operator()(CommandLine::ExtraFile* pFile1, CommandLine::ExtraFile* pFile2) const
{
	char name1[1024];
	strncpy(name1, Util::BaseFileName(pFile1->FileName().c_str()), 1024);
	name1[1024-1] = '\0';
	if (char* ext = strrchr(name1, '.')) *ext = '\0'; // trim extension

	char name2[1024];
	strncpy(name2, Util::BaseFileName(pFile2->FileName().c_str()), 1024);
	name2[1024-1] = '\0';
	if (char* ext = strrchr(name2, '.')) *ext = '\0'; // trim extension

	return strcmp(name1, m_szBaseParFilename) == 0 && strcmp(name1, name2) != 0;
}


ParChecker::Segment::Segment(bool bSuccess, long long iOffset, int iSize, unsigned long lCrc)
{
	m_bSuccess = bSuccess;
	m_iOffset = iOffset;
	m_iSize = iSize;
	m_lCrc = lCrc;
}


ParChecker::SegmentList::~SegmentList()
{
	for (iterator it = begin(); it != end(); it++)
	{
		delete *it;
	}
}


ParChecker::ParChecker()
{
    debug("Creating ParChecker");

	m_eStatus = psFailed;
	m_szDestDir = NULL;
	m_szNZBName = NULL;
	m_szParFilename = NULL;
	m_szInfoName = NULL;
	m_szErrMsg = NULL;
	m_szProgressLabel = (char*)malloc(1024);
	m_pRepairer = NULL;
	m_iFileProgress = 0;
	m_iStageProgress = 0;
	m_iExtraFiles = 0;
	m_bVerifyingExtraFiles = false;
	m_bCancelled = false;
	m_eStage = ptLoadingPars;
}

ParChecker::~ParChecker()
{
    debug("Destroying ParChecker");

	free(m_szDestDir);
	free(m_szNZBName);
	free(m_szInfoName);
	free(m_szProgressLabel);

	Cleanup();
}

void ParChecker::Cleanup()
{
	delete (Repairer*)m_pRepairer;
	m_pRepairer = NULL;

	for (FileList::iterator it = m_QueuedParFiles.begin(); it != m_QueuedParFiles.end() ;it++)
	{
		free(*it);
	}
	m_QueuedParFiles.clear();

	for (FileList::iterator it = m_ProcessedFiles.begin(); it != m_ProcessedFiles.end() ;it++)
	{
		free(*it);
	}
	m_ProcessedFiles.clear();

	m_sourceFiles.clear();

	free(m_szErrMsg);
	m_szErrMsg = NULL;
}

void ParChecker::SetDestDir(const char * szDestDir)
{
	free(m_szDestDir);
	m_szDestDir = strdup(szDestDir);
}

void ParChecker::SetNZBName(const char * szNZBName)
{
	free(m_szNZBName);
	m_szNZBName = strdup(szNZBName);
}

void ParChecker::SetInfoName(const char * szInfoName)
{
	free(m_szInfoName);
	m_szInfoName = strdup(szInfoName);
}

void ParChecker::Run()
{
	ParCoordinator::ParFileList fileList;
	if (!ParCoordinator::FindMainPars(m_szDestDir, &fileList))
	{
		PrintMessage(Message::mkError, "Could not start par-check for %s. Could not find any par-files", m_szNZBName);
		m_eStatus = psFailed;
		Completed();
		return;
	}

	m_eStatus = psRepairNotNeeded;
	m_bCancelled = false;

	for (ParCoordinator::ParFileList::iterator it = fileList.begin(); it != fileList.end(); it++)
	{
		char* szParFilename = *it;
		debug("Found par: %s", szParFilename);

		if (!IsStopped() && !m_bCancelled)
		{
			char szFullParFilename[1024];
			snprintf(szFullParFilename, 1024, "%s%c%s", m_szDestDir, (int)PATH_SEPARATOR, szParFilename);
			szFullParFilename[1024-1] = '\0';

			char szInfoName[1024];
			int iBaseLen = 0;
			ParCoordinator::ParseParFilename(szParFilename, &iBaseLen, NULL);
			int maxlen = iBaseLen < 1024 ? iBaseLen : 1024 - 1;
			strncpy(szInfoName, szParFilename, maxlen);
			szInfoName[maxlen] = '\0';
			
			char szParInfoName[1024];
			snprintf(szParInfoName, 1024, "%s%c%s", m_szNZBName, (int)PATH_SEPARATOR, szInfoName);
			szParInfoName[1024-1] = '\0';

			SetInfoName(szParInfoName);

			EStatus eStatus = RunParCheck(szFullParFilename);

			// accumulate total status, the worst status has priority
			if (m_eStatus > eStatus)
			{
				m_eStatus = eStatus;
			}

			if (g_pOptions->GetCreateBrokenLog())
			{
				WriteBrokenLog(eStatus);
			}
		}

		free(szParFilename);
	}

	Completed();
}

ParChecker::EStatus ParChecker::RunParCheck(const char* szParFilename)
{
	Cleanup();
	m_szParFilename = szParFilename;
	m_eStage = ptLoadingPars;
	m_iProcessedFiles = 0;
	m_iExtraFiles = 0;
	m_bVerifyingExtraFiles = false;
	m_bHasDamagedFiles = false;
	EStatus eStatus = psFailed;

	PrintMessage(Message::mkInfo, "Verifying %s", m_szInfoName);

    debug("par: %s", m_szParFilename);
	
	snprintf(m_szProgressLabel, 1024, "Verifying %s", m_szInfoName);
	m_szProgressLabel[1024-1] = '\0';
	m_iFileProgress = 0;
	m_iStageProgress = 0;
	UpdateProgress();

    Result res = (Result)PreProcessPar();
	if (IsStopped() || res != eSuccess)
	{
		Cleanup();
		return psFailed;
	}
	
	m_eStage = ptVerifyingSources;
	Repairer* pRepairer = (Repairer*)m_pRepairer;
	res = pRepairer->Process(false);
    debug("ParChecker: Process-result=%i", res);

	bool bAddedSplittedFragments = false;
	if (m_bHasDamagedFiles && !IsStopped() && res == eRepairNotPossible)
	{
		bAddedSplittedFragments = AddSplittedFragments();
		if (bAddedSplittedFragments)
		{
			res = pRepairer->Process(false);
			debug("ParChecker: Process-result=%i", res);
		}
	}

	if (m_bHasDamagedFiles && !IsStopped() && pRepairer->missingfilecount > 0 && 
		!(bAddedSplittedFragments && res == eRepairPossible) &&
		g_pOptions->GetParScan() == Options::psAuto)
	{
		if (AddMissingFiles())
		{
			res = pRepairer->Process(false);
			debug("ParChecker: Process-result=%i", res);
		}
	}

	if (m_bHasDamagedFiles && !IsStopped() && res == eRepairNotPossible)
	{
		res = (Result)ProcessMorePars();
	}

	if (IsStopped())
	{
		Cleanup();
		return psFailed;
	}
	
	eStatus = psFailed;
	
	if (res == eSuccess || !m_bHasDamagedFiles)
	{
    	PrintMessage(Message::mkInfo, "Repair not needed for %s", m_szInfoName);
		eStatus = psRepairNotNeeded;
	}
	else if (res == eRepairPossible)
	{
		eStatus = psRepairPossible;
		if (g_pOptions->GetParRepair())
		{
			PrintMessage(Message::mkInfo, "Repairing %s", m_szInfoName);

			SaveSourceList();
			snprintf(m_szProgressLabel, 1024, "Repairing %s", m_szInfoName);
			m_szProgressLabel[1024-1] = '\0';
			m_iFileProgress = 0;
			m_iStageProgress = 0;
			m_iProcessedFiles = 0;
			m_eStage = ptRepairing;
			m_iFilesToRepair = pRepairer->damagedfilecount + pRepairer->missingfilecount;
			UpdateProgress();

			res = pRepairer->Process(true);
    		debug("ParChecker: Process-result=%i", res);
			if (res == eSuccess)
			{
    			PrintMessage(Message::mkInfo, "Successfully repaired %s", m_szInfoName);
				eStatus = psRepaired;
				DeleteLeftovers();
			}
		}
		else
		{
    		PrintMessage(Message::mkInfo, "Repair possible for %s", m_szInfoName);
		}
	}
	
	if (m_bCancelled)
	{
		if (m_eStage >= ptRepairing)
		{
			PrintMessage(Message::mkWarning, "Repair cancelled for %s", m_szInfoName);
			m_szErrMsg = strdup("repair cancelled");
			eStatus = psRepairPossible;
		}
		else
		{
			PrintMessage(Message::mkWarning, "Par-check cancelled for %s", m_szInfoName);
			m_szErrMsg = strdup("par-check cancelled");
			eStatus = psFailed;
		}
	}
	else if (eStatus == psFailed)
	{
		if (!m_szErrMsg && (int)res >= 0 && (int)res <= 8)
		{
			m_szErrMsg = strdup(Par2CmdLineErrStr[res]);
		}
		PrintMessage(Message::mkError, "Repair failed for %s: %s", m_szInfoName, m_szErrMsg ? m_szErrMsg : "");
	}
	
	Cleanup();
	return eStatus;
}

int ParChecker::PreProcessPar()
{
	Result res = eRepairFailed;
	while (!IsStopped() && res != eSuccess)
	{
		Cleanup();

		Repairer* pRepairer = new Repairer(this);
		m_pRepairer = pRepairer;
		pRepairer->sig_filename.connect(sigc::mem_fun(*this, &ParChecker::signal_filename));
		pRepairer->sig_progress.connect(sigc::mem_fun(*this, &ParChecker::signal_progress));
		pRepairer->sig_done.connect(sigc::mem_fun(*this, &ParChecker::signal_done));

		res = pRepairer->PreProcess(m_szParFilename);
		debug("ParChecker: PreProcess-result=%i", res);

		if (IsStopped())
		{
			PrintMessage(Message::mkError, "Could not verify %s: stopping", m_szInfoName);
			m_szErrMsg = strdup("par-check was stopped");
			return eRepairFailed;
		}

		if (res == eInvalidCommandLineArguments)
		{
			PrintMessage(Message::mkError, "Could not start par-check for %s. Par-file: %s", m_szInfoName, m_szParFilename);
			m_szErrMsg = strdup("Command line could not be parsed");
			return res;
		}

		if (res != eSuccess)
		{
			PrintMessage(Message::mkWarning, "Could not verify %s: par2-file could not be processed", m_szInfoName);
			PrintMessage(Message::mkInfo, "Requesting more par2-files for %s", m_szInfoName);
			bool bHasMorePars = LoadMainParBak();
			if (!bHasMorePars)
			{
				PrintMessage(Message::mkWarning, "No more par2-files found");
				break;
			}
		}
	}

	if (res != eSuccess)
	{
		PrintMessage(Message::mkError, "Could not verify %s: par2-file could not be processed", m_szInfoName);
		m_szErrMsg = strdup("par2-file could not be processed");
		return res;
	}

	return res;
}

bool ParChecker::LoadMainParBak()
{
	while (!IsStopped())
	{
		m_mutexQueuedParFiles.Lock();
        bool hasMorePars = !m_QueuedParFiles.empty();
		for (FileList::iterator it = m_QueuedParFiles.begin(); it != m_QueuedParFiles.end() ;it++)
		{
			free(*it);
		}
		m_QueuedParFiles.clear();
		m_mutexQueuedParFiles.Unlock();

		if (hasMorePars)
		{
			return true;
		}

		int iBlockFound = 0;
		bool requested = RequestMorePars(1, &iBlockFound);
		if (requested)
		{
			strncpy(m_szProgressLabel, "Awaiting additional par-files", 1024);
			m_szProgressLabel[1024-1] = '\0';
			m_iFileProgress = 0;
			UpdateProgress();
		}

		m_mutexQueuedParFiles.Lock();
		hasMorePars = !m_QueuedParFiles.empty();
		m_bQueuedParFilesChanged = false;
		m_mutexQueuedParFiles.Unlock();

		if (!requested && !hasMorePars)
		{
			return false;
		}

		if (!hasMorePars)
		{
			// wait until new files are added by "AddParFile" or a change is signaled by "QueueChanged"
			bool bQueuedParFilesChanged = false;
			while (!bQueuedParFilesChanged && !IsStopped() && !m_bCancelled)
			{
				m_mutexQueuedParFiles.Lock();
				bQueuedParFilesChanged = m_bQueuedParFilesChanged;
				m_mutexQueuedParFiles.Unlock();
				usleep(100 * 1000);
			}
		}
	}

	return false;
}

int ParChecker::ProcessMorePars()
{
	Result res = eRepairNotPossible;
	Repairer* pRepairer = (Repairer*)m_pRepairer;

	bool bMoreFilesLoaded = true;
	while (!IsStopped() && res == eRepairNotPossible)
	{
		int missingblockcount = pRepairer->missingblockcount - pRepairer->recoverypacketmap.size();
		if (missingblockcount <= 0)
		{
			return eRepairPossible;
		}

		if (bMoreFilesLoaded)
		{
			PrintMessage(Message::mkInfo, "Need more %i par-block(s) for %s", missingblockcount, m_szInfoName);
		}
		
		m_mutexQueuedParFiles.Lock();
        bool hasMorePars = !m_QueuedParFiles.empty();
		m_mutexQueuedParFiles.Unlock();
		
		if (!hasMorePars)
		{
			int iBlockFound = 0;
			bool requested = RequestMorePars(missingblockcount, &iBlockFound);
			if (requested)
			{
				strncpy(m_szProgressLabel, "Awaiting additional par-files", 1024);
				m_szProgressLabel[1024-1] = '\0';
				m_iFileProgress = 0;
				UpdateProgress();
			}
			
			m_mutexQueuedParFiles.Lock();
			hasMorePars = !m_QueuedParFiles.empty();
			m_bQueuedParFilesChanged = false;
			m_mutexQueuedParFiles.Unlock();
			
			if (!requested && !hasMorePars)
			{
				m_szErrMsg = (char*)malloc(1024);
				snprintf(m_szErrMsg, 1024, "not enough par-blocks, %i block(s) needed, but %i block(s) available", missingblockcount, iBlockFound);
                m_szErrMsg[1024-1] = '\0';
				break;
			}
			
			if (!hasMorePars)
			{
				// wait until new files are added by "AddParFile" or a change is signaled by "QueueChanged"
				bool bQueuedParFilesChanged = false;
				while (!bQueuedParFilesChanged && !IsStopped() && !m_bCancelled)
				{
					m_mutexQueuedParFiles.Lock();
					bQueuedParFilesChanged = m_bQueuedParFilesChanged;
					m_mutexQueuedParFiles.Unlock();
					usleep(100 * 1000);
				}
			}
		}

		if (IsStopped() || m_bCancelled)
		{
			break;
		}

		bMoreFilesLoaded = LoadMorePars();
		if (bMoreFilesLoaded)
		{
			pRepairer->UpdateVerificationResults();
			res = pRepairer->Process(false);
			debug("ParChecker: Process-result=%i", res);
		}
	}

	return res;
}

bool ParChecker::LoadMorePars()
{
	m_mutexQueuedParFiles.Lock();
	FileList moreFiles;
	moreFiles.assign(m_QueuedParFiles.begin(), m_QueuedParFiles.end());
	m_QueuedParFiles.clear();
	m_mutexQueuedParFiles.Unlock();
	
	for (FileList::iterator it = moreFiles.begin(); it != moreFiles.end() ;it++)
	{
		char* szParFilename = *it;
		bool loadedOK = ((Repairer*)m_pRepairer)->LoadPacketsFromFile(szParFilename);
		if (loadedOK)
		{
			PrintMessage(Message::mkInfo, "File %s successfully loaded for par-check", Util::BaseFileName(szParFilename), m_szInfoName);
		}
		else
		{
			PrintMessage(Message::mkInfo, "Could not load file %s for par-check", Util::BaseFileName(szParFilename), m_szInfoName);
		}
		free(szParFilename);
	}

	return !moreFiles.empty();
}

void ParChecker::AddParFile(const char * szParFilename)
{
	m_mutexQueuedParFiles.Lock();
	m_QueuedParFiles.push_back(strdup(szParFilename));
	m_bQueuedParFilesChanged = true;
	m_mutexQueuedParFiles.Unlock();
}

void ParChecker::QueueChanged()
{
	m_mutexQueuedParFiles.Lock();
	m_bQueuedParFilesChanged = true;
	m_mutexQueuedParFiles.Unlock();
}

bool ParChecker::AddSplittedFragments()
{
	char szDirectory[1024];
	strncpy(szDirectory, m_szParFilename, 1024);
	szDirectory[1024-1] = '\0';

	char* szBasename = Util::BaseFileName(szDirectory);
	if (szBasename == szDirectory)
	{
		return false;
	}
	szBasename[-1] = '\0';

	std::list<CommandLine::ExtraFile> extrafiles;

	DirBrowser dir(szDirectory);
	while (const char* filename = dir.Next())
	{
		if (strcmp(filename, ".") && strcmp(filename, "..") && strcmp(filename, "_brokenlog.txt") &&
			!IsParredFile(filename) && !IsProcessedFile(filename))
		{
			for (std::vector<Par2RepairerSourceFile*>::iterator it = ((Repairer*)m_pRepairer)->sourcefiles.begin();
				it != ((Repairer*)m_pRepairer)->sourcefiles.end(); it++)
			{
				Par2RepairerSourceFile *sourcefile = *it;

				std::string target = sourcefile->TargetFileName();
				const char* szFilename2 = target.c_str();
				const char* szBasename2 = Util::BaseFileName(szFilename2);
				int iBaseLen = strlen(szBasename2);

				if (!strncasecmp(filename, szBasename2, iBaseLen))
				{
					const char* p = filename + iBaseLen;
					if (*p == '.')
					{
						for (p++; *p && strchr("0123456789", *p); p++) ;
						if (!*p)
						{
							debug("Found splitted fragment %s", filename);

							char fullfilename[1024];
							snprintf(fullfilename, 1024, "%s%c%s", szDirectory, PATH_SEPARATOR, filename);
							fullfilename[1024-1] = '\0';

							CommandLine::ExtraFile extrafile(fullfilename, Util::FileSize(fullfilename));
							extrafiles.push_back(extrafile);
						}
					}
				}
			}
		}
	}

	bool bFragmentsAdded = false;

	if (!extrafiles.empty())
	{
		m_iExtraFiles += extrafiles.size();
		m_bVerifyingExtraFiles = true;
		info("Found %i splitted fragments for %s", (int)extrafiles.size(), m_szInfoName);
		bFragmentsAdded = ((Repairer*)m_pRepairer)->VerifyExtraFiles(extrafiles);
		((Repairer*)m_pRepairer)->UpdateVerificationResults();
		m_bVerifyingExtraFiles = false;
	}

	return bFragmentsAdded;
}

bool ParChecker::AddMissingFiles()
{
    PrintMessage(Message::mkInfo, "Performing extra par-scan for %s", m_szInfoName);

	char szDirectory[1024];
	strncpy(szDirectory, m_szParFilename, 1024);
	szDirectory[1024-1] = '\0';

	char* szBasename = Util::BaseFileName(szDirectory);
	if (szBasename == szDirectory)
	{
		return false;
	}
	szBasename[-1] = '\0';

	std::list<CommandLine::ExtraFile*> extrafiles;

	DirBrowser dir(szDirectory);
	while (const char* filename = dir.Next())
	{
		if (strcmp(filename, ".") && strcmp(filename, "..") && strcmp(filename, "_brokenlog.txt") &&
			!IsParredFile(filename) && !IsProcessedFile(filename))
		{
			char fullfilename[1024];
			snprintf(fullfilename, 1024, "%s%c%s", szDirectory, PATH_SEPARATOR, filename);
			fullfilename[1024-1] = '\0';

			extrafiles.push_back(new CommandLine::ExtraFile(fullfilename, Util::FileSize(fullfilename)));
		}
	}

	// Sort the list
	char* szBaseParFilename = strdup(Util::BaseFileName(m_szParFilename));
	if (char* ext = strrchr(szBaseParFilename, '.')) *ext = '\0'; // trim extension
	extrafiles.sort(MissingFilesComparator(szBaseParFilename));
	free(szBaseParFilename);

	// Scan files
	bool bFilesAdded = false;
	if (!extrafiles.empty())
	{
		m_iExtraFiles += extrafiles.size();
		m_bVerifyingExtraFiles = true;

		std::list<CommandLine::ExtraFile> extrafiles1;

		// adding files one by one until all missing files are found

		while (!IsStopped() && !m_bCancelled && extrafiles.size() > 0 && ((Repairer*)m_pRepairer)->missingfilecount > 0)
		{
			CommandLine::ExtraFile* pExtraFile = extrafiles.front();
			extrafiles.pop_front();

			extrafiles1.clear();
			extrafiles1.push_back(*pExtraFile);

			int iWasMissing = ((Repairer*)m_pRepairer)->missingfilecount;
			((Repairer*)m_pRepairer)->VerifyExtraFiles(extrafiles1);
			bool bAdded = iWasMissing > (int)((Repairer*)m_pRepairer)->missingfilecount;
			if (bAdded)
			{
				info("Found missing file %s", Util::BaseFileName(pExtraFile->FileName().c_str()));
				RegisterParredFile(Util::BaseFileName(pExtraFile->FileName().c_str()));
			}

			bFilesAdded |= bAdded;
			((Repairer*)m_pRepairer)->UpdateVerificationResults();

			delete pExtraFile;
		}

		m_bVerifyingExtraFiles = false;

		// free any remaining objects
		for (std::list<CommandLine::ExtraFile*>::iterator it = extrafiles.begin(); it != extrafiles.end() ;it++)
		{
			delete *it;
		}
	}

	return bFilesAdded;
}

bool ParChecker::IsProcessedFile(const char* szFilename)
{
	for (FileList::iterator it = m_ProcessedFiles.begin(); it != m_ProcessedFiles.end(); it++)
	{
		const char* szProcessedFilename = *it;
		if (!strcasecmp(Util::BaseFileName(szProcessedFilename), szFilename))
		{
			return true;
		}
	}

	return false;
}

void ParChecker::signal_filename(std::string str)
{
	if (!m_lastFilename.compare(str))
	{
		return;
	}

	m_lastFilename = str;

	const char* szStageMessage[] = { "Loading file", "Verifying file", "Repairing file", "Verifying repaired file" };

	if (m_eStage == ptRepairing)
	{
		m_eStage = ptVerifyingRepaired;
	}

	PrintMessage(Message::mkInfo, "%s %s", szStageMessage[m_eStage], str.c_str());

	if (m_eStage == ptLoadingPars || m_eStage == ptVerifyingSources)
	{
		m_ProcessedFiles.push_back(strdup(str.c_str()));
	}

	snprintf(m_szProgressLabel, 1024, "%s %s", szStageMessage[m_eStage], str.c_str());
	m_szProgressLabel[1024-1] = '\0';
	m_iFileProgress = 0;
	UpdateProgress();
}

void ParChecker::signal_progress(double progress)
{
	m_iFileProgress = (int)progress;

	if (m_eStage == ptRepairing)
	{
		// calculating repair-data for all files
		m_iStageProgress = m_iFileProgress;
	}
	else
	{
		// processing individual files

		int iTotalFiles = 0;
		if (m_eStage == ptVerifyingRepaired)
		{
			// repairing individual files
			iTotalFiles = m_iFilesToRepair;
		}
		else
		{
			// verifying individual files
			iTotalFiles = ((Repairer*)m_pRepairer)->sourcefiles.size() + m_iExtraFiles;
		}

		if (iTotalFiles > 0)
		{
			if (m_iFileProgress < 1000)
			{
				m_iStageProgress = (m_iProcessedFiles * 1000 + m_iFileProgress) / iTotalFiles;
			}
			else
			{
				m_iStageProgress = m_iProcessedFiles * 1000 / iTotalFiles;
			}
		}
		else
		{
			m_iStageProgress = 0;
		}
	}

	debug("Current-progres: %i, Total-progress: %i", m_iFileProgress, m_iStageProgress);

	UpdateProgress();
}

void ParChecker::signal_done(std::string str, int available, int total)
{
	m_iProcessedFiles++;

	if (m_eStage == ptVerifyingSources)
	{
		if (available < total && !m_bVerifyingExtraFiles)
		{
			const char* szFilename = str.c_str();

			bool bFileExists = true;
			for (std::vector<Par2RepairerSourceFile*>::iterator it = ((Repairer*)m_pRepairer)->sourcefiles.begin();
				it != ((Repairer*)m_pRepairer)->sourcefiles.end(); it++)
			{
				Par2RepairerSourceFile *sourcefile = *it;
				if (sourcefile && !strcmp(szFilename, Util::BaseFileName(sourcefile->TargetFileName().c_str())) &&
					!sourcefile->GetTargetExists())
				{
					bFileExists = false;
					break;
				}
			}

			bool bIgnore = Util::MatchFileExt(szFilename, g_pOptions->GetParIgnoreExt(), ",;") ||
				Util::MatchFileExt(szFilename, g_pOptions->GetExtCleanupDisk(), ",;");
			m_bHasDamagedFiles |= !bIgnore;

			if (bFileExists)
			{
				PrintMessage(Message::mkWarning, "File %s has %i bad block(s) of total %i block(s)%s",
					szFilename, total - available, total, bIgnore ? ", ignoring" : "");
			}
			else
			{
				PrintMessage(Message::mkWarning, "File %s with %i block(s) is missing%s",
					szFilename, total, bIgnore ? ", ignoring" : "");
			}
		}
	}
}

void ParChecker::Cancel()
{
#ifdef HAVE_PAR2_CANCEL
	((Repairer*)m_pRepairer)->cancelled = true;
	m_bCancelled = true;
	QueueChanged();
#else
	PrintMessage(Message::mkError, "Could not cancel par-repair. The program was compiled using version of libpar2 which doesn't support cancelling of par-repair. Please apply libpar2-patches supplied with NZBGet and recompile libpar2 and NZBGet (see README for details).");
#endif
}

void ParChecker::WriteBrokenLog(EStatus eStatus)
{
	char szBrokenLogName[1024];
	snprintf(szBrokenLogName, 1024, "%s%c_brokenlog.txt", m_szDestDir, (int)PATH_SEPARATOR);
	szBrokenLogName[1024-1] = '\0';
	
	if (eStatus != psRepairNotNeeded || Util::FileExists(szBrokenLogName))
	{
		FILE* file = fopen(szBrokenLogName, FOPEN_AB);
		if (file)
		{
			if (eStatus == psFailed)
			{
				if (m_bCancelled)
				{
					fprintf(file, "Repair cancelled for %s\n", m_szInfoName);
				}
				else
				{
					fprintf(file, "Repair failed for %s: %s\n", m_szInfoName, m_szErrMsg ? m_szErrMsg : "");
				}
			}
			else if (eStatus == psRepairPossible)
			{
				fprintf(file, "Repair possible for %s\n", m_szInfoName);
			}
			else if (eStatus == psRepaired)
			{
				fprintf(file, "Successfully repaired %s\n", m_szInfoName);
			}
			else if (eStatus == psRepairNotNeeded)
			{
				fprintf(file, "Repair not needed for %s\n", m_szInfoName);
			}
			fclose(file);
		}
		else
		{
			PrintMessage(Message::mkError, "Could not open file %s", szBrokenLogName);
		}
	}
}

void ParChecker::SaveSourceList()
{
	// Buliding a list of DiskFile-objects, marked as source-files
	
	for (std::vector<Par2RepairerSourceFile*>::iterator it = ((Repairer*)m_pRepairer)->sourcefiles.begin();
		it != ((Repairer*)m_pRepairer)->sourcefiles.end(); it++)
	{
		Par2RepairerSourceFile* sourcefile = (Par2RepairerSourceFile*)*it;
		vector<DataBlock>::iterator it2 = sourcefile->SourceBlocks();
		for (int i = 0; i < (int)sourcefile->BlockCount(); i++, it2++)
		{
			DataBlock block = *it2;
			DiskFile* pSourceFile = block.GetDiskFile();
			if (pSourceFile &&
				std::find(m_sourceFiles.begin(), m_sourceFiles.end(), pSourceFile) == m_sourceFiles.end())
			{
				m_sourceFiles.push_back(pSourceFile);
			}
		}
	}
}

void ParChecker::DeleteLeftovers()
{
	// After repairing check if all DiskFile-objects saved by "SaveSourceList()" have
	// corresponding target-files. If not - the source file was replaced. In this case
	// the DiskFile-object points to the renamed bak-file, which we can delete.
	
	for (SourceList::iterator it = m_sourceFiles.begin(); it != m_sourceFiles.end(); it++)
	{
		DiskFile* pSourceFile = (DiskFile*)*it;

		bool bFound = false;
		for (std::vector<Par2RepairerSourceFile*>::iterator it2 = ((Repairer*)m_pRepairer)->sourcefiles.begin();
			it2 != ((Repairer*)m_pRepairer)->sourcefiles.end(); it2++)
		{
			Par2RepairerSourceFile* sourcefile = *it2;
			if (sourcefile->GetTargetFile() == pSourceFile)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			PrintMessage(Message::mkInfo, "Deleting file %s", Util::BaseFileName(pSourceFile->FileName().c_str()));
			remove(pSourceFile->FileName().c_str());
		}
	}
}

/**
 * This function implements quick par verification replacing the standard verification routine
 * from libpar2:
 * - for successfully downloaded files the function compares CRC of the file computed during
 *   download with CRC stored in PAR2-file;
 * - for partially downloaded files the CRCs of articles are compared with block-CRCs stored
 *   in PAR2-file;
 * - for completely failed files (not a single successful artice) no verification is needed at all.
 *
 * Limitation of the function:
 * This function requires every block in the file to have an unique CRC (across all blocks
 * of the par-set). Otherwise the full verification is performed.
 * The limitation can be avoided by using something more smart than "verificationhashtable.Lookup"
 * but in the real life all blocks have unique CRCs and the simple "Lookup" works good enough.
 */
ParChecker::EFileStatus ParChecker::VerifyDataFile(void* pDiskfile, void* pSourcefile, int* pAvailableBlocks)
{
	if (m_eStage != ptVerifyingSources)
	{
		// do full verification for repaired files
		return fsUnknown;
	}

	DiskFile* pDiskFile = (DiskFile*)pDiskfile;
	Par2RepairerSourceFile* pSourceFile = (Par2RepairerSourceFile*)pSourcefile;
	if (!pSourcefile || !pSourceFile->GetTargetExists())
	{
		return fsUnknown;
	}

	VerificationPacket* packet = pSourceFile->GetVerificationPacket();
	if (!packet)
	{
		return fsUnknown;
	}

	const char* szFilename = pSourceFile->GetTargetFile()->FileName().c_str();

	// find file status and CRC computed during download
	unsigned long lDownloadCrc;
	SegmentList segments;
	EFileStatus	eFileStatus = FindFileCrc(Util::BaseFileName(szFilename), &lDownloadCrc, &segments);
	ValidBlocks validBlocks;

	if (eFileStatus == fsFailure || eFileStatus == fsUnknown)
	{
		return eFileStatus;
	}
	else if ((eFileStatus == fsSuccess && !VerifySuccessDataFile(pDiskfile, pSourcefile, lDownloadCrc)) ||
		(eFileStatus == fsPartial && !VerifyPartialDataFile(pDiskfile, pSourcefile, &segments, &validBlocks)))
	{
		warn("Quick verification failed for %s file %s, performing full verification instead",
			eFileStatus == fsSuccess ? "good" : "damaged", Util::BaseFileName(szFilename));
		return fsUnknown; // let libpar2 do the full verification of the file
	}

	// attach verification blocks to the file
	*pAvailableBlocks = 0;
	u64 blocksize = ((Repairer*)m_pRepairer)->mainpacket->BlockSize();
	std::deque<const VerificationHashEntry*> undoList;
	for (unsigned int i = 0; i < packet->BlockCount(); i++)
	{
		if (eFileStatus == fsSuccess || validBlocks.at(i))
		{
			const FILEVERIFICATIONENTRY* entry = packet->VerificationEntry(i);
			u32 blockCrc = entry->crc;

			// Look for a match
			const VerificationHashEntry* pHashEntry = ((Repairer*)m_pRepairer)->verificationhashtable.Lookup(blockCrc);
			if (!pHashEntry || pHashEntry->SourceFile() != pSourceFile || pHashEntry->IsSet())
			{
				// no match found, revert back the changes made by "pHashEntry->SetBlock"
				for (std::deque<const VerificationHashEntry*>::iterator it = undoList.begin(); it != undoList.end(); it++)
				{
					const VerificationHashEntry* pUndoEntry = *it;
					pUndoEntry->SetBlock(NULL, 0);
				}
				return fsUnknown;
			}

			undoList.push_back(pHashEntry);
			pHashEntry->SetBlock(pDiskFile, i*blocksize);
			(*pAvailableBlocks)++;
		}
	}

	PrintMessage(Message::mkDetail, "Quickly verified %s file %s",
		eFileStatus == fsSuccess ? "good" : "damaged", Util::BaseFileName(szFilename));

	return eFileStatus;
}

bool ParChecker::VerifySuccessDataFile(void* pDiskfile, void* pSourcefile, unsigned long lDownloadCrc)
{
	Par2RepairerSourceFile* pSourceFile = (Par2RepairerSourceFile*)pSourcefile;
	u64 blocksize = ((Repairer*)m_pRepairer)->mainpacket->BlockSize();
	VerificationPacket* packet = pSourceFile->GetVerificationPacket();

	// extend lDownloadCrc to block size
	lDownloadCrc = CRCUpdateBlock(lDownloadCrc ^ 0xFFFFFFFF,
		(size_t)(blocksize * packet->BlockCount() - pSourceFile->GetTargetFile()->FileSize())) ^ 0xFFFFFFFF;
	debug("Download-CRC: %.8x", lDownloadCrc);

	// compute file CRC using CRCs of blocks
	unsigned long lParCrc = 0;
	for (unsigned int i = 0; i < packet->BlockCount(); i++)
	{
		const FILEVERIFICATIONENTRY* entry = packet->VerificationEntry(i);
		u32 blockCrc = entry->crc;
		lParCrc = i == 0 ? blockCrc : Util::Crc32Combine(lParCrc, blockCrc, (unsigned long)blocksize);
	}
	debug("Block-CRC: %x, filename: %s", lParCrc, Util::BaseFileName(pSourceFile->GetTargetFile()->FileName().c_str()));

	return lParCrc == lDownloadCrc;
}

bool ParChecker::VerifyPartialDataFile(void* pDiskfile, void* pSourcefile, SegmentList* pSegments, ValidBlocks* pValidBlocks)
{
	Par2RepairerSourceFile* pSourceFile = (Par2RepairerSourceFile*)pSourcefile;
	u64 blocksize = ((Repairer*)m_pRepairer)->mainpacket->BlockSize();
	VerificationPacket* packet = pSourceFile->GetVerificationPacket();
	const char* szFilename = pSourceFile->GetTargetFile()->FileName().c_str();
	u64 iFileSize = pSourceFile->GetTargetFile()->FileSize();

	// determine presumably valid and bad blocks based on article download status
	pValidBlocks->resize(packet->BlockCount(), false);
	for (int i = 0; i < pValidBlocks->size(); i++)
	{
		u64 blockStart = i * blocksize;
		u64 blockEnd = blockStart + blocksize < iFileSize - 1 ? blockStart + blocksize : iFileSize - 1;
		bool bBlockOK = false;
		bool bBlockEnd = false;
		u64 iCurOffset = 0;
		for (SegmentList::iterator it = pSegments->begin(); it != pSegments->end(); it++)
		{
			Segment* pSegment = *it;
			if (!bBlockOK && pSegment->GetSuccess() && pSegment->GetOffset() <= blockStart &&
				pSegment->GetOffset() + pSegment->GetSize() >= blockStart)
			{
				bBlockOK = true;
				iCurOffset = pSegment->GetOffset();
			}
			if (bBlockOK)
			{
				if (!(pSegment->GetSuccess() && pSegment->GetOffset() == iCurOffset))
				{
					bBlockOK = false;
					break;
				}
				if (pSegment->GetOffset() + pSegment->GetSize() >= blockEnd)
				{
					bBlockEnd = true;
					break;
				}
				iCurOffset = pSegment->GetOffset() + pSegment->GetSize();
			}
		}
		pValidBlocks->at(i) = bBlockOK && bBlockEnd;
	}

	char szErrBuf[256];
	FILE* infile = fopen(szFilename, FOPEN_RB);
	if (!infile)
	{
		error("Could not open file %s: %s", szFilename, Util::GetLastErrorMessage(szErrBuf, sizeof(szErrBuf)));
	}

	// For each sequential range of presumably valid blocks:
	// - compute par-CRC of the range of blocks using block CRCs;
	// - compute download-CRC for the same byte range using CRCs of articles; if articles and block
	//   overlap - read a little bit of data from the file and calculate its CRC;
	// - compare two CRCs - they must match; if not - the file is more damaged than we thought -
	//   let libpar2 do the full verification of the file in this case.
	unsigned long lParCrc = 0;
	int iBlockStart = -1;
	pValidBlocks->push_back(false); // end marker
	for (int i = 0; i < pValidBlocks->size(); i++)
	{
		bool bValidBlock = pValidBlocks->at(i);
		if (bValidBlock)
		{
			if (iBlockStart == -1)
			{
				iBlockStart = i;
			}
			const FILEVERIFICATIONENTRY* entry = packet->VerificationEntry(i);
			u32 blockCrc = entry->crc;
			lParCrc = iBlockStart == i ? blockCrc : Util::Crc32Combine(lParCrc, blockCrc, (unsigned long)blocksize);
		}
		else
		{
			if (iBlockStart > -1)
			{
				int iBlockEnd = i - 1;
				long long iBytesStart = iBlockStart * blocksize;
				long long iBytesEnd = iBlockEnd * blocksize + blocksize - 1;
				unsigned long lDownloadCrc = 0;
				bool bOK = SmartCalcFileRangeCrc(infile, iBytesStart,
					iBytesEnd < iFileSize - 1 ? iBytesEnd : iFileSize - 1, pSegments, &lDownloadCrc);
				if (bOK && iBytesEnd > iFileSize - 1)
				{
					// for the last block: extend lDownloadCrc to block size
					lDownloadCrc = CRCUpdateBlock(lDownloadCrc ^ 0xFFFFFFFF, iBytesEnd - (iFileSize - 1)) ^ 0xFFFFFFFF;
				}

				if (!bOK || lDownloadCrc != lParCrc)
				{
					fclose(infile);
					return false;
				}
			}
			iBlockStart = -1;
		}
	}

	fclose(infile);

	return true;
}

/*
 * Compute CRC of bytes range of file using CRCs of segments and reading some data directly
 * from file if necessary
 */
bool ParChecker::SmartCalcFileRangeCrc(FILE* pFile, long long lStart, long long lEnd, SegmentList* pSegments,
	unsigned long* pDownloadCrc)
{
	unsigned long lDownloadCrc = 0;
	bool bStarted = false;
	for (SegmentList::iterator it = pSegments->begin(); it != pSegments->end(); it++)
	{
		Segment* pSegment = *it;

		if (!bStarted && pSegment->GetOffset() > lStart)
		{
			// read start of range from file
			if (!DumbCalcFileRangeCrc(pFile, lStart, pSegment->GetOffset() - 1, &lDownloadCrc))
			{
				return false;
			}
			if (pSegment->GetOffset() + pSegment->GetSize() >= lEnd)
			{
				break;
			}
			bStarted = true;
		}

		if (pSegment->GetOffset() >= lStart && pSegment->GetOffset() + pSegment->GetSize() <= lEnd)
		{
			lDownloadCrc = !bStarted ? pSegment->GetCrc() : Util::Crc32Combine(lDownloadCrc, pSegment->GetCrc(), (unsigned long)pSegment->GetSize());
			bStarted = true;
		}

		if (pSegment->GetOffset() + pSegment->GetSize() == lEnd)
		{
			break;
		}

		if (pSegment->GetOffset() + pSegment->GetSize() > lEnd)
		{
			// read end of range from file
			unsigned long lPartialCrc = 0;
			if (!DumbCalcFileRangeCrc(pFile, pSegment->GetOffset(), lEnd, &lPartialCrc))
			{
				return false;
			}

			lDownloadCrc = Util::Crc32Combine(lDownloadCrc, lPartialCrc, lEnd - pSegment->GetOffset() + 1);

			break;
		}
	}

	*pDownloadCrc = lDownloadCrc;
	return true;
}

/*
 * Compute CRC of bytes range of file reading the data directly from file
 */
bool ParChecker::DumbCalcFileRangeCrc(FILE* pFile, long long lStart, long long lEnd, unsigned long* pDownloadCrc)
{
	if (fseek(pFile, lStart, SEEK_SET))
	{
		return false;
	}

	static const int BUFFER_SIZE = 1024 * 64;
	unsigned char* buffer = (unsigned char*)malloc(BUFFER_SIZE);
	unsigned long lDownloadCrc = 0xFFFFFFFF;

	int cnt = BUFFER_SIZE;
	while (cnt == BUFFER_SIZE && lStart < lEnd)
	{
		int iNeedBytes = lEnd - lStart + 1 > BUFFER_SIZE ? BUFFER_SIZE : lEnd - lStart + 1;
		cnt = (int)fread(buffer, 1, iNeedBytes, pFile);
		lDownloadCrc = Util::Crc32m(lDownloadCrc, buffer, cnt);
		lStart += cnt;
	}

	free(buffer);

	lDownloadCrc ^= 0xFFFFFFFF;

	*pDownloadCrc = lDownloadCrc;
	return true;
}

#endif
