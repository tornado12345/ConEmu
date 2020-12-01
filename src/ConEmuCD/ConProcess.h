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

#include "../common/MArray.h"
#include "../common/Common.h"

#include "WinConExports.h"

class MSectionLock;

struct ConProcess final
{
public:
	explicit ConProcess(const MModule& kernel32);
	~ConProcess();

	ConProcess(const ConProcess&) = delete;
	ConProcess(ConProcess&&) = delete;
	ConProcess& operator=(const ConProcess&) = delete;
	ConProcess& operator=(ConProcess&&) = delete;

	bool CheckProcessCount(bool abForce = false);
	bool GetRootInfo(CESERVER_REQ* pReq);
	bool ProcessAdd(DWORD nPID, MSectionLock& CS);
	void ProcessCountChanged(BOOL abChanged, UINT anPrevCount, MSectionLock& CS);
	bool ProcessRemove(DWORD nPID, UINT nPrevCount, MSectionLock& CS);

	void StartStopXTermMode(const TermModeCommand cmd, const DWORD value, const DWORD pid);
	void OnAttached();

	// returns true if process list was changed since last query
	bool GetProcesses(DWORD* processes, UINT count, DWORD dwMainServerPid);

	void DumpProcInfo(LPCWSTR sLabel, DWORD nCount, DWORD* pPID) const;

	DWORD WaitForRootConsoleProcess(DWORD nTimeout) const;

	/// Checks if we are able to retrieve current console processes list. <br>
	/// Note that this could not be possible on old OS or emulators.
	bool IsConsoleProcessCountSupported() const;

	/// Returns PIDs of processes except of server/root/ntvdm. <br>
	/// The function is used to check if we need to print
	/// the "Press Enter or Esc to exit..." confirmation
	MArray<DWORD> GetSpawnedProcesses() const;

	/// Returns all PIDs of processes without sorting, just as WinApi returns them.
	MArray<DWORD> GetAllProcesses() const;

public:
	MSection *csProc = nullptr;

	UINT nProcessCount = 0, nMaxProcesses = 0;
	UINT nConhostPID = 0; // Windows 7 and higher: "conhost.exe"
	MArray<DWORD> pnProcesses, pnProcessesGet, pnProcessesCopy;
	DWORD nProcessStartTick = 0;
	DWORD nLastRetProcesses[CONSOLE_PROCESSES_MAX/*20*/] = {};
	DWORD nLastFoundPID = 0; // Informational! Retrieved by CheckProcessCount/pfnGetConsoleProcessList
	DWORD dwProcessLastCheckTick = 0;

	#ifndef WIN64
	// Only 32-bit Windows versions have ntvdm (old 16-bit DOS subsystem)
	BOOL bNtvdmActive = false; DWORD nNtvdmPID = 0;
	#endif

	#ifdef USE_COMMIT_EVENT
	HANDLE hExtConsoleCommit = NULL; // Event для синхронизации (выставляется по Commit);
	DWORD  nExtConsolePID = 0;
	#endif

protected:
	int GetProcessCount(DWORD* rpdwPID, UINT nMaxCount, DWORD dwMainServerPid);

	// Hold all XTermMode requests
	struct XTermRequest
	{
		// the process was requested the mode
		DWORD pid;
		// time of request, required to avoid race
		// zero if process was found in GetConsoleProcessList
		DWORD tick;
		// TermModeCommand's mode arguments (if required)
		DWORD modes[tmc_Last];
	};
	MArray<XTermRequest> xRequests;
	// Some flags (only tmc_CursorShape yet) are *console* life-time
	// And we store here current projection of xRequests
	DWORD xFixedRequests[tmc_Last] = {};

	/// DWORD GetConsoleProcessList(LPDWORD lpdwProcessList, DWORD dwProcessCount)
	FGetConsoleProcessList pfnGetConsoleProcessList = nullptr;

	/// Console processes count on startup
	DWORD startProcessCount_ = 0;
	/// Console processes IDs on startup
	DWORD startProcessIds_[64] = {};

	// create=false used to erasing on reset
	INT_PTR GetXRequestIndex(DWORD pid, bool create);
	// Force update xFixedRequests and inform GUI
	void RefreshXRequests(MSectionLock& CS);
	// Check PID liveliness in xRequests
	void CheckXRequests(MSectionLock& CS);
};
