/*
 *  This file is part of nzbget
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

/* Base64 decryption is taken from 
 *  Article "BASE 64 Decoding and Encoding Class 2003" by Jan Raddatz
 *  http://www.codeguru.com/cpp/cpp/algorithms/article.php/c5099/
 */

const static char BASE64_DEALPHABET [128] = 
	{
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0, //   0 -   9
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0, //  10 -  19
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0, //  20 -  29
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0, //  30 -  39
	 0,  0,  0, 62,  0,  0,  0, 63, 52, 53, //  40 -  49
	54, 55, 56, 57, 58, 59, 60, 61,  0,  0, //  50 -  59
	 0, 61,  0,  0,  0,  0,  1,  2,  3,  4, //  60 -  69
	 5,  6,  7,  8,  9, 10, 11, 12, 13, 14, //  70 -  79
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, //  80 -  89
	25,  0,  0,  0,  0,  0,  0, 26, 27, 28, //  90 -  99
	29, 30, 31, 32, 33, 34, 35, 36, 37, 38, // 100 - 109
	39, 40, 41, 42, 43, 44, 45, 46, 47, 48, // 110 - 119
	49, 50, 51,  0,  0,  0,  0,  0			// 120 - 127
	};

unsigned int DecodeByteQuartet(char* szInputBuffer, char* szOutputBuffer)
{
	unsigned int buffer = 0;

	if (szInputBuffer[3] == '=')
	{
		if (szInputBuffer[2] == '=')
		{
			buffer = (buffer | BASE64_DEALPHABET [(int)szInputBuffer[0]]) << 6;
			buffer = (buffer | BASE64_DEALPHABET [(int)szInputBuffer[1]]) << 6;
			buffer = buffer << 14;

			char* temp = (char*) &buffer;
			szOutputBuffer [0] = temp [3];
			
			return 1;
		}
		else
		{
			buffer = (buffer | BASE64_DEALPHABET [(int)szInputBuffer[0]]) << 6;
			buffer = (buffer | BASE64_DEALPHABET [(int)szInputBuffer[1]]) << 6;
			buffer = (buffer | BASE64_DEALPHABET [(int)szInputBuffer[2]]) << 6;
			buffer = buffer << 8;

			char* temp = (char*) &buffer;
			szOutputBuffer [0] = temp [3];
			szOutputBuffer [1] = temp [2];
			
			return 2;
		}
	}
	else
	{
		buffer = (buffer | BASE64_DEALPHABET [(int)szInputBuffer[0]]) << 6;
		buffer = (buffer | BASE64_DEALPHABET [(int)szInputBuffer[1]]) << 6;
		buffer = (buffer | BASE64_DEALPHABET [(int)szInputBuffer[2]]) << 6;
		buffer = (buffer | BASE64_DEALPHABET [(int)szInputBuffer[3]]) << 6; 
		buffer = buffer << 2;

		char* temp = (char*) &buffer;
		szOutputBuffer [0] = temp [3];
		szOutputBuffer [1] = temp [2];
		szOutputBuffer [2] = temp [1];

		return 3;
	}

	return -1;
}

unsigned int DecodeBase64(char* szInputBuffer, int iInputBufferLength, char* szOutputBuffer)
{
	unsigned int InputBufferIndex  = 0;
	unsigned int OutputBufferIndex = 0;
	unsigned int InputBufferLength = iInputBufferLength > 0 ? iInputBufferLength : strlen(szInputBuffer);

	char ByteQuartet [4];

	while (InputBufferIndex < InputBufferLength)
	{
		for (int i = 0; i < 4; i++)
		{
			ByteQuartet [i] = szInputBuffer[InputBufferIndex];

			// Ignore all characters except the ones in BASE64_ALPHABET
			if (!((ByteQuartet [i] >= 48 && ByteQuartet [i] <=  57) ||
				(ByteQuartet [i] >= 65 && ByteQuartet [i] <=  90) ||
				(ByteQuartet [i] >= 97 && ByteQuartet [i] <= 122) ||
				 ByteQuartet [i] == '+' || ByteQuartet [i] == '/' || ByteQuartet [i] == '='))
			{
				// Invalid character
				i--;
			}

			InputBufferIndex++;
		}

		OutputBufferIndex += DecodeByteQuartet(ByteQuartet, szOutputBuffer + OutputBufferIndex);
	}

	// OutputBufferIndex gives us the next position of the next decoded character
	// inside our output buffer and thus represents the number of decoded characters
	// in our buffer.
	return OutputBufferIndex;
}

/* END - Base64
*/

char* XmlEncode(const char* raw)
{
	// calculate the required outputstring-size based on number of xml-entities and their sizes
	int iReqSize = strlen(raw);
	for (const char* p = raw; *p; p++)
	{
		switch (*p)
		{
			case '>':
			case '<':
				iReqSize += 4;
				break;
			case '&':
				iReqSize += 5;
				break;
			case '\'':
			case '\"':
				iReqSize += 6;
				break;
		}
	}

	char* result = (char*)malloc(iReqSize + 1);

	// copy string
	char* output = result;
	for (const char* p = raw; ; p++)
	{
		switch (*p)
		{
			case '\0':
				goto BreakLoop;
			case '<':
				strcpy(output, "&lt;");
				output += 4;
				break;
			case '>':
				strcpy(output, "&gt;");
				output += 4;
				break;
			case '&':
				strcpy(output, "&amp;");
				output += 5;
				break;
			case '\'':
				strcpy(output, "&apos;");
				output += 6;
				break;
			case '\"':
				strcpy(output, "&quot;");
				output += 6;
				break;
			default:
				*output++ = *p;
				break;
		}
	}
BreakLoop:

	*output = '\0';

	return result;
}

void XmlDecode(char* raw)
{
	char* output = raw;
	for (char* p = raw;;)
	{
		switch (*p)
		{
			case '\0':
				goto BreakLoop;
			case '&':
				{
					p++;
					if (!strncmp(p, "lt;", 3))
					{
						*output++ = '<';
						p += 3;
					}
					else if (!strncmp(p, "gt;", 3))
					{
						*output++ = '>';
						p += 3;
					}
					else if (!strncmp(p, "amp;", 4))
					{
						*output++ = '&';
						p += 4;
					}
					else if (!strncmp(p, "apos;", 5))
					{
						*output++ = '\'';
						p += 5;
					}
					else if (!strncmp(p, "quot;", 5))
					{
						*output++ = '\"';
						p += 5;
					}
					else
					{
						// unknown entity
						*output++ = *(p-1);
						p++;
					}
					break;
				}
			default:
				*output++ = *p++;
				break;
		}
	}
BreakLoop:

	*output = '\0';
}

const char* FindTag(const char* szXml, const char* szTag, int* iValueLength)
{
	char szOpenTag[100];
	snprintf(szOpenTag, 100, "<%s>", szTag);
	szOpenTag[100-1] = '\0';

	char szCloseTag[100];
	snprintf(szCloseTag, 100, "</%s>", szTag);
	szCloseTag[100-1] = '\0';

	const char* pstart = strstr(szXml, szOpenTag);
	if (!pstart) return NULL;

	const char* pend = strstr(pstart, szCloseTag);
	if (!pend) return NULL;

	int iTagLen = strlen(szOpenTag);
	*iValueLength = pend - pstart - iTagLen;

	return pstart + iTagLen;
}

bool ParseTagValue(const char* szXml, const char* szTag, char* szValueBuf, int iValueBufSize, const char** pTagEnd)
{
	int iValueLen = 0;
	const char* szValue = FindTag(szXml, szTag, &iValueLen);
	if (!szValue)
	{
		return false;
	}
	int iLen = iValueLen < iValueBufSize ? iValueLen : iValueBufSize - 1;
	strncpy(szValueBuf, szValue, iLen);
	szValueBuf[iLen] = '\0';
	if (pTagEnd)
	{
		*pTagEnd = szValue + iValueLen;
	}
	return true;
}
