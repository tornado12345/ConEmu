﻿
/*
Copyright (c) 2014-present Maximus5
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#define HIDE_USE_EXCEPTION_INFO
#define SHOWDEBUGSTR

#include "Header.h"
#include "RConFiles.h"
#include "RealConsole.h"
#include "../common/WFiles.h"

CRConFiles::CRConFiles(CRealConsole* apRCon)
	: mp_RCon(apRCon)
{
}

CRConFiles::~CRConFiles()
{
}

LPCWSTR CRConFiles::GetFileFromConsole(LPCWSTR asSrc, CEStr& szFull)
{
	CEStr szWinPath;
	LPCWSTR pszWinPath = MakeWinPath(asSrc, mp_RCon ? mp_RCon->GetMntPrefix() : nullptr, szWinPath);
	if (!pszWinPath || !*pszWinPath)
	{
		_ASSERTE(pszWinPath && *pszWinPath);
		return nullptr;
	}

	if (IsFilePath(pszWinPath, true))
	{
		if (!FileExists(pszWinPath)) // otherwise it will cover directories too
			return nullptr;
		szFull.Attach(szWinPath.Detach());
	}
	else
	{
		CEStr szDir;
		LPCWSTR pszDir = mp_RCon->GetConsoleCurDir(szDir, true);
		// We may get empty dir here if we are in "~" subdir
		if (!pszDir || !*pszDir)
		{
			_ASSERTE(pszDir && *pszDir && wcschr(pszDir,L'/')==nullptr);
			return nullptr;
		}

		// Попытаться просканировать один-два уровеня подпапок
		bool bFound = FileExistSubDir(pszDir, pszWinPath, 1, szFull);

		if (!bFound)
		{
			// git diffs style, but with backslashes (they are converted already)
			// "a/src/ConEmu.cpp" and "b/src/ConEmu.cpp"
			if (pszWinPath[0] && (pszWinPath[1] == L'\\') && wcschr(L"abicwo", pszWinPath[0]))
			{
				LPCWSTR pszSlash = pszWinPath;
				while (!bFound && ((pszSlash = wcschr(pszSlash, L'\\')) != nullptr))
				{
					while (*pszSlash == L'\\')
						pszSlash++;
					if (!*pszSlash)
						break;
					bFound = FileExistSubDir(pszDir, pszSlash, 1, szFull);
					if (!bFound)
					{
						// Try to go to parent folder (useful while browsing git-diff-s)
						bFound = CheckParentFolders(pszDir, pszSlash, szFull);
					}
				}
			}
			else
			{
				// let's try to check some paths from #include
				// for example: #include "src/common/Common.h"
				LPCWSTR pszSlash = pszWinPath;
				while (*pszSlash == L'\\') pszSlash++;
				bFound = CheckParentFolders(pszDir, pszSlash, szFull);
			}
		}

		if (!bFound)
		{
			// If there is "src" subfolder in the current folder
			const wchar_t* predefined[] = {L"trunk", L"src", L"source", L"sources", nullptr};
			for (size_t i = 0; !bFound && predefined[i]; ++i)
			{
				CEStr szSrc(JoinPath(pszDir, predefined[i]));
				if (DirectoryExists(szSrc))
					bFound = FileExistSubDir(szSrc, pszWinPath, 1, szFull);
			}
		}

		if (!bFound)
		{
			return nullptr;
		}
	}

	if (!szFull.IsEmpty())
	{
		// "src\conemu\realconsole.cpp" --> "src\ConEmu\RealConsole.cpp"
		MakePathProperCase(szFull);
	}

	return szFull;
}

bool CRConFiles::CheckParentFolders(LPCWSTR asParentDir, LPCWSTR asFilePath, CEStr& szFull)
{
	bool bFound = false;

	// Try to go to parent folder (useful while browsing git-diff-s or #include-s)
	CEStr lsParent = asParentDir;
	MBoxAssert(lsParent.ms_Val && !wcschr(lsParent.ms_Val, L'/')); // WinPath is expected
	for (int i = 6; i >= 0; --i)
	{
		wchar_t* pszDirSlash = wcsrchr(lsParent.ms_Val, L'\\');
		// Stop on the network server's root and drive letter
		if (!pszDirSlash || (pszDirSlash - lsParent.ms_Val) <= 2 || *(pszDirSlash-1) == L':')
			break;
		*pszDirSlash = 0;
		// Does it exist?
		if ((bFound = FileExistSubDir(lsParent, asFilePath, 1, szFull)))
			break;
		// Don't try to go upper, if current folder already contains ".git" (root of the repo)
		if (i > 0)
		{
			CEStr szGit(JoinPath(lsParent, L".git"));
			if (DirectoryExists(szGit))
				break;
		}
	}

	return bFound;
}

void CRConFiles::ResetCache()
{
}
