/*
 *  This file if part of nzbget
 *
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
#include <stdio.h>
#ifdef WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "nzbget.h"
#include "Util.h"

char* BaseFileName(const char* filename)
{
	char* p = (char*)strrchr(filename, PATH_SEPARATOR);
	char* p1 = (char*)strrchr(filename, ALT_PATH_SEPARATOR);
	if (p1)
	{
		if ((p && p < p1) || !p)
		{
			p = p1;
		}
	}
	if (p)
	{
		return p + 1;
	}
	else
	{
		return (char*)filename;
	}
}

#ifdef WIN32

// getopt for WIN32:
// from http://www.codeproject.com/cpp/xgetopt.asp
// Original Author:  Hans Dietrich (hdietrich2@hotmail.com)
// Released to public domain from author (thanks)
// Slightly modified by Andrei Prygounkov

char	*optarg;		// global argument pointer
int		optind = 0; 	// global argv index

int getopt(int argc, char *argv[], char *optstring)
{
	static char *next = NULL;
	if (optind == 0)
		next = NULL;

	optarg = NULL;

	if (next == NULL || *next == '\0')
	{
		if (optind == 0)
			optind++;

		if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0')
		{
			optarg = NULL;
			if (optind < argc)
				optarg = argv[optind];
			return -1;
		}

		if (strcmp(argv[optind], "--") == 0)
		{
			optind++;
			optarg = NULL;
			if (optind < argc)
				optarg = argv[optind];
			return -1;
		}

		next = argv[optind];
		next++;		// skip past -
		optind++;
	}

	char c = *next++;
	char *cp = strchr(optstring, c);

	if (cp == NULL || c == ':')
	{
		fprintf(stderr, "Invalid option %c", c);
		return '?';
	}

	cp++;
	if (*cp == ':')
	{
		if (*next != '\0')
		{
			optarg = next;
			next = NULL;
		}
		else if (optind < argc)
		{
			optarg = argv[optind];
			optind++;
		}
		else
		{
			fprintf(stderr, "Option %c needs an argument", c);
			return '?';
		}
	}

	return c;
}

DirBrowser::DirBrowser(const char* szPath)
{
	char szMask[MAX_PATH + 1];
	int len = strlen(szPath);
	if (szPath[len] == '\\' || szPath[len] == '/')
	{
		snprintf(szMask, MAX_PATH + 1, "%s*.*", szPath);
	}
	else
	{
		snprintf(szMask, MAX_PATH + 1, "%s%c*.*", szPath, (int)PATH_SEPARATOR);
	}
	szMask[MAX_PATH] = '\0';
	m_hFile = _findfirst(szMask, &m_FindData);
	m_bFirst = true;
}

DirBrowser::~DirBrowser()
{
	if (m_hFile != -1L)
	{
		_findclose(m_hFile);
	}
}

const char* DirBrowser::Next()
{
	bool bOK = false;
	if (m_bFirst)
	{
		bOK = m_hFile != -1L;
		m_bFirst = false;
	}
	else
	{
		bOK = _findnext(m_hFile, &m_FindData) == 0;
	}
	if (bOK)
	{
		return m_FindData.name;
	}
	return NULL;
}

#else

DirBrowser::DirBrowser(const char* szPath)
{
	m_pDir = opendir(szPath);
}

DirBrowser::~DirBrowser()
{
	if (m_pDir)
	{
		closedir(m_pDir);
	}
}

const char* DirBrowser::Next()
{
	if (m_pDir)
	{
		m_pFindData = readdir(m_pDir);
		if (m_pFindData)
		{
			return m_pFindData->d_name;
		}
	}
	return NULL;
}

#endif

void NormalizePathSeparators(char* szPath)
{
	for (char* p = szPath; *p; p++) 
	{
		if (*p == ALT_PATH_SEPARATOR) 
		{
			*p = PATH_SEPARATOR;
		}
	}
}

bool ForceDirectories(const char* szPath)
{
	char* szNormPath = strdup(szPath);
	NormalizePathSeparators(szNormPath);
	int iLen = strlen(szNormPath);
	if ((iLen > 0) && szNormPath[iLen-1] == PATH_SEPARATOR
#ifdef WIN32
		&& iLen > 3
#endif
		)
	{
		szNormPath[iLen-1] = '\0';
	}

	struct stat buffer;
	bool bOK = !stat(szNormPath, &buffer) && S_ISDIR(buffer.st_mode);
	if (!bOK
#ifdef WIN32
		&& strlen(szNormPath) > 2
#endif
		)
	{
		char* szParentPath = strdup(szNormPath);
		bOK = true;
		char* p = (char*)strrchr(szParentPath, PATH_SEPARATOR);
		if (p)
		{
#ifdef WIN32
			if (p - szParentPath == 2 && szParentPath[1] == ':' && strlen(szParentPath) > 2)
			{
				szParentPath[3] = '\0';
			}
			else
#endif
			{
				*p = '\0';
			}
			if (strlen(szParentPath) != strlen(szPath))
			{
				bOK = ForceDirectories(szParentPath);
			}
		}
		if (bOK)
		{
			mkdir(szNormPath, S_DIRMODE);
			bOK = !stat(szNormPath, &buffer) && S_ISDIR(buffer.st_mode);
		}
		free(szParentPath);
	}
	free(szNormPath);
	return bOK;
}

bool LoadFileIntoBuffer(const char* szFileName, char** pBuffer, int* pBufferLength)
{
    FILE* pFile = fopen(szFileName, "r");
    if (!pFile)
    {
        return false;
    }

    // obtain file size.
    fseek(pFile , 0 , SEEK_END);
    int iSize  = ftell(pFile);
    rewind(pFile);

    // allocate memory to contain the whole file.
    *pBuffer = (char*) malloc(iSize + 1);
    if (!*pBuffer)
    {
        return false;
    }

    // copy the file into the buffer.
    fread(*pBuffer, 1, iSize, pFile);

    fclose(pFile);

    (*pBuffer)[iSize] = 0;

    *pBufferLength = iSize + 1;

    return true;
}

bool SetFileSize(const char* szFilename, int iSize)
{
	bool bOK = false;
#ifdef WIN32
	FILE* pFile = fopen(szFilename, "a");
	if (pFile)
	{
		bOK = _chsize_s(pFile->_file, iSize) == 0;
		fclose(pFile);
	}
#else
	// create file
	FILE* pFile = fopen(szFilename, "a");
	if (pFile)
	{
		fclose(pFile);
	}
	// there no reliable function to expand file on POSIX, so we must try different approaches,
	// starting with the fastest one and hoping it will work
	// 1) set file size using function "truncate" (it is fast, if works)
	truncate(szFilename, iSize);
	// check if it worked
	pFile = fopen(szFilename, "a");
	if (pFile)
	{
		fseek(pFile, 0, SEEK_END);
		bOK = ftell(pFile) == iSize;
		if (!bOK)
		{
			// 2) truncate did not work, expanding the file by writing in it (it is slow)
			fclose(pFile);
			truncate(szFilename, 0);
			pFile = fopen(szFilename, "a");
			char c = '0';
			fwrite(&c, 1, iSize, pFile);
			bOK = ftell(pFile) == iSize;
		}
		fclose(pFile);
	}
#endif
	return bOK;
}

//replace bad chars in filename
void MakeValidFilename(char* szFilename, char cReplaceChar)
{
	char* p = szFilename;
	while (*p)
	{
		if (strchr("\\/:*?\"><'\n\r\t", *p))
		{
			*p = cReplaceChar;
		}
		p++;
	}

	// remove trailing points. they are not allowed in directory names on windows,
	// but we remove them on posix also, in a case the directory is accessed from windows via samba.
	for (int iLen = strlen(szFilename); iLen > 0 && szFilename[iLen - 1] == '.'; iLen--) 
	{
		szFilename[iLen - 1] = '\0';
	}
}

long long JoinInt64(unsigned int Hi, unsigned int Lo)
{
	return (((long long)Hi) << 32) + Lo;
}

void SplitInt64(long long Int64, unsigned int* Hi, unsigned int* Lo)
{
	*Hi = (unsigned int)(Int64 >> 32);
	*Lo = (unsigned int)Int64;
}

float EqualTime(_timeval* t1, _timeval* t2)
{
#ifdef WIN32
	return t1->time == t2->time && t1->millitm == t2->millitm;
#else
	return t1->tv_sec == t2->tv_sec && t1->tv_usec == t2->tv_usec;
#endif
}

bool EmptyTime(_timeval* t)
{
#ifdef WIN32
	return t->time == 0 && t->millitm == 0;
#else
	return t->tv_sec == 0 && t->tv_usec == 0;
#endif
}

float DiffTime(_timeval* t1, _timeval* t2)
{
#ifdef WIN32
	return ((t1->time - t2->time) + (t1->millitm - t2->millitm) / 1000.0);
#else
	return (float)((t1->tv_sec - t2->tv_sec) + (t1->tv_usec - t2->tv_usec) / 1000000.0);
#endif
}