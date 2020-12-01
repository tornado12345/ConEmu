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

#include "RefRelease.h"
#include "../common/RConStartArgsEx.h"
#include "../common/MArray.h"
#include <functional>

class CVirtualConsole;
class CVConGuard;
class CTabID;
struct AppSettings;


// used with enum EnumVConFlags, callback for EnumVCon
using EnumVConProc = std::function<bool(CVirtualConsole* pVCon, LPARAM lParam)>;


class CVConGroup : public CRefRelease
{
protected:
	CVirtualConsole* mp_Item;     // консоль, к которой привязан этот "Pane"
	RConStartArgsEx::SplitType m_SplitType; // eSplitNone/eSplitHorz/eSplitVert
	UINT mn_SplitPercent10; // (0.1% - 99.9%)*10
	CVConGroup *mp_Grp1, *mp_Grp2; // Ссылки на "дочерние" панели
	CVConGroup *mp_Parent; // Ссылка на "родительскую" панель
	long mb_Released;
	void RemoveGroup();
	RECT mrc_Full;
	RECT mrc_Splitter;
	RECT mrc_DragSplitter;
	bool mb_ResizeFlag; // взводится в true для корня, когда в группе что-то меняется
	bool mb_GroupInputFlag; // Одновременный ввод во все сплиты (не гарантируется для ChildGui)
	bool mb_PaneMaximized; // Активный сплит группы отображается "развернутым"
	void* mp_ActiveGroupVConPtr; // указатель (CVirtualConsole*) на последнюю активную консоль в этой группе

	void SetResizeFlags();

	CVConGroup* GetRootGroup() const;
	static CVConGroup* GetRootOfVCon(CVirtualConsole* apVCon);
	CVConGroup* GetAnotherGroup() const;
	void MoveToParent(CVConGroup* apParent);
	void RepositionVCon(RECT rcNewCon, bool bVisible);
	void CalcSplitRect(UINT nSplitPercent10, RECT rcNewCon, RECT& rcCon1, RECT& rcCon2, RECT& rcSplitter) const;
	void CalcSplitRootRect(RECT rcAll, RECT& rcCon, const CVConGroup* pTarget = nullptr) const;
	#if 0
	void CalcSplitConSize(COORD size, COORD& sz1, COORD& sz2);
	#endif
	void ShowAllVCon(int nShowCmd);
	static void ShowActiveGroup(CVirtualConsole* pOldActive);

	void GetAllTextSize(SIZE& sz, SIZE& splits, bool abMinimal = false);
	void SetConsoleSizes(const COORD& size, const RECT& rcNewCon, bool abSync);

	void StoreActiveVCon(CVirtualConsole* pVCon);
	bool ReSizeSplitter(int iCells);
	void OnPaintSplitter(HDC hdc, HBRUSH hbr);

	CVConGroup* FindNextPane(const RECT& rcPrev, int nHorz /*= 0*/, int nVert /*= 0*/) const;

	static CVConGroup* mp_GroupSplitDragging;
	static LPARAM ReSizeSplitterHelper(LPARAM lParam);
	static CVConGroup* FindSplitGroup(POINT ptWork, CVConGroup* pFrom);
	static bool isGroupVisible(CVConGroup* pGrp);
	static void StopSplitDragging();

private:
	static CVirtualConsole* CreateVCon(RConStartArgsEx& args, CVirtualConsole*& ppVConI, int index);

	static CVConGroup* CreateVConGroup();
	CVConGroup* SplitVConGroup(RConStartArgsEx::SplitType aSplitType = RConStartArgsEx::eSplitHorz/*eSplitVert*/, UINT anPercent10 = 500);

	void PopulateSplitPanes(MArray<CVConGuard*>& VCons) const;
	CVConGroup* GetLeafLeft() const;
	CVConGroup* GetLeafRight() const;

public:
	// Если rPanes==nullptr - просто вернуть количество сплитов
	int GetGroupPanes(MArray<CVConGuard*>* rPanes);
	static void FreePanesArray(MArray<CVConGuard*> &rPanes);
private:
	static bool CloseQuery(MArray<CVConGuard*>* rpPanes, bool* rbMsgConfirmed /*= nullptr*/, bool bForceKill = false, bool bNoGroup = false);

	CVConGroup(CVConGroup *apParent);

protected:
	virtual ~CVConGroup();
	virtual void FinalRelease() override;

protected:
	friend class CVConGuard;
	static bool setRef(CVirtualConsole*& rpRef, CVirtualConsole* apVCon);
	static void setActiveVConAndFlags(CVirtualConsole* apNewVConActive);

public:
	enum CloseConsoleMode
	{
		CloseSimple,
		CloseZombie,
		Close2Right,
	};
public:
	static void Initialize();
	static void Deinitialize();
	static CVirtualConsole* CreateCon(RConStartArgsEx& args, bool abAllowScripts = false, bool abForceCurConsole = false);
	static void OnVConDestroyed(CVirtualConsole* apVCon);

	static bool InCreateGroup();
	static void OnCreateGroupBegin();
	static void OnCreateGroupEnd();

public:
	static bool isActiveGroupVCon(CVirtualConsole* pVCon);
	static bool isValid(CRealConsole* apRCon);
	static bool isValid(CVirtualConsole* apVCon);
	static bool isVConExists(int nIdx);
	static bool isInGroup(CVirtualConsole* apVCon, CVConGroup* apGroup);
	static bool isGroup(CVirtualConsole* apVCon, CVConGroup** rpRoot = nullptr, CVConGuard* rpActiveVCon = nullptr);
	static bool isConSelectMode();
	static bool isInCreateRoot();
	static bool isDetached();
	static bool isFilePanel(bool abPluginAllowed=false);
	static bool isNtvdm(BOOL abCheckAllConsoles=FALSE);
	static bool isOurConsoleWindow(HWND hCon);
	static bool isOurGuiChildWindow(HWND hWnd);
	static bool isOurWindow(HWND hAnyWnd);
	static bool isChildWindowVisible();
	static bool isPictureView();
	static bool isEditor();
	static bool isViewer();
	static bool isFar(bool abPluginRequired=false);
	static int isFarExist(CEFarWindowType anWindowType=fwt_Any, LPWSTR asName=nullptr, CVConGuard* rpVCon=nullptr);
	static bool isVConHWND(HWND hChild, CVConGuard* rpVCon = nullptr);
	static bool isConsolePID(DWORD nPID);
	static DWORD GetFarPID(bool abPluginRequired = false);
	static void CheckTabValid(CTabID* apTab, bool& rbVConValid, bool& rbPidValid, bool& rbPassive);

	static bool EnumVCon(EnumVConFlags what, EnumVConProc pfn, LPARAM lParam);

	static int  GetActiveVCon(CVConGuard* pVCon = nullptr, int* pAllCount = nullptr);
	static int  GetVConIndex(CVirtualConsole* apVCon);
	static bool GetVCon(int nIdx, CVConGuard* pVCon = nullptr, bool bFromCycle = false);
	static bool GetVConFromPoint(POINT ptScreen, CVConGuard* pVCon = nullptr);
	static bool GetProgressInfo(short& pnProgress, bool& pbActiveHasProgress, AnsiProgressStatus& state);

	static void StopSignalAll();
	static void DestroyAllVCon();
	static void OnRConTimerCheck();
	static void OnAlwaysShowScrollbar(bool abSync = true);
	static void OnUpdateScrollInfo();
	static void OnUpdateFarSettings();
	static void OnUpdateTextColorSettings(bool ChangeTextAttr = true, bool ChangePopupAttr = true, const AppSettings* apDistinct = nullptr);
	static bool OnCloseQuery(bool* rbMsgConfirmed = nullptr);
	static bool DoCloseAllVCon(bool bMsgConfirmed = false);
	static void CloseAllButActive(CVirtualConsole* apVCon/*may be null*/, CloseConsoleMode closeMode, bool abNoConfirm);
	static void CloseGroup(CVirtualConsole* apVCon/*may be null*/, bool abKillActiveProcess = false);
	static void OnDestroyConEmu();
	static void OnVConClosed(CVirtualConsole* apVCon);
	static void OnUpdateProcessDisplay(HWND hInfo);
	static void OnDosAppStartStop(HWND hwnd, StartStopType sst, DWORD idChild);
	static void UpdateWindowChild(CVirtualConsole* apVCon);
	//static void RePaint();
	static void Update(bool isForce = false);
	static HWND DoSrvCreated(const DWORD nServerPID, const HWND hWndCon, const DWORD dwKeybLayout, DWORD& t1, DWORD& t2, int& iFound, CESERVER_REQ_SRVSTARTSTOPRET& pRet);
	static void OnVConCreated(CVirtualConsole* apVCon, const RConStartArgsEx *args);
	static void OnGuiFocused(bool abFocus, bool abForceChild = FALSE);

	static void ResetGroupInput(CConEmuMain* pOwner, GroupInputCmd cmd);
	static void GroupInput(CVirtualConsole* apVCon, GroupInputCmd cmd);
	static void GroupSelectedInput(CVirtualConsole* apVCon);

	static bool Activate(CVirtualConsole* apVCon);
	static void MoveActiveTab(CVirtualConsole* apVCon, bool bLeftward);

	static bool ExchangePanes(CVirtualConsole* apVCon, int nHorz = 0, int nVert = 0);
	static bool ActivateNextPane(CVirtualConsole* apVCon, int nHorz = 0, int nVert = 0);
	static bool PaneActivateNext(bool abNext);
	static void PaneMaximizeRestore(CVirtualConsole* apVCon);
	static void ReSizePanes(RECT workspace);
	static bool ReSizeSplitter(CVirtualConsole* apVCon, int iHorz = 0, int iVert = 0);

	static void OnUpdateGuiInfoMapping(ConEmuGuiMapping* apGuiInfo);
	static void OnPanelViewSettingsChanged();
	static void OnTaskbarSettingsChanged();
	static void OnTaskbarCreated();

	static void MoveAllVCon(CVirtualConsole* pVConCurrent, RECT rcNewCon);
	static HRGN GetExclusionRgn();
	static void OnConActivated(CVirtualConsole* pVCon);
	static bool ConActivate(int nCon);
	private:
	static bool ConActivate(CVConGuard& VCon, int nCon);
	public:
	static bool ConActivateByName(LPCWSTR asName);
	static bool ConActivateNext(bool abNext);
	static DWORD CheckProcesses();
	static CRealConsole* AttachRequestedGui(DWORD anServerPID, LPCWSTR asAppFileName, DWORD anAppPID);
	static BOOL AttachRequested(HWND ahConWnd, const CESERVER_REQ_STARTSTOP* pStartStop, CESERVER_REQ_SRVSTARTSTOPRET& pRet);
	static int GetConCount(bool bNoDetached = false);
	static int ActiveConNum();
	static bool GetVConBySrvPID(DWORD anServerPID, DWORD anMonitorTID, CVConGuard* pVCon = nullptr);
	static bool GetVConByHWND(HWND hConWnd, HWND hDcWnd, CVConGuard* pVCon = nullptr);
	static bool GetVConByName(LPCWSTR asName, CVConGuard* rpVCon = nullptr);

	static void LogString(LPCSTR asText);
	static void LogString(LPCWSTR asText);
	static void LogInput(UINT uMsg, WPARAM wParam, LPARAM lParam, LPCWSTR pszTranslatedChars = nullptr);

	static RECT CalcRect(enum ConEmuRect tWhat, RECT rFrom, enum ConEmuRect tFrom, CVirtualConsole* pVCon, enum ConEmuMargins tTabAction=CEM_TAB);
	static bool PreReSize(unsigned WindowMode, RECT rcWnd, enum ConEmuRect tFrom = CER_MAIN, bool bSetRedraw = false);
	static void SyncWindowToConsole(); // -- функция пустая, игнорируется
	static void SyncConsoleToWindow(LPRECT prcNewWnd=nullptr, bool bSync=false);
	static void LockSyncConsoleToWindow(bool abLockSync);
	static void SetAllConsoleWindowsSize(RECT rcWorkspace, COORD size, bool bSetRedraw /*= false*/);
	static void SyncAllConsoles2Window(RECT rcWorkspace, bool bSetRedraw = false);
	static void OnConsoleResize(bool abSizingToDo);

	static LRESULT OnMouseEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	static RConStartArgsEx::SplitType isSplitterDragging();

	static void NotifyChildrenWindows();

	static void SetRedraw(bool abRedrawEnabled);
	static void Redraw();
	static void InvalidateGaps();
	static void OnPaintGaps(HDC hDC);
	static void InvalidateAll();

	static bool OnFlashWindow(DWORD nOpt, DWORD nFlags, DWORD nCount, HWND hCon);

	static void ExportEnvVarAll(CESERVER_REQ* pIn, CRealConsole* pExceptRCon);

	//// Это некие сводные размеры, соответствующие тому, как если бы была
	//// только одна активная консоль, БЕЗ Split-screen
	//static uint TextWidth();
	//static uint TextHeight();

	static RECT AllTextRect(SIZE* rpSplits = nullptr, bool abMinimal = false);

	static wchar_t* GetTasks();

//public:
//	bool ResizeConsoles(const RECT &rFrom, enum ConEmuRect tFrom);
//	bool ResizeViews(bool bResizeRCon=true, WPARAM wParam=0, WORD newClientWidth=(WORD)-1, WORD newClientHeight=(WORD)-1);
};

class CGroupGuard : public CRefGuard<CVConGroup>
{
public:
	CGroupGuard(CVConGroup* apRef);
	virtual ~CGroupGuard();
	virtual bool Attach(CVConGroup* apRef) override;
	CVConGroup* VGroup() { return Ptr(); };
};
