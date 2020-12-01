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

#pragma once

#include "../common/MArray.h"
#include "../common/MMap.h"
#include "../common/MModule.h"

enum ProcessDpiAwareness
{
	Process_DPI_Unaware            = 0,
	Process_System_DPI_Aware       = 1,
	Process_Per_Monitor_DPI_Aware  = 2
};

enum MonitorDpiType
{
	MDT_Effective_DPI  = 0,
	MDT_Angular_DPI    = 1,
	MDT_Raw_DPI        = 2,
	MDT_Default        = MDT_Effective_DPI
};

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

struct DpiValue
{
public:
	int Ydpi = 96;
	int Xdpi = 96;

	enum class DpiSource
	{
		Default,
		WParam,
		Explicit,
		PerMonitor,
		StaticOverall,
	};
	DpiSource source_{};

public:
	DpiValue();
	DpiValue(int xDpi, int yDpi, DpiSource source = DpiSource::Explicit);

	DpiValue(const DpiValue& dpi) = default;
	DpiValue& operator=(const DpiValue& dpi) = default;
	DpiValue(DpiValue&& dpi) = default;
	DpiValue& operator=(DpiValue&& dpi) = default;

	~DpiValue();

	static DpiValue FromWParam(WPARAM wParam);

public:
	bool Equals(const DpiValue& dpi) const;
	bool operator==(const DpiValue& dpi) const;
	bool operator!=(const DpiValue& dpi) const;

	int GetDpi() const;

	void SetDpi(int xDpi, int yDpi, DpiSource source);
	void SetDpi(const DpiValue& dpi);
	void OnDpiChanged(WPARAM wParam);
};

struct CEStartupEnv;

class CDpiAware
{
public:
	static HRESULT SetProcessDpiAwareness();

	static void UpdateStartupInfo(CEStartupEnv* pStartEnv);

	static bool IsPerMonitorDpi();

	static int QueryDpi(HWND hWnd = nullptr, DpiValue* pDpi = nullptr);

	// if hWnd is nullptr - returns DC's dpi
	static int QueryDpiForWindow(HWND hWnd = nullptr, DpiValue* pDpi = nullptr);

	static DpiValue QueryDpiForRect(const RECT& rcWnd, MonitorDpiType dpiType = MDT_Default);
	static DpiValue QueryDpiForMonitor(HMONITOR hMon, MonitorDpiType dpiType = MDT_Default);

	// Dialog helper
	static void GetCenteredRect(HWND hWnd, RECT& rcCentered, HMONITOR hDefault = nullptr);
	static void CenterDialog(HWND hDialog);

protected:
	static MModule shCore_;
	static const MModule& GetShCore();
	static MModule user32_;
	static const MModule& GetUser32();

	typedef HRESULT (WINAPI* GetDpiForMonitor_t)(HMONITOR hMonitor, MonitorDpiType dpiType, UINT *dpiX, UINT *dpiY);
	static GetDpiForMonitor_t getDpiForMonitor_;
};

class CDynDialog;

class CDpiForDialog
{
protected:
	HWND mh_Dlg;

	LONG mn_InSet;

	DpiValue m_InitDpi;
	LOGFONT mlf_InitFont;
	DpiValue m_CurDpi;
	UINT mn_TemplateFontSize;
	int mn_CurFontHeight;
	LOGFONT mlf_CurFont;

	HFONT mh_OldFont, mh_CurFont;

	struct DlgItem
	{
		HWND h;
		RECT r;
	};
	MMap<int, MArray<DlgItem>*> m_Items;

	CDpiForDialog();
public:
	static bool Create(CDpiForDialog*& pHelper);

	~CDpiForDialog();

	bool Attach(HWND hWnd, HWND hCenterParent, CDynDialog* apDlgTemplate);

	bool SetDialogDPI(const DpiValue& newDpi, LPRECT lprcSuggested = nullptr);

	void Detach();

public:
	const DpiValue& GetCurDpi() const;
	bool ProcessDpiMessages(HWND hDlg, UINT nMsg, WPARAM wParam, LPARAM lParam);

protected:
	static MArray<DlgItem>* LoadDialogItems(HWND hDlg);
	int GetFontSizeForDpi(HDC hdc, int Ydpi);
};
