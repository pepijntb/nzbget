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
#include "config.h"
#endif

#ifdef WIN32
#include "win32.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include "nzbget.h"
#include "LoggableFrontend.h"
#include "Log.h"

LoggableFrontend::LoggableFrontend()
{
	debug("Creating LoggableFrontend");

	m_iNeededLogEntries = 0;
	m_bSummary = false;
	m_bFileList = false;
}

void LoggableFrontend::Run()
{
	debug("Entering LoggableFrontend-loop");

	while (!IsStopped())
	{
		Update();
		usleep(200 * 1000);
	}
	// Printing the last messages
	Update();

	BeforeExit();

	debug("Exiting LoggableFrontend-loop");
}

void LoggableFrontend::Update()
{
	if (!PrepareData())
	{
		FreeData();
		return;
	}

	BeforePrint();

	Log::Messages* pMessages = LockMessages();
	if (!pMessages->empty())
	{
		Message* pFirstMessage = pMessages->front();
		int iStart = m_iNeededLogFirstID - pFirstMessage->GetID() + 1;
		if (iStart < 0)
		{
			PrintSkip();
			iStart = 0;
		}
		for (unsigned int i = (unsigned int)iStart; i < pMessages->size(); i++)
		{
			PrintMessage((*pMessages)[i]);
			m_iNeededLogFirstID = (*pMessages)[i]->GetID();
		}
	}
	UnlockMessages();

	PrintStatus();

	FreeData();

	fflush(stdout);
}

void LoggableFrontend::PrintMessage(Message * pMessage)
{
	const char* msg = pMessage->GetText();
	switch (pMessage->GetKind())
	{
		case Message::mkDebug:
			fprintf(stdout, "[DEBUG] %s\n", msg);
			break;
		case Message::mkError:
			fprintf(stdout, "[ERROR] %s\n", msg);
			break;
		case Message::mkWarning:
			fprintf(stdout, "[WARNING] %s\n", msg);
			break;
		case Message::mkInfo:
			fprintf(stdout, "[INFO] %s\n", msg);
			break;
	}
}

void LoggableFrontend::PrintSkip()
{
	fprintf(stdout, ".....\n");
}
