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

#include "ConsoleMain.h"
#include "ConEmuSrv.h"
#include "ExitCodes.h"
#include "UnicodeTest.h"


#include "ConsoleState.h"
#include "../common/Common.h"
#include "../common/ConsoleRead.h"
#include "../common/EnvVar.h"
#include "../common/StartupEnvEx.h"
#include "../common/UnicodeChars.h"
#include "../common/WCodePage.h"

void PrintConsoleInfo()
{
	//_ASSERTE(FALSE && "Continue to PrintConsoleInfo");
	HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);

	extern CEStartupEnv* gpStartEnv;
	CEStr osInfo;
	LoadStartupEnvEx::ToString(gpStartEnv, [](LPCWSTR asText, LPARAM lParam, bool bFirst, bool bNewLine)
		{
			CEStr* pOsInfo = (CEStr*)lParam;
			if (!pOsInfo->IsEmpty())
				return;
			pOsInfo->Set(asText);
		}, (LPARAM)&osInfo);
	_wprintf(osInfo);

	wchar_t szInfo[1024];
	CONSOLE_SCREEN_BUFFER_INFO csbi = {};
	CONSOLE_CURSOR_INFO ci = {};

	msprintf(szInfo, countof(szInfo), L"ConHWND=0x%08X, Class=\"", LODWORD(gState.realConWnd_));
	if (gState.realConWnd_)
		GetClassName(gState.realConWnd_, szInfo+lstrlen(szInfo), 255);
	lstrcpyn(szInfo+lstrlen(szInfo), L"\"\r\n", 4);
	_wprintf(szInfo);

	{
	CEStr gui(GetEnvVar(L"ConEmuPID"));
	if (gui.GetLen() > 64) gui.ms_Val[64] = 0;
	CEStr srv(GetEnvVar(L"ConEmuServerPID"));
	if (srv.GetLen() > 64) srv.ms_Val[64] = 0;
	msprintf(szInfo, countof(szInfo), L"GuiPID=%u, ConEmuPID=%s, ConEmuServerPID=%s\n", gState.conemuPid_, gui.c_str(L""), srv.c_str(L""));
	_wprintf(szInfo);
	}

	struct FONT_INFOEX
	{
		ULONG cbSize;
		DWORD nFont;
		COORD dwFontSize;
		UINT  FontFamily;
		UINT  FontWeight;
		WCHAR FaceName[LF_FACESIZE];
	};
	typedef BOOL (WINAPI* GetCurrentConsoleFontEx_t)(HANDLE hConsoleOutput, BOOL bMaximumWindow, FONT_INFOEX* lpConsoleCurrentFontEx);
	HMODULE hKernel = GetModuleHandle(L"kernel32.dll");
	GetCurrentConsoleFontEx_t _GetCurrentConsoleFontEx = (GetCurrentConsoleFontEx_t)(hKernel ? GetProcAddress(hKernel,"GetCurrentConsoleFontEx") : NULL);
	if (!_GetCurrentConsoleFontEx)
	{
		lstrcpyn(szInfo, L"Console font info: Not available\r\n", countof(szInfo));
	}
	else
	{
		FONT_INFOEX info = {sizeof(info)};
		if (!_GetCurrentConsoleFontEx(hOut, FALSE, &info))
		{
			msprintf(szInfo, countof(szInfo), L"Console font info: Failed, code=%u\r\n", GetLastError());
		}
		else
		{
			info.FaceName[LF_FACESIZE-1] = 0;
			msprintf(szInfo, countof(szInfo), L"Console font info: %u, {%ux%u}, %u, %u, \"%s\"\r\n",
				info.nFont, info.dwFontSize.X, info.dwFontSize.Y, info.FontFamily, info.FontWeight,
				info.FaceName);
		}
	}
	_wprintf(szInfo);

	{
	DWORD nInMode = 0, nOutMode = 0, nErrMode = 0;
	BOOL bIn = GetConsoleMode(hIn, &nInMode);
	BOOL bOut = GetConsoleMode(hOut, &nOutMode);
	BOOL bErr = GetConsoleMode(hErr, &nErrMode);
	wchar_t szIn[20], szOut[20], szErr[20];
	msprintf(szIn, countof(szIn), L"x%X", nInMode);
	msprintf(szOut, countof(szOut), L"x%X", nOutMode);
	msprintf(szErr, countof(szErr), L"x%X", nErrMode);
	msprintf(szInfo, countof(szInfo), L"Handles: In=x%X (Mode=%s) Out=x%X (Mode=%s) Err=x%X (Mode=%s)\r\n",
		LODWORD(hIn), bIn?szIn:L"-1", LODWORD(hOut), bOut?szOut:L"-1", LODWORD(hErr), bErr?szErr:L"-1");
	_wprintf(szInfo);
	}

	if (GetConsoleScreenBufferInfo(hOut, &csbi))
		msprintf(szInfo, countof(szInfo), L"Buffer={%i,%i} Window={%i,%i}-{%i,%i} MaxSize={%i,%i}\r\n",
			csbi.dwSize.X, csbi.dwSize.Y,
			csbi.srWindow.Left, csbi.srWindow.Top, csbi.srWindow.Right, csbi.srWindow.Bottom,
			csbi.dwMaximumWindowSize.X, csbi.dwMaximumWindowSize.Y);
	else
		msprintf(szInfo, countof(szInfo), L"GetConsoleScreenBufferInfo failed, code=%u\r\n",
			GetLastError());
	_wprintf(szInfo);

	if (GetConsoleCursorInfo(hOut, &ci))
		msprintf(szInfo, countof(szInfo), L"Cursor: Pos={%i,%i} Size=%i%% %s\r\n",
			csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y, ci.dwSize, ci.bVisible ? L"Visible" : L"Hidden");
	else
		msprintf(szInfo, countof(szInfo), L"GetConsoleCursorInfo failed, code=%u\r\n",
			GetLastError());
	_wprintf(szInfo);

	DWORD nCP = GetConsoleCP();
	DWORD nOutCP = GetConsoleOutputCP();
	CPINFOEX cpinfo = {};
	DWORD dwLayout = 0, dwLayoutRc = -1;
	SetLastError(0);
	gpWorker->IsKeyboardLayoutChanged(dwLayout, &dwLayoutRc);

	msprintf(szInfo, countof(szInfo), L"ConsoleCP=%u, ConsoleOutputCP=%u, Layout=%08X (%s errcode=%u)\r\n",
		nCP, nOutCP, dwLayout, dwLayoutRc ? L"failed" : L"OK", dwLayoutRc);
	_wprintf(szInfo);

	for (UINT i = 0; i <= 1; i++)
	{
		if (i && (nOutCP == nCP))
			break;

		if (!GetCPInfoEx(i ? nOutCP : nCP, 0, &cpinfo))
		{
			msprintf(szInfo, countof(szInfo), L"GetCPInfoEx(%u) failed, code=%u\r\n", nCP, GetLastError());
		}
		else
		{
			msprintf(szInfo, countof(szInfo),
				L"CP%u: Max=%u "
				L"Def=x%02X,x%02X UDef=x%X\r\n"
				L"  Lead=x%02X,x%02X,x%02X,x%02X,x%02X,x%02X,x%02X,x%02X,x%02X,x%02X,x%02X,x%02X\r\n"
				L"  Name=\"%s\"\r\n",
				cpinfo.CodePage, cpinfo.MaxCharSize,
				cpinfo.DefaultChar[0], cpinfo.DefaultChar[1], cpinfo.UnicodeDefaultChar,
				cpinfo.LeadByte[0], cpinfo.LeadByte[1], cpinfo.LeadByte[2], cpinfo.LeadByte[3], cpinfo.LeadByte[4], cpinfo.LeadByte[5],
				cpinfo.LeadByte[6], cpinfo.LeadByte[7], cpinfo.LeadByte[8], cpinfo.LeadByte[9], cpinfo.LeadByte[10], cpinfo.LeadByte[11],
				cpinfo.CodePageName);
		}
		_wprintf(szInfo);
	}
}

// Returns:
// * CERR_UNICODE_CHK_OKAY(142), if RealConsole supports
//   unicode characters output.
// * CERR_UNICODE_CHK_FAILED(141), if RealConsole CAN'T
//   output/store unicode characters.
// This function is called by: ConEmuC.exe /CHECKUNICODE
int CheckUnicodeFont()
{
	// Print version and console information first
	PrintConsoleInfo();

	_ASSERTE(FALSE && "Continue to CheckUnicodeFont");

	int iRc = CERR_UNICODE_CHK_FAILED;

	HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);

	wchar_t szText[80] = UnicodeTestString;
	wchar_t szColor[32] = ColorTestString;

	CHAR_INFO cRead[80] = {};
	WORD aRead[80] = {};
	wchar_t sAttrRead[80] = {}, sAttrBlock[80] = {};
	wchar_t szInfo[1024]; DWORD nTmp;
	wchar_t szCheck[80] = L"", szBlock[80] = L"";
	BOOL bInfo = FALSE, bWrite = FALSE, bRead = FALSE, bReadAttr = FALSE, bCheck = FALSE, bBlkRead = FALSE;
	DWORD nLen = lstrlen(szText), nWrite = 0, nRead = 0, nErr = 0;
	WORD nDefColor = 7;
	DWORD nColorLen = lstrlen(szColor);
	CONSOLE_SCREEN_BUFFER_INFO csbi = {}, csbi2 = {};
	CONSOLE_CURSOR_INFO ci = {};
	_ASSERTE(nLen<=35); // ниже на 2 буфер множится

	DWORD nCurOutMode = 0;
	GetConsoleMode(hOut, &nCurOutMode);
	SetConsoleMode(hOut, ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT);


	struct WriteInvoke
	{
		HANDLE out;
		DWORD  written;
		BOOL   result;

		void operator()(const wchar_t* txt, int len = -1)
		{
			result = WriteConsoleW(out, txt, (len < 0) ? lstrlen(txt) : len, &written, NULL);
		};
	} write = {hOut};


	if (GetConsoleScreenBufferInfo(hOut, &csbi))
		nDefColor = csbi.wAttributes;

	// Simlify checking of ConEmu's "colorization"
	write(L"\r\n");
	WORD nColor = 7;
	for (DWORD n = 0; n < nColorLen; n++)
	{
		SetConsoleTextAttribute(hOut, nColor);
		write(szColor+n, 1);
		nColor++; if (nColor == 16) nColor = 7;
	}
	SetConsoleTextAttribute(hOut, 7);
	write(L"\r\n");

	// Test of "Reverse video"
	GetConsoleScreenBufferInfo(hOut, &csbi);
	const WORD N = 0x07, H = N|COMMON_LVB_REVERSE_VIDEO, E = 0x2007;
	// "Normal" + ' ' + "Reverse"
	CHAR_INFO cHighlight[] = {{{'N'},N},{{'o'},N},{{'r'},N},{{'m'},N},{{'a'},N},{{'l'},N},
		{{' '},N},
		{{'R'},H},{{'e'},H},{{'v'},H},{{'e'},H},{{'r'},H},{{'s'},H},{{'e'},H},
		{{' '},N},
		{{'E'},E},{{'x'},E},{{'t'},E},{{'r'},E},{{'a'},E},
		{{' '},N}};
	COORD crHiSize = {countof(cHighlight), 1}, cr0 = {};
	COORD crBeforePos = csbi.dwCursorPosition, crAfterPos = {countof(cHighlight), csbi.dwCursorPosition.Y};
	SMALL_RECT srHiPos = {0, csbi.dwCursorPosition.Y, crHiSize.X-1, csbi.dwCursorPosition.Y};
	SetConsoleCursorPosition(hOut, crAfterPos);
	WriteConsoleOutputW(hOut, cHighlight, crHiSize, cr0, &srHiPos);
	GetConsoleScreenBufferInfo(hOut, &csbi);
	ReadConsoleOutputAttribute(hOut, aRead, countof(cHighlight), crBeforePos, &nTmp);
	msprintf(szInfo, countof(szInfo), L"--> x%X x%X x%X", static_cast<UINT>(aRead[0]), static_cast<UINT>(aRead[7]), static_cast<UINT>(aRead[15]));
	write(szInfo);
	write(L"\r\n");
	struct { const wchar_t* Label; WORD Attr; } AttrTests[] = {
		{L"LVert", N|COMMON_LVB_GRID_LVERTICAL},
		{L"RVert", N|COMMON_LVB_GRID_RVERTICAL},
		{L"Extra", N|0x2000},
		{L"Under", N|COMMON_LVB_UNDERSCORE},
		{L"Horz", N|COMMON_LVB_GRID_HORIZONTAL},
		{}
	};
	for (size_t i = 0; AttrTests[i].Label; ++i)
	{
		SetConsoleTextAttribute(hOut, AttrTests[i].Attr);
		msprintf(szInfo, countof(szInfo), L"%s:x%04X", AttrTests[i].Label, static_cast<UINT>(AttrTests[i].Attr));
		write(szInfo);
		SetConsoleTextAttribute(hOut, N);
		write(L" ");
	}
	SetConsoleTextAttribute(hOut, N);
	write(L"\r\n");

	write(L"\r\n" L"Check ");


	if ((bInfo = GetConsoleScreenBufferInfo(hOut, &csbi)) != FALSE)
	{
		// First check console cursor offset, szText contains 5 CJK glyphs
		if (WriteConsoleW(hOut, szText, nLen, &nWrite, NULL))
		{
			GetConsoleScreenBufferInfo(hOut, &csbi2);
		}
		// And continue to attribute check
		COORD cr0 = {0,0};
		WORD intens[17] = {}; for (size_t i = 0; i < countof(intens); ++i) intens[i] = N|FOREGROUND_INTENSITY; intens[countof(intens)-1] = 1;
		WriteConsoleOutputAttribute(hOut, intens, countof(intens), csbi.dwCursorPosition, &nWrite);
		if ((bWrite = WriteConsoleOutputCharacterW(hOut, szText, nLen, csbi.dwCursorPosition, &nWrite)) != FALSE)
		{
			if (((bRead = ReadConsoleOutputCharacterW(hOut, szCheck, nLen*2, csbi.dwCursorPosition, &nRead)) != FALSE)
				/*&& (nRead == nLen)*/)
			{
				bCheck = (memcmp(szText, szCheck, nLen*sizeof(szText[0])) == 0);
				if (bCheck)
				{
					iRc = CERR_UNICODE_CHK_OKAY;
				}
			}

			bReadAttr = ReadConsoleOutputAttribute(hOut, aRead, nLen*2, csbi.dwCursorPosition, &nTmp);

			COORD crBufSize = {(SHORT)sizeof(cRead),1};
			SMALL_RECT rcRead = {csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y, csbi.dwCursorPosition.X+(SHORT)nLen*2, csbi.dwCursorPosition.Y};
			bBlkRead = ReadConsoleOutputW(hOut, cRead, crBufSize, cr0, &rcRead);
		}
	}

	nErr = GetLastError();

	write(L"\r\n"); // don't overwrite the text

	write(L"Text: "); write(szText); write(L"\r\n");

	wchar_t* ptr;
	write(L"Read:");
	ptr = szInfo;
	for (UINT i = 0; i < nLen*2; i++)
	{
		if (szCheck[i] <= L' ')
			break;
		msprintf(ptr, countof(szInfo)-(ptr-szInfo), L" %c:x%X", szCheck[i], aRead[i]);
		ptr += lstrlen(ptr);
	}
	*ptr = 0;
	write(szInfo);
	write(L"\r\n");
	write(L"Blck:");
	ptr = szInfo;
	for (UINT i = 0; i < nLen*2; i++)
	{
		if (cRead[i].Char.UnicodeChar <= L' ')
			break;
		msprintf(ptr, countof(szInfo)-(ptr-szInfo), L" %c:x%X", cRead[i].Char.UnicodeChar, cRead[i].Attributes);
		ptr += lstrlen(ptr);
	}
	*ptr = 0;
	write(szInfo);
	write(L"\r\n");


	int cursor_diff = (csbi2.dwCursorPosition.X - (csbi.dwCursorPosition.X + nLen));
	msprintf(szInfo, countof(szInfo),
		L"Info: %u,%u,%u,%u %u,%u,%u,%u  Cursor={%i,%i}->{%i,%i} [%c%i]\r\n"
		L"\r\n",
		nErr, bInfo, bWrite, nWrite,
		bRead, bReadAttr, nRead, bBlkRead,
		csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y, csbi2.dwCursorPosition.X, csbi2.dwCursorPosition.Y,
		(cursor_diff >= 0) ? L'+' : L'?', cursor_diff
		);
	write(szInfo);

	// #WIN10 Unexpected glyphs width in Win10 - szMid line displayed narrower than console width
	wchar_t szUpp[] = {ucBoxDblDownRight, ucBoxDblHorz, ucBoxDblDownDblHorz, ucBoxDblDownLeft, 0};
	wchar_t szMid[] = {ucBoxDblVert, ucSpace, ucBoxDblVert, ucBoxDblVert, 0};
	wchar_t szExt[] = {ucBoxDblVertRight, ucSpace, ucBoxDblUpDblHorz, ucBoxDblVertLeft, 0};
	wchar_t szBot[] = {ucBoxDblUpRight, ucBoxDblHorz, ucBoxDblUpDblHorz, ucBoxDblUpLeft, 0};
	wchar_t szChs[] = L"中文";
	int nMid = csbi.dwSize.X / 2;
	int nDbcsShift = IsConsoleDoubleCellCP() ? 2 : 0;
	msprintf(szInfo, countof(szInfo),
		L"Width: %i, Mid: %i, DblCell: %i\r\n",
		csbi.dwSize.X, nMid, IsConsoleDoubleCellCP() ? 1 : 0);
	write(szInfo);
	SetConsoleMode(hOut, ENABLE_PROCESSED_OUTPUT);
	write(szUpp, 1);
		for (int X = 2; X < nMid; X++) write(szUpp+1, 1);
		write(szUpp+2, 1);
		for (int X = nMid+1; X < csbi.dwSize.X; X++) write(szUpp+1, 1);
		write(szUpp+3, 1); // the cursor is expected to be stuck at the end of console line
		write(szUpp+3, 1); //
		write(L"\r\n", 2); // and only now it shall goes to the begginning of the new line
	SetConsoleMode(hOut, ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT);
	// Here are we write four CJK glyphs. Most weird and tricky because of inconsitence of CJK support in various Windows versions...
	write(szMid, 1);
		write(szChs, 2);
		for (int X = 4; X < nMid - nDbcsShift; X++) write(szMid+1, 1);
		write(szMid+2, 1);
		write(szChs, 1);
		for (int X = nMid+3; X < csbi.dwSize.X - nDbcsShift; X++) write(szMid+1, 1);
		write(szChs+1, 1);
		write(szMid+3, 1);
	// Check for ENABLE_WRAP_AT_EOL_OUTPUT flag, if it works properly,
	// AND "\r" does not go to new line, we expect that szExt will be
	// overwritten by szBot completely.
	write(szExt, 1);
		for (int X = 2; X < nMid; X++) write(szBot+1, 1);
		write(szExt+2, 1);
		for (int X = nMid+1; X < csbi.dwSize.X; X++) write(szBot+1, 1);
		SetConsoleMode(hOut, ENABLE_PROCESSED_OUTPUT);
		write(L"#", 1); // this should be just overwritten by next (szExt+3)
		write(szExt+3, 1);
		write(L"\r", 1);
	SetConsoleMode(hOut, ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT);
	write(szBot, 1);
		for (int X = 2; X < nMid; X++) write(szBot+1, 1);
		write(szBot+2, 1);
		for (int X = nMid+1; X < csbi.dwSize.X; X++) write(szBot+1, 1);
		write(szBot+3, 1);
	write(L"\r\n");


	write(bCheck ? L"Unicode check succeeded" :
		bRead ? L"Unicode check FAILED!"
		: L"Unicode is not supported in this console!");
	write(L"\r\n");

	return iRc;
}

// Unit test for internal purpose, it utilizes CpCvt
// to convert series of UTF-8 characters into unicode
// This function is called by: ConEmuC.exe /TESTUNICODE
int TestUnicodeCvt()
{
	wchar_t szInfo[140]; DWORD nWrite;
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

	// Some translations check
	{
		char chUtf8[] =
			"## "
			"\x41\xF0\x9D\x94\xB8\x41" // 'A', double-struck capital A as surrogate pair, 'A'
			" # "
			"\xE9\xE9\xE9"             // Invalid unicode translations
			" # "
			"\xE6\x96\x87\xE6\x9C\xAC" // Chinese '文' and '本'
			"\xEF\xBC\x8E"             // Fullwidth Full Stop
			" ##\r\n"
			;
		CpCvt cvt = {}; wchar_t wc; char* pch = chUtf8;
		cvt.SetCP(65001);

		msprintf(szInfo, countof(szInfo),
			L"CVT test: CP=%u Max=%u Def=%c: ",
			cvt.CP.CodePage, cvt.CP.MaxCharSize, cvt.CP.UnicodeDefaultChar
			);
		WriteConsoleW(hOut, szInfo, lstrlen(szInfo), &nWrite, NULL);

		while (*pch)
		{
			switch (cvt.Convert(*(pch++), wc))
			{
			case ccr_OK:
			case ccr_BadUnicode:
				WriteConsoleW(hOut, &wc, 1, &nWrite, NULL);
				break;
			case ccr_Surrogate:
			case ccr_BadTail:
			case ccr_DoubleBad:
				WriteConsoleW(hOut, &wc, 1, &nWrite, NULL);
				cvt.GetTail(wc);
				WriteConsoleW(hOut, &wc, 1, &nWrite, NULL);
				break;
			}
		}
	}

	return 0;
}
