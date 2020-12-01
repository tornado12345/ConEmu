﻿
/*
Copyright (c) 2016-present Maximus5
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

#include "ConEmu.h"
#include "Font.h"
#include "LngRc.h"
#include "OptionsClass.h"
#include "SetPgInfo.h"
#include "VConGroup.h"
#include "VirtualConsole.h"

CSetPgInfo::CSetPgInfo()
{
}

CSetPgInfo::~CSetPgInfo()
{
}

LRESULT CSetPgInfo::OnInitDialog(HWND hDlg, bool abInitial)
{
	CVirtualConsole* pVCon = nullptr;
	CVConGuard VCon;
	if (CVConGroup::GetActiveVCon(&VCon) >= 0)
		pVCon = VCon.VCon();

	SetDlgItemText(hDlg, tCurCmdLine, GetCommandLine());

	gpConEmu->UpdateProcessDisplay(TRUE);

	gpConEmu->UpdateSizes();

	FillFontInfo(hDlg);

	return 0;
}

void CSetPgInfo::OnPostLocalize(HWND hDlg)
{
	CVirtualConsole* pVCon = nullptr;
	CVConGuard VCon;
	if (CVConGroup::GetActiveVCon(&VCon) >= 0)
		pVCon = VCon.VCon();

	// Performance
	gpSetCls->Performance(gbPerformance, TRUE);

	if (pVCon)
	{
		FillConsoleMode(hDlg, pVCon->RCon());
	}
}

void CSetPgInfo::FillFontInfo(HWND hDlg)
{
	CFontPtr font;
	wchar_t szMain[32] = L"", szAlt[32] = L"";

	if (gpFontMgr->QueryFont(fnt_Normal, nullptr, font))
		swprintf_c(szMain, L"%ix%ix%i", font->m_LF.lfHeight, font->m_LF.lfWidth, font->m_tm.tmAveCharWidth);
	swprintf_c(szAlt, L"%ix%i", gpFontMgr->BorderFontHeight(), gpFontMgr->BorderFontWidth());

	SetDlgItemText(hDlg, tRealFontMain, szMain);
	SetDlgItemText(hDlg, tRealFontBorders, szAlt);
}

void CSetPgInfo::FillConsoleMode(HWND hDlg, CRealConsole* pRCon)
{
	// E.g. "xterm|BrPaste"
	wchar_t szFlags[128] = L"";
	pRCon->QueryTermModes(szFlags, countof(szFlags), true);

	// E.g. "In=x98, Out=x03"
	wchar_t szModes[80];
	pRCon->QueryRConModes(szModes, countof(szModes), true);

	// "Console states"
	CEStr lsLng; gpLng->getControl(IDC_CONSOLE_STATES, lsLng, L"Console states");

	// Final: "Console states (In=x98, Out=x03, win32)"
	const CEStr lsInfo(lsLng, L" (", szModes, L", ", szFlags, L")");
	SetDlgItemText(hDlg, IDC_CONSOLE_STATES, lsInfo);
}

void CSetPgInfo::FillCursorInfo(HWND hDlg, const ConsoleInfoArg* pInfo)
{
	wchar_t szCursor[64];
	swprintf_c(szCursor, L"%ix%i, %i %s",
		static_cast<int>(pInfo->crCursor.X), static_cast<int>(pInfo->crCursor.Y),
		pInfo->cInfo.dwSize, pInfo->cInfo.bVisible ? L"vis" : L"hid");
	SetDlgItemText(hDlg, tCursorPos, szCursor);
}
