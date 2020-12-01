﻿
/*
Copyright (c) 2009-present Maximus5
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

#pragma once

#include "../common/defines.h"
#include "../common/CEStr.h"

extern BOOL gbInDisplayLastError;
int DisplayLastError(LPCTSTR asLabel, DWORD dwError = 0, DWORD dwMsgFlags = 0, LPCWSTR asTitle = nullptr, HWND hParent = nullptr);

// All window/gdi related code must be run in main thread
bool isMainThread();
void initMainThread();

const wchar_t* DupCygwinPath(LPCWSTR asWinPath, bool bAutoQuote, LPCWSTR asMntPrefix, CEStr& path);
LPCWSTR MakeWinPath(LPCWSTR asAnyPath, LPCWSTR pszMntPrefix, CEStr& szWinPath);
wchar_t* MakeStraightSlashPath(LPCWSTR asWinPath);
bool FixDirEndSlash(wchar_t* rsPath);

bool isKey(DWORD wp,DWORD vk);

// pszWords - '|'separated
void StripWords(wchar_t* pszText, const wchar_t* pszWords);

// pszCommentMark - for example L"#"
void StripLines(wchar_t* pszText, LPCWSTR pszCommentMark);
