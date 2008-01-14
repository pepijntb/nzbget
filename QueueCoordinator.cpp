/*
 *  This file if part of nzbget
 *
 *  Copyright (C) 2005  Bo Cordes Petersen <placebodk@users.sourceforge.net>
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
#ifndef WIN32
#include <unistd.h>
#include <sys/time.h>
#endif

#include "nzbget.h"
#include "QueueCoordinator.h"
#include "Options.h"
#include "ServerPool.h"
#include "ArticleDownloader.h"
#include "DiskState.h"
#include "Log.h"
#include "Util.h"
#include "Decoder.h"

extern Options* g_pOptions;
extern ServerPool* g_pServerPool;
extern DiskState* g_pDiskState;

QueueCoordinator::QueueCoordinator()
{
	debug("Creating QueueCoordinator");

	m_bHasMoreJobs = true;
	m_DownloadQueue.clear();
	m_ActiveDownloads.clear();

	for (int i = 0; i < SPEEDMETER_SECONDS; i++)
	{
		m_iSpeedBytes[i] = 0;
	}
	m_iSpeedBytesIndex = 0;

	YDecoder::Init();
}

QueueCoordinator::~QueueCoordinator()
{
	debug("Destroying QueueCoordinator");
	// Cleanup

	debug("Deleting DownloadQueue");
	for (DownloadQueue::iterator it = m_DownloadQueue.begin(); it != m_DownloadQueue.end(); it++)
	{
		delete *it;
	}
	m_DownloadQueue.clear();

	debug("Deleting ArticleDownloaders");
	for (ActiveDownloads::iterator it = m_ActiveDownloads.begin(); it != m_ActiveDownloads.end(); it++)
	{
		delete *it;
	}
	m_ActiveDownloads.clear();

	YDecoder::Final();

	debug("QueueCoordinator destroyed");
}

void QueueCoordinator::Run()
{
	debug("Entering QueueCoordinator-loop");

	m_mutexDownloadQueue.Lock();

	if (g_pOptions->GetServerMode() && g_pOptions->GetSaveQueue() && g_pDiskState->Exists())
	{
		if (g_pOptions->GetReloadQueue())
		{
			g_pDiskState->Load(&m_DownloadQueue);
		}
		else
		{
			g_pDiskState->Discard();
		}
	}

	g_pDiskState->CleanupTempDir(&m_DownloadQueue);

	m_mutexDownloadQueue.Unlock();

	int iResetCounter = 0;

	while (!IsStopped())
	{
		if (!g_pOptions->GetPause())
		{
			NNTPConnection* pConnection = g_pServerPool->GetConnection(0, false);
			if (pConnection)
			{
				// start download for next article
				FileInfo* pFileInfo;
				ArticleInfo* pArticleInfo;

				m_mutexDownloadQueue.Lock();
				bool bHasMoreArticles = GetNextArticle(pFileInfo, pArticleInfo);
				m_bHasMoreJobs = bHasMoreArticles || !m_ActiveDownloads.empty();
				if (bHasMoreArticles && !IsStopped() && Thread::GetThreadCount() < g_pOptions->GetThreadLimit())
				{
					StartArticleDownload(pFileInfo, pArticleInfo, pConnection);
				}
				else
				{
					g_pServerPool->FreeConnection(pConnection, false);
				}
				m_mutexDownloadQueue.Unlock();
			}
		}

		// sleep longer in StandBy
		int iSleepInterval = (g_pOptions->GetPause() || !m_bHasMoreJobs) ? 100 : 5;
		usleep(iSleepInterval * 1000);

		AddSpeedReading(0);

		iResetCounter+= iSleepInterval;
		if (iResetCounter >= 1000)
		{
			// this code should not be called very often, once per second is OK
			g_pServerPool->CloseUnusedConnections();
			ResetHangingDownloads();
			iResetCounter = 0;
		}
	}

	// waiting for downloads
	debug("QueueCoordinator: waiting for Downloads to complete");
	bool completed = false;
	while (!completed)
	{
		m_mutexDownloadQueue.Lock();
		completed = m_ActiveDownloads.size() == 0;
		m_mutexDownloadQueue.Unlock();
		usleep(100 * 1000);
		ResetHangingDownloads();
	}
	debug("QueueCoordinator: Downloads are completed");

	debug("Exiting QueueCoordinator-loop");
}

void QueueCoordinator::AddNZBFileToQueue(NZBFile* pNZBFile, bool bAddFirst)
{
	debug("Adding NZBFile to queue");

	m_mutexDownloadQueue.Lock();

	DownloadQueue tmpDownloadQueue;
	tmpDownloadQueue.clear();
	DownloadQueue DupeList;
	DupeList.clear();

	for (NZBFile::FileInfos::iterator it = pNZBFile->GetFileInfos()->begin(); it != pNZBFile->GetFileInfos()->end(); it++)
	{
		FileInfo* pFileInfo = *it;

		if (g_pOptions->GetDupeCheck())
		{
			bool dupe = false;
			if (IsDupe(pFileInfo))
			{
				warn("File \"%s\" seems to be duplicate, skipping", pFileInfo->GetFilename());
				dupe = true;
			}
			for (NZBFile::FileInfos::iterator it2 = pNZBFile->GetFileInfos()->begin(); it2 != pNZBFile->GetFileInfos()->end(); it2++)
			{
				FileInfo* pFileInfo2 = *it2;
				if (!strcmp(pFileInfo->GetFilename(), pFileInfo2->GetFilename()) &&
					(pFileInfo->GetSize() < pFileInfo2->GetSize()))
				{
					warn("File \"%s\" appears twice in nzb-request, adding only the biggest file", pFileInfo->GetFilename());
					dupe = true;
					break;
				}
			}
			if (dupe)
			{
				DupeList.push_back(pFileInfo);
				continue;
			}
		}

		if (bAddFirst)
		{
			tmpDownloadQueue.push_front(pFileInfo);
		}
		else
		{
			tmpDownloadQueue.push_back(pFileInfo);
		}
	}

	for (DownloadQueue::iterator it = tmpDownloadQueue.begin(); it != tmpDownloadQueue.end(); it++)
	{
		if (bAddFirst)
		{
			m_DownloadQueue.push_front(*it);
		}
		else
		{
			m_DownloadQueue.push_back(*it);
		}
	}

	for (DownloadQueue::iterator it = DupeList.begin(); it != DupeList.end(); it++)
	{
		FileInfo* pFileInfo = *it;
		g_pDiskState->DiscardFile(NULL, pFileInfo);
		delete pFileInfo;
	}

	pNZBFile->DetachFileInfos();

	Aspect aspect = { eaNZBFileAdded, NULL, &m_DownloadQueue, pNZBFile->GetFileName() };
	Notify(&aspect);
	
	if (g_pOptions->GetSaveQueue() && g_pOptions->GetServerMode())
	{
		g_pDiskState->Save(&m_DownloadQueue);
	}

	m_mutexDownloadQueue.Unlock();
}

bool QueueCoordinator::AddFileToQueue(const char* szFileName)
{
	// Parse the buffer and make it into a NZBFile
	NZBFile* pNZBFile = NZBFile::CreateFromFile(szFileName);

	// Did file parse correctly?
	if (!pNZBFile)
	{
		return false;
	}

	// Add NZBFile to Queue
	AddNZBFileToQueue(pNZBFile, false);

	delete pNZBFile;

	return true;
}

/*
 * NOTE: see note to "AddSpeedReading"
 */
float QueueCoordinator::CalcCurrentDownloadSpeed()
{
	int iTotal = 0;

	for (int i = 0; i < SPEEDMETER_SECONDS; i++)
	{
		iTotal += m_iSpeedBytes[i];
	}

	float fSpeed = iTotal / 1024.0 / SPEEDMETER_SECONDS;

	return fSpeed;
}

/*
 * NOTE: we should use mutex by access to m_iSpeedBytes and m_iSpeedBytesIndex,
 * but this would results in a big performance loss (the function 
 * "AddSpeedReading" is called extremly often), so we better agree with calculation 
 * errors possible because of simultaneuos access from several threads.
 * The used algorithm is able to recover after few seconds.
 * In any case the calculation errors can not result in fatal system 
 * errors (segmentation faults).
 */
void QueueCoordinator::AddSpeedReading(int iBytes)
{
	int iIndex = time(NULL);

	if (iIndex - m_iSpeedBytesIndex > SPEEDMETER_SECONDS)
	{
		m_iSpeedBytesIndex = iIndex - SPEEDMETER_SECONDS - 1;
	}

	for (int i = m_iSpeedBytesIndex + 1; i < iIndex; i++)
	{
		m_iSpeedBytes[i % SPEEDMETER_SECONDS] = 0;
	}

	if (iIndex > m_iSpeedBytesIndex)
	{
		m_iSpeedBytesIndex = iIndex;
		m_iSpeedBytes[iIndex % SPEEDMETER_SECONDS] = iBytes;
	}
	else
	{
		m_iSpeedBytes[m_iSpeedBytesIndex % SPEEDMETER_SECONDS] += iBytes;
	}
}

long long QueueCoordinator::CalcRemainingSize()
{
	long long lRemainingSize = 0;

	m_mutexDownloadQueue.Lock();
	for (DownloadQueue::iterator it = m_DownloadQueue.begin(); it != m_DownloadQueue.end(); it++)
	{
		FileInfo* pFileInfo = *it;
		if (!pFileInfo->GetPaused() && !pFileInfo->GetDeleted())
		{
			lRemainingSize += pFileInfo->GetRemainingSize();
		}
	}
	m_mutexDownloadQueue.Unlock();

	return lRemainingSize;
}

/*
 * NOTE: DownloadQueue must be locked prior to call of this function
 * Returns True if Entry was deleted from Queue or False if it was scheduled for Deletion.
 * NOTE: "False" does not mean unsuccess; the entry is (or will be) deleted in any case.
 */
bool QueueCoordinator::DeleteQueueEntry(FileInfo* pFileInfo)
{
	pFileInfo->SetDeleted(true);
	bool hasDownloads = false;
	for (ActiveDownloads::iterator it = m_ActiveDownloads.begin(); it != m_ActiveDownloads.end(); it++)
	{
		ArticleDownloader* pArticleDownloader = *it;
		if (pArticleDownloader->GetFileInfo() == pFileInfo)
		{
			hasDownloads = true;
			pArticleDownloader->Stop();
		}
	}
	if (!hasDownloads)
	{
		Aspect aspect = { eaFileDeleted, pFileInfo, &m_DownloadQueue, NULL };
		Notify(&aspect);

		DeleteFileInfo(pFileInfo);
	}
	return hasDownloads;
}

void QueueCoordinator::Stop()
{
	Thread::Stop();

	debug("Stopping ArticleDownloads");
	m_mutexDownloadQueue.Lock();
	for (ActiveDownloads::iterator it = m_ActiveDownloads.begin(); it != m_ActiveDownloads.end(); it++)
	{
		(*it)->Stop();
	}
	m_mutexDownloadQueue.Unlock();
	debug("ArticleDownloads are notified");
}

bool QueueCoordinator::GetNextArticle(FileInfo* &pFileInfo, ArticleInfo* &pArticleInfo)
{
	//debug("QueueCoordinator::GetNextArticle()");

	for (DownloadQueue::iterator it = m_DownloadQueue.begin(); it != m_DownloadQueue.end(); it++)
	{
		pFileInfo = *it;
		if (!pFileInfo->GetPaused() && !pFileInfo->GetDeleted())
		{
			if (pFileInfo->GetArticles()->empty() && g_pOptions->GetSaveQueue() && g_pOptions->GetServerMode())
			{
				g_pDiskState->LoadArticles(pFileInfo);
			}
			for (FileInfo::Articles::iterator at = pFileInfo->GetArticles()->begin(); at != pFileInfo->GetArticles()->end(); at++)
			{
				pArticleInfo = *at;
				if (pArticleInfo->GetStatus() == 0)
				{
					return true;
				}
			}
		}
	}

	return false;
}

void QueueCoordinator::StartArticleDownload(FileInfo* pFileInfo, ArticleInfo* pArticleInfo, NNTPConnection* pConnection)
{
	debug("Starting new ArticleDownloader");

	ArticleDownloader* pArticleDownloader = new ArticleDownloader();
	pArticleDownloader->SetAutoDestroy(true);
	pArticleDownloader->Attach(this);
	pArticleDownloader->SetFileInfo(pFileInfo);
	pArticleDownloader->SetArticleInfo(pArticleInfo);
	pArticleDownloader->SetConnection(pConnection);
	BuildArticleFilename(pArticleDownloader, pFileInfo, pArticleInfo);

	pArticleInfo->SetStatus(ArticleInfo::aiRunning);

	m_ActiveDownloads.push_back(pArticleDownloader);
	pArticleDownloader->Start();
}

void QueueCoordinator::BuildArticleFilename(ArticleDownloader* pArticleDownloader, FileInfo* pFileInfo, ArticleInfo* pArticleInfo)
{
	char name[1024];
	
	snprintf(name, 1024, "%s%i.%03i", g_pOptions->GetTempDir(), pFileInfo->GetID(), pArticleInfo->GetPartNumber());
	name[1024-1] = '\0';
	pArticleInfo->SetResultFilename(name);

	char tmpname[1024];
	snprintf(tmpname, 1024, "%s.tmp", name);
	tmpname[1024-1] = '\0';
	pArticleDownloader->SetTempFilename(tmpname);

	char szNZBNiceName[1024];
	pFileInfo->GetNiceNZBName(szNZBNiceName, 1024);
	
	snprintf(name, 1024, "%s%c%s [%i/%i]", szNZBNiceName, (int)PATH_SEPARATOR, pFileInfo->GetFilename(), pArticleInfo->GetPartNumber(), pFileInfo->GetArticles()->size());
	name[1024-1] = '\0';
	pArticleDownloader->SetInfoName(name);

	if (g_pOptions->GetDirectWrite())
	{
		snprintf(name, 1024, "%s%i.out", g_pOptions->GetTempDir(), pFileInfo->GetID());
		name[1024-1] = '\0';
		pArticleDownloader->SetOutputFilename(name);
	}
}

DownloadQueue* QueueCoordinator::LockQueue()
{
	m_mutexDownloadQueue.Lock();
	return &m_DownloadQueue;
}

void QueueCoordinator::UnlockQueue()
{
	m_mutexDownloadQueue.Unlock();
}

void QueueCoordinator::Update(Subject* Caller, void* Aspect)
{
	debug("Notification from ArticleDownloader received");

	ArticleDownloader* pArticleDownloader = (ArticleDownloader*) Caller;
	if ((pArticleDownloader->GetStatus() == ArticleDownloader::adFinished) ||
	        (pArticleDownloader->GetStatus() == ArticleDownloader::adFailed))
	{
		ArticleCompleted(pArticleDownloader);
	}
}

void QueueCoordinator::ArticleCompleted(ArticleDownloader* pArticleDownloader)
{
	debug("Article downloaded");

	FileInfo* pFileInfo = pArticleDownloader->GetFileInfo();
	ArticleInfo* pArticleInfo = pArticleDownloader->GetArticleInfo();

	m_mutexDownloadQueue.Lock();

	if (pArticleDownloader->GetStatus() == ArticleDownloader::adFinished)
	{
		pArticleInfo->SetStatus(ArticleInfo::aiFinished);
	}
	else if (pArticleDownloader->GetStatus() == ArticleDownloader::adFailed)
	{
		pArticleInfo->SetStatus(ArticleInfo::aiFailed);
	}

	pFileInfo->SetRemainingSize(pFileInfo->GetRemainingSize() - pArticleInfo->GetSize());
	pFileInfo->SetCompleted(pFileInfo->GetCompleted() + 1);
	bool fileCompleted = (int)pFileInfo->GetArticles()->size() == pFileInfo->GetCompleted();

	if (!pFileInfo->GetFilenameConfirmed() &&
		pArticleDownloader->GetStatus() == ArticleDownloader::adFinished &&
		pArticleDownloader->GetArticleFilename())
	{
		pFileInfo->SetFilename(pArticleDownloader->GetArticleFilename());
		pFileInfo->SetFilenameConfirmed(true);
		if (g_pOptions->GetDupeCheck() && pFileInfo->IsDupe(pFileInfo->GetFilename()))
		{
			warn("File \"%s\" seems to be duplicate, cancelling download and deleting file from queue", pFileInfo->GetFilename());
			fileCompleted = false;
			DeleteQueueEntry(pFileInfo);
		}
	}

	bool deleteFileObj = false;

	if (pFileInfo->GetDeleted())
	{
		int cnt = 0;
		for (ActiveDownloads::iterator it = m_ActiveDownloads.begin(); it != m_ActiveDownloads.end(); it++)
		{
			if ((*it)->GetFileInfo() == pFileInfo)
			{
				cnt++;
			}
		}
		if (cnt == 1)
		{
			// this was the last Download for a file deleted from queue
			deleteFileObj = true;
		}
	}

	if (fileCompleted && !IsStopped() && !pFileInfo->GetDeleted())
	{
		// all jobs done
		m_mutexDownloadQueue.Unlock();
		pArticleDownloader->CompleteFileParts();
		m_mutexDownloadQueue.Lock();
		deleteFileObj = true;
	}

	// delete Download from Queue
	for (ActiveDownloads::iterator it = m_ActiveDownloads.begin(); it != m_ActiveDownloads.end(); it++)
	{
		ArticleDownloader* pa = *it;
		if (pa == pArticleDownloader)
		{
			m_ActiveDownloads.erase(it);
			break;
		}
	}
	if (deleteFileObj)
	{
		// delete File from Queue
		pFileInfo->SetDeleted(true);

		Aspect aspect = { fileCompleted ? eaFileCompleted : eaFileDeleted, pFileInfo, &m_DownloadQueue, NULL };
		Notify(&aspect);
		
		DeleteFileInfo(pFileInfo);
	}

	m_mutexDownloadQueue.Unlock();
}

void QueueCoordinator::DeleteFileInfo(FileInfo* pFileInfo)
{
	for (DownloadQueue::iterator it = m_DownloadQueue.begin(); it != m_DownloadQueue.end(); it++)
	{
		FileInfo* pa = *it;
		if (pa == pFileInfo)
		{
			m_DownloadQueue.erase(it);
			break;
		}
	}

	if (g_pOptions->GetSaveQueue() && g_pOptions->GetServerMode())
	{
		g_pDiskState->DiscardFile(&m_DownloadQueue, pFileInfo);
	}

	delete pFileInfo;
}

bool QueueCoordinator::IsDupe(FileInfo* pFileInfo)
{
	debug("Checking if the file is already queued");

	// checking on disk
	if (pFileInfo->IsDupe(pFileInfo->GetFilename()))
	{
		return true;
	}

	// checking in queue
	for (DownloadQueue::iterator it = m_DownloadQueue.begin(); it != m_DownloadQueue.end(); it++)
	{
		FileInfo* pQueueEntry = *it;
		if (!strcmp(pFileInfo->GetDestDir(), pQueueEntry->GetDestDir()) &&
			!strcmp(pFileInfo->GetFilename(), pQueueEntry->GetFilename()) &&
			pFileInfo != pQueueEntry)
		{
			return true;
		}
	}

	return false;
}

void QueueCoordinator::LogDebugInfo()
{
	debug("--------------------------------------------");
	debug("Dumping debug info to log");
	debug("--------------------------------------------");

	debug("   QueueCoordinator");
	debug("   ----------------");

	m_mutexDownloadQueue.Lock();
	debug("    Active Downloads: %i", m_ActiveDownloads.size());
	for (ActiveDownloads::iterator it = m_ActiveDownloads.begin(); it != m_ActiveDownloads.end(); it++)
	{
		ArticleDownloader* pArticleDownloader = *it;
		pArticleDownloader->LogDebugInfo();
	}
	m_mutexDownloadQueue.Unlock();

	debug("");

	g_pServerPool->LogDebugInfo();
}

void QueueCoordinator::ResetHangingDownloads()
{
	const int TimeOut = g_pOptions->GetTerminateTimeout();
	if (TimeOut == 0)
	{
		return;
	}

	m_mutexDownloadQueue.Lock();
	time_t tm = ::time(NULL);

	for (ActiveDownloads::iterator it = m_ActiveDownloads.begin(); it != m_ActiveDownloads.end();)
	{
		ArticleDownloader* pArticleDownloader = *it;
		if (tm - pArticleDownloader->GetLastUpdateTime() > TimeOut &&
		   pArticleDownloader->GetStatus() == ArticleDownloader::adRunning)
		{
			ArticleInfo* pArticleInfo = pArticleDownloader->GetArticleInfo();
			debug("Terminating hanging download %s", pArticleDownloader->GetInfoName());
			if (pArticleDownloader->Terminate())
			{
				error("Terminated hanging download %s", pArticleDownloader->GetInfoName());
				pArticleInfo->SetStatus(ArticleInfo::aiUndefined);
			}
			else
			{
				error("Could not terminate hanging download %s", BaseFileName(pArticleInfo->GetResultFilename()));
			}
			m_ActiveDownloads.erase(it);
			// it's not safe to destroy pArticleDownloader, because the state of object is unknown
			delete pArticleDownloader;
			it = m_ActiveDownloads.begin();
			continue;
		}
		it++;
	}                                              

	m_mutexDownloadQueue.Unlock();
}
