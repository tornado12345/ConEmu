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

#undef VALIDATE_AND_DELAY_ON_TERMINATE

#ifdef _DEBUG
//  SHOW_STARTED_MSGBOX may be defined in ConsoleMain.h
//	#define SHOW_ADMIN_STARTED_MSGBOX
//	#define SHOW_MAIN_MSGBOX
//	#define SHOW_ALTERNATIVE_MSGBOX
//  #define SHOW_DEBUG_STARTED_MSGBOX
//	#define SHOW_COMSPEC_STARTED_MSGBOX
//	#define SHOW_SERVER_STARTED_MSGBOX
//  #define SHOW_STARTED_ASSERT
//  #define SHOW_STARTED_PRINT
//	#define SHOW_STARTED_PRINT_LITE
	#define SHOW_EXITWAITKEY_MSGBOX
//	#define SHOW_INJECT_MSGBOX
//	#define SHOW_INJECTREM_MSGBOX
//	#define SHOW_ATTACH_MSGBOX
//	#define SHOW_ROOT_STARTED
//	#define SHOW_ASYNC_STARTED
//	#define USE_PIPE_DEBUG_BOXES
//	#define SHOW_SETCONTITLE_MSGBOX
	#define SHOW_LOADCFGFILE_MSGBOX
	#define SHOW_SHUTDOWNSRV_STEPS

//	#define DEBUG_ISSUE_623

//	#define VALIDATE_AND_DELAY_ON_TERMINATE
#endif

#define SHOWDEBUGSTR
#define DEBUGSTRCMD(x) DEBUGSTR(x)
#define DEBUGSTARTSTOPBOX(x) //MessageBox(nullptr, x, WIN3264TEST(L"ConEmuC",L"ConEmuC64"), MB_ICONINFORMATION|MB_SYSTEMMODAL)
#define DEBUGSTRFIN(x) DEBUGSTR(x)
#define DEBUGSTRCP(x) DEBUGSTR(x)
#define DEBUGSTRSIZE(x) DEBUGSTR(x)

//#define SHOW_INJECT_MSGBOX

#include "ConsoleMain.h"

#include "Actions.h"
#include "ConEmuCmd.h"
#include "ConEmuSrv.h"
#include "ConProcess.h"
#include "ConsoleArgs.h"
#include "ConsoleHelp.h"
#include "ConsoleState.h"
#include "Debugger.h"
#include "ExportedFunctions.h"
#include "GuiMacro.h"
#include "Shutdown.h"
#include "StartEnv.h"
#include "StdCon.h"

#include "../ConEmuHk/Injects.h"
#include "../ConEmu/version.h"
#include "../common/WUser.h"
#include "../common/WThreads.h"
#include "../common/WFiles.h"
#include "../common/WConsole.h"
#include "../common/StartupEnvEx.h"
#include "../common/SetEnvVar.h"
#include "../common/RConStartArgs.h"
#include "../common/ProcessSetEnv.h"
#include "../common/MWow64Disable.h"
#include "../common/MSectionSimple.h"
#include "../common/MRect.h"
#include "../common/MProcess.h"
#include "../common/MPerfCounter.h"
#include "../common/MArray.h"
#include "../common/HkFunc.h"
#include "../common/execute.h"
#include "../common/EnvVar.h"
#include "../common/EmergencyShow.h"
#include "../common/ConsoleRead.h"
#include "../common/CmdLine.h"

#include <tuple>



#ifndef __GNUC__
#pragma comment(lib, "shlwapi.lib")
#endif


WARNING("Обязательно после запуска сделать apiSetForegroundWindow на GUI окно, если в фокусе консоль");
WARNING("Обязательно получить код и имя родительского процесса");

#ifdef USEPIPELOG
// required global variables for PipeServer.h
namespace PipeServerLogger
{
	Event g_events[BUFFER_SIZE];
	LONG g_pos = -1;
}
#endif

WARNING("!!!! Пока можно при появлении события запоминать текущий тик");
// и проверять его в RefreshThread. Если он не 0 - и дельта больше (100мс?)
// то принудительно перечитать консоль и сбросить тик в 0.


WARNING("Наверное все-же стоит производить периодические чтения содержимого консоли, а не только по событию");

WARNING("Стоит именно здесь осуществлять проверку живости GUI окна (если оно было). Ведь может быть запущен не far, а CMD.exe");

WARNING("Если GUI умер, или не подцепился по таймауту - показать консольное окно и наверное установить шрифт поболе");

WARNING("В некоторых случаях не срабатывает ни EVENT_CONSOLE_UPDATE_SIMPLE ни EVENT_CONSOLE_UPDATE_REGION");
// Пример. Запускаем cmd.exe. печатаем какую-то муйню в командной строке и нажимаем 'Esc'
// При Esc никаких событий ВООБЩЕ не дергается, а экран в консоли изменился!



// Время ожидания завершения консольных процессов, когда юзер нажал крестик в КОНСОЛЬНОМ окне
// The system also displays this dialog box if the process does not respond within a certain time-out period
// (5 seconds for CTRL_CLOSE_EVENT, and 20 seconds for CTRL_LOGOFF_EVENT or CTRL_SHUTDOWN_EVENT).
// Поэтому ждать пытаемся - не больше 4 сек
#define CLOSE_CONSOLE_TIMEOUT 4000

/*  Global  */
CEStartupEnv* gpStartEnv = nullptr;
HMODULE ghOurModule = nullptr; // ConEmuCD.dll
DWORD   gnSelfPID = 0;
wchar_t gsModuleName[32] = L"";
wchar_t gsVersion[20] = L"";
wchar_t gsSelfExe[MAX_PATH] = L"";  // Full path+exe to our executable
wchar_t gsSelfPath[MAX_PATH] = L""; // Directory of our executable
DWORD   gnMainServerPID = 0;
DWORD   gnAltServerPID = 0;
BOOL    gbLogProcess = FALSE;
BOOL    gbWasBufferHeight = FALSE;
BOOL    gbNonGuiMode = FALSE;
DWORD   gnExitCode = 0;
HANDLE  ghRootProcessFlag = nullptr;
HANDLE  ghExitQueryEvent = nullptr; int nExitQueryPlace = 0, nExitPlaceStep = 0;
#define EPS_WAITING4PROCESS  550
#define EPS_ROOTPROCFINISHED 560
SetTerminateEventPlace gTerminateEventPlace = ste_None;
HANDLE  ghQuitEvent = nullptr;
bool    gbQuit = false;
BOOL    gbInShutdown = FALSE;
BOOL    gbStopExitWaitForKey = FALSE;
BOOL    gbCtrlBreakStopWaitingShown = FALSE;
BOOL    gbTerminateOnCtrlBreak = FALSE;
bool    gbSkipHookersCheck = false;
int     gbRootWasFoundInCon = 0;
BOOL    gbComspecInitCalled = FALSE;
DWORD   gdwMainThreadId = 0;
wchar_t* gpszRunCmd = nullptr;
bool    gbRunInBackgroundTab = false;
DWORD   gnImageSubsystem = 0, gnImageBits = 32;
#ifdef _DEBUG
size_t gnHeapUsed = 0, gnHeapMax = 0;
HANDLE ghFarInExecuteEvent;
#endif

BOOL  gbDumpServerInitStatus = FALSE;
UINT  gnPTYmode = 0; // 1 enable PTY, 2 - disable PTY (work as plain console), 0 - don't change
WORD  gnDefTextColors = 0, gnDefPopupColors = 0; // Передаются через "/TA=..."
BOOL  gbVisibleOnStartup = FALSE;


SrvInfo* gpSrv = nullptr;

COORD gcrVisibleSize = {80,25}; // gcrBufferSize переименован в gcrVisibleSize
BOOL  gbParmVisibleSize = FALSE;
BOOL  gbParmBufSize = FALSE;
SHORT gnBufferHeight = 0;
SHORT gnBufferWidth = 0; // Определяется в MyGetConsoleScreenBufferInfo

#ifdef _DEBUG
wchar_t* gpszPrevConTitle = nullptr;
#endif

MFileLogEx* gpLogSize = nullptr;


BOOL gbInRecreateRoot = FALSE;


void ShutdownSrvStep(LPCWSTR asInfo, int nParm1 /*= 0*/, int nParm2 /*= 0*/, int nParm3 /*= 0*/, int nParm4 /*= 0*/)
{
#ifdef SHOW_SHUTDOWNSRV_STEPS
	static int nDbg = 0;
	if (!nDbg)
		nDbg = IsDebuggerPresent() ? 1 : 2;
	if (nDbg != 1)
		return;
	wchar_t szFull[512];
	msprintf(szFull, countof(szFull), L"%u:ConEmuC:PID=%u:TID=%u: ",
		GetTickCount(), GetCurrentProcessId(), GetCurrentThreadId());
	if (asInfo)
	{
		int nLen = lstrlen(szFull);
		msprintf(szFull+nLen, countof(szFull)-nLen, asInfo, nParm1, nParm2, nParm3, nParm4);
	}
	lstrcat(szFull, L"\n");
	OutputDebugString(szFull);
#endif
}

void InitVersion()
{
	wchar_t szMinor[8] = L""; lstrcpyn(szMinor, _T(MVV_4a), countof(szMinor));
	//msprintf не умеет "%02u"
	swprintf_c(gsVersion, L"%02u%02u%02u%s", MVV_1, MVV_2, MVV_3, szMinor);
}

#ifdef _DEBUG
int ShowInjectRemoteMsg(int nRemotePID, LPCWSTR asCmdArg)
{
	int iBtn = IDOK;
	#ifdef SHOW_INJECTREM_MSGBOX
	wchar_t szDbgMsg[512], szTitle[128];
	PROCESSENTRY32 pinf;
	GetProcessInfo(nRemotePID, &pinf);
	swprintf_c(szTitle, L"ConEmuCD PID=%u", GetCurrentProcessId());
	swprintf_c(szDbgMsg, L"Hooking PID=%s {%s}\nConEmuCD PID=%u. Continue with injects?", asCmdArg ? asCmdArg : L"", pinf.szExeFile, GetCurrentProcessId());
	iBtn = MessageBoxW(nullptr, szDbgMsg, szTitle, MB_SYSTEMMODAL|MB_OKCANCEL);
	#endif
	return iBtn;
}
#endif

//UINT gnMsgActivateCon = 0;
UINT gnMsgSwitchCon = 0;
UINT gnMsgHookedKey = 0;
//UINT gnMsgConsoleHookedKey = 0;

void LoadSrvInfoMap(LPCWSTR pszExeName = nullptr, LPCWSTR pszDllName = nullptr)
{
	if (gState.realConWnd_)
	{
		MFileMapping<CESERVER_CONSOLE_MAPPING_HDR> ConInfo;
		ConInfo.InitName(CECONMAPNAME, LODWORD(gState.realConWnd_)); //-V205
		CESERVER_CONSOLE_MAPPING_HDR *pInfo = ConInfo.Open();
		if (pInfo)
		{
			if (pInfo->cbSize >= sizeof(CESERVER_CONSOLE_MAPPING_HDR))
			{
				if (pInfo->hConEmuRoot && IsWindow(pInfo->hConEmuRoot))
				{
					WorkerServer::SetConEmuWindows(pInfo->hConEmuRoot, pInfo->hConEmuWndDc, pInfo->hConEmuWndBack);
				}
				if (pInfo->nServerPID && pInfo->nServerPID != gnSelfPID)
				{
					gnMainServerPID = pInfo->nServerPID;
					gnAltServerPID = pInfo->nAltServerPID;
				}

				gbLogProcess = (pInfo->nLoggingType == glt_Processes);
				if (pszExeName && gbLogProcess)
				{
					CEStr lsDir;
					int ImageBits = 0, ImageSystem = 0;
					#ifdef _WIN64
					ImageBits = 64;
					#else
					ImageBits = 32;
					#endif
					CESERVER_REQ* pIn = ExecuteNewCmdOnCreate(pInfo, gState.realConWnd_, eSrvLoaded,
						L"", pszExeName, pszDllName, GetDirectory(lsDir), nullptr, nullptr, nullptr, nullptr,
						ImageBits, ImageSystem,
						GetStdHandle(STD_INPUT_HANDLE), GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE));
					if (pIn)
					{
						CESERVER_REQ* pOut = ExecuteGuiCmd(gState.realConWnd_, pIn, gState.realConWnd_);
						ExecuteFreeResult(pIn);
						if (pOut) ExecuteFreeResult(pOut);
					}
				}

				ConInfo.CloseMap();
			}
			else
			{
				_ASSERTE(pInfo->cbSize == sizeof(CESERVER_CONSOLE_MAPPING_HDR));
			}
		}
	}
}

#if defined(__GNUC__)
extern "C"
#endif
BOOL WINAPI DllMain(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
		{
			ghOurModule = (HMODULE)hModule;
			ghWorkingModule = hModule;

			//_ASSERTE(FALSE && "DLL_PROCESS_ATTACH");

			hkFunc.Init(ConEmuCD_DLL_3264, ghOurModule);

			DWORD nImageBits = WIN3264TEST(32,64), nImageSubsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
			GetImageSubsystem(nImageSubsystem,nImageBits);
			if (nImageSubsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI)
				gState.realConWnd_ = nullptr;
			else
				gState.realConWnd_ = GetConEmuHWND(2);

			gnSelfPID = GetCurrentProcessId();
			gfnSearchAppPaths = SearchAppPaths;

			wchar_t szExeName[MAX_PATH] = L"", szDllName[MAX_PATH] = L"";
			GetModuleFileName(nullptr, szExeName, countof(szExeName));
			GetModuleFileName((HMODULE)hModule, szDllName, countof(szDllName));
			if (IsConsoleServer(PointToName(szExeName)))
				wcscpy_c(gsModuleName, WIN3264TEST(L"ConEmuC",L"ConEmuC64"));
			else
				wcscpy_c(gsModuleName, WIN3264TEST(L"ConEmuCD",L"ConEmuCD64"));

			InitVersion();

			#ifdef _DEBUG
			HANDLE hProcHeap = GetProcessHeap();
			gAllowAssertThread = am_Pipe;
			#endif

			#ifdef SHOW_STARTED_MSGBOX
			if (!IsDebuggerPresent())
			{
				wchar_t szMsg[128] = L"";
				msprintf(szMsg, countof(szMsg), ConEmuCD_DLL_3264 L" loaded, PID=%u, TID=%u", GetCurrentProcessId(), GetCurrentThreadId());
				wchar_t szFile[MAX_PATH] = L"";
				GetModuleFileNameW(nullptr, szFile, countof(szFile));
				MessageBoxW(nullptr, szMsg, PointToName(szFile), 0);
			}
			#endif
			#ifdef _DEBUG
			DWORD dwConMode = -1;
			GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &dwConMode);
			#endif

			HeapInitialize();

			/* *** DEBUG PURPOSES */
			gpStartEnv = LoadStartupEnvEx::Create();
			/* *** DEBUG PURPOSES */

			// Preload some function pointers to get proper addresses,
			// before some other hooking dlls may replace them
			GetLoadLibraryAddress(); // gfLoadLibrary
			if (IsWin7()) // and gfLdrGetDllHandleByName
				GetLdrGetDllHandleByNameAddress();

			//#ifndef TESTLINK
			gpLocalSecurity = LocalSecurity();
			//gnMsgActivateCon = RegisterWindowMessage(CONEMUMSG_ACTIVATECON);
			gnMsgSwitchCon = RegisterWindowMessage(CONEMUMSG_SWITCHCON);
			gnMsgHookedKey = RegisterWindowMessage(CONEMUMSG_HOOKEDKEY);

			// Открыть мэппинг консоли и попытаться получить HWND GUI, PID сервера, и пр...
			if (gState.realConWnd_)
				LoadSrvInfoMap(szExeName, szDllName);
		}
		break;
		case DLL_PROCESS_DETACH:
		{
			ShutdownSrvStep(L"DLL_PROCESS_DETACH");

			#ifdef _DEBUG
			if ((gState.runMode_ == RunMode::Server) && (nExitPlaceStep == EPS_WAITING4PROCESS/*550*/))
			{
				// Это происходило после Ctrl+C если не был установлен HandlerRoutine
				// Ни _ASSERT ни DebugBreak здесь позвать уже не получится - все закрывается и игнорируется
				OutputDebugString(L"!!! Server was abnormally terminated !!!\n");
				LogString("!!! Server was abnormally terminated !!!\n");
			}
			#endif

			if (gState.runMode_ == RunMode::AltServer)
			{
				SendStopped();
			}

			Shutdown::ProcessShutdown();

			CommonShutdown();

			delete gpWorker;
			delete gpSrv;
			delete gpConsoleArgs;
			// Should not do any special (POD), but just in case
			gState.~ConsoleState();

			HeapDeinitialize();

			ShutdownSrvStep(L"DLL_PROCESS_DETACH done");
		}
		break;
	}

	return TRUE;
}

//#if defined(GNUCRTSTARTUP)
//extern "C" {
//	BOOL WINAPI _DllMainCRTStartup(HANDLE hDll,DWORD dwReason,LPVOID lpReserved);
//};
//
//BOOL WINAPI _DllMainCRTStartup(HANDLE hDll,DWORD dwReason,LPVOID lpReserved)
//{
//	DllMain(hDll, dwReason, lpReserved);
//	return TRUE;
//}
//#endif

#ifdef _DEBUG
void OnProcessCreatedDbg(BOOL bRc, DWORD dwErr, LPPROCESS_INFORMATION pProcessInformation, LPSHELLEXECUTEINFO pSEI)
{
	int iDbg = 0;

#ifdef SHOW_ASYNC_STARTED
	if (gpConsoleArgs->asyncRun_)
	{
		_ASSERTE(FALSE && "Async startup requested");
	}
#endif

#ifdef SHOW_ROOT_STARTED
	if (bRc)
	{
		wchar_t szTitle[64], szMsg[128];
		swprintf_c(szTitle, L"ConEmuSrv, PID=%u", GetCurrentProcessId());
		swprintf_c(szMsg, L"Root process started, PID=%u", pProcessInformation->dwProcessId);
		MessageBox(nullptr,szMsg,szTitle,0);
	}
#endif
}
#endif

BOOL createProcess(BOOL abSkipWowChange, LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
	CEStr fnDescr(lstrmerge(L"createProcess App={", lpApplicationName, L"} Cmd={", lpCommandLine, L"}"));
	LogFunction(fnDescr);

	MWow64Disable wow;
	if (!abSkipWowChange)
		wow.Disable();

	// %PATHS% from [HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths]
	// must be already processed in IsNeedCmd >> FileExistsSearch >> SearchAppPaths

	DWORD nStartTick = GetTickCount();

#if defined(SHOW_STARTED_PRINT_LITE)
	if (gState.runMode_ == RunMode::Server)
	{
		_printf("Starting root: ");
		_wprintf(lpCommandLine ? lpCommandLine : lpApplicationName ? lpApplicationName : L"<nullptr>");
	}
#endif

	SetLastError(0);

	BOOL lbRc = CreateProcess(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
	DWORD dwErr = GetLastError();
	DWORD nStartDuration = GetTickCount() - nStartTick;

	wchar_t szRunRc[80];
	if (lbRc)
		swprintf_c(szRunRc, L"Succeeded (%u ms) PID=%u", nStartDuration, lpProcessInformation->dwProcessId);
	else
		swprintf_c(szRunRc, L"Failed (%u ms) Code=%u(x%04X)", nStartDuration, dwErr, dwErr);
	LogFunction(szRunRc);

#if defined(SHOW_STARTED_PRINT_LITE)
	if (gState.runMode_ == RunMode::Server)
	{
		if (lbRc)
			_printf("\nSuccess (%u ms), PID=%u\n\n", nStartDuration, lpProcessInformation->dwProcessId);
		else
			_printf("\nFailed (%u ms), Error code=%u(x%04X)\n", nStartDuration, dwErr, dwErr);
	}
#endif

	#ifdef _DEBUG
	OnProcessCreatedDbg(lbRc, dwErr, lpProcessInformation, nullptr);
	#endif

	if (!abSkipWowChange)
	{
		wow.Restore();
	}

	SetLastError(dwErr);
	return lbRc;
}

void ShowServerStartedMsgBox(LPCWSTR asCmdLine)
{
#ifdef SHOW_SERVER_STARTED_MSGBOX
	wchar_t szTitle[100]; swprintf_c(szTitle, L"ConEmuC [Server] started (PID=%i)", gnSelfPID);
	const wchar_t* pszCmdLine = asCmdLine;
	MessageBox(nullptr,pszCmdLine,szTitle,0);
#endif
}

void ShowMainStartedMsgBoxEarly(const ConsoleMainMode anWorkMode, LPCWSTR asCmdLine)
{
#if defined(SHOW_MAIN_MSGBOX) || defined(SHOW_ADMIN_STARTED_MSGBOX)
	bool bShowWarn = false;
#if defined(SHOW_MAIN_MSGBOX)
	if (!IsDebuggerPresent())
		bShowWarn = true;
#endif
#if defined(SHOW_ADMIN_STARTED_MSGBOX)
	if (IsUserAdmin())
		bShowWarn = true;
#endif
	if (bShowWarn)
	{
		wchar_t szMsg[MAX_PATH + 128] = L"";
		msprintf(szMsg, countof(szMsg), ConEmuCD_DLL_3264 L" loaded, PID=%u, TID=%u, mode=%u\r\n",
			GetCurrentProcessId(), GetCurrentThreadId(), static_cast<int>(anWorkMode));
		const auto nMsgLen = wcslen(szMsg);
		GetModuleFileNameW(nullptr, szMsg + nMsgLen, static_cast<DWORD>(countof(szMsg) - nMsgLen));
		MessageBoxW(nullptr, szMsg, L"ConEmu server" WIN3264TEST("", " x64"), 0);
	}
#endif
}

void ShowMainStartedMsgBox(const ConsoleMainMode anWorkMode, LPCWSTR asCmdLine)
{
	ShowMainStartedMsgBoxEarly(anWorkMode, asCmdLine);

#if defined(SHOW_STARTED_PRINT)
	BOOL lbDbgWrite; DWORD nDbgWrite; HANDLE hDbg; char szDbgString[255], szHandles[128];
	wchar_t szTitle[255];
	swprintf_c(szTitle, L"ConEmuCD[%u]: PID=%u", WIN3264TEST(32,64), GetCurrentProcessId());
	MessageBox(0, asCmdLine, szTitle, MB_SYSTEMMODAL);
	hDbg = CreateFile(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
	                  0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	sprintf_c(szHandles, "STD_OUTPUT_HANDLE(0x%08X) STD_ERROR_HANDLE(0x%08X) CONOUT$(0x%08X)",
	        (DWORD)GetStdHandle(STD_OUTPUT_HANDLE), (DWORD)GetStdHandle(STD_ERROR_HANDLE), (DWORD)hDbg);
	printf("ConEmuC: Printf: %s\n", szHandles);
	sprintf_c(szDbgString, "ConEmuC: STD_OUTPUT_HANDLE: %s\n", szHandles);
	lbDbgWrite = WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), szDbgString, lstrlenA(szDbgString), &nDbgWrite, nullptr);
	sprintf_c(szDbgString, "ConEmuC: STD_ERROR_HANDLE:  %s\n", szHandles);
	lbDbgWrite = WriteFile(GetStdHandle(STD_ERROR_HANDLE), szDbgString, lstrlenA(szDbgString), &nDbgWrite, nullptr);
	sprintf_c(szDbgString, "ConEmuC: CONOUT$: %s", szHandles);
	lbDbgWrite = WriteFile(hDbg, szDbgString, lstrlenA(szDbgString), &nDbgWrite, nullptr);
	CloseHandle(hDbg);
	//sprintf_c(szDbgString, "ConEmuC: PID=%u", GetCurrentProcessId());
	//MessageBoxA(0, "Press Ok to continue", szDbgString, MB_SYSTEMMODAL);
#elif defined(SHOW_STARTED_PRINT_LITE)
	wchar_t szPath[MAX_PATH]; GetModuleFileNameW(nullptr, szPath, countof(szPath));
	wchar_t szDbgMsg[MAX_PATH*2];
	swprintf_c(szDbgMsg, L"%s started, PID=%u\n", PointToName(szPath), GetCurrentProcessId());
	_wprintf(szDbgMsg);
	gbDumpServerInitStatus = TRUE;
#endif
}

void ShowStartedMsgBox()
{
	#if defined(SHOW_STARTED_MSGBOX) || defined(SHOW_COMSPEC_STARTED_MSGBOX)
	if (!IsDebuggerPresent())
	{
		wchar_t szTitle[100]; swprintf_c(szTitle, L"ConEmuCD[%u]: PID=%u", WIN3264TEST(32,64), GetCurrentProcessId());
		MessageBox(nullptr, gpConsoleArgs->fullCmdLine_.c_str(L""), szTitle, MB_SYSTEMMODAL);
	}
	#endif

}

void ShowStartedAssert()
{
	#ifdef SHOW_STARTED_ASSERT
	if (!IsDebuggerPresent())
	{
		_ASSERT(FALSE);
	}
	#endif
}


void LoadExePath()
{
	// Already loaded?
	if (gsSelfExe[0] && gsSelfPath[0])
		return;

	_ASSERTE(ghOurModule != nullptr);

	const DWORD nSelfLen = GetModuleFileNameW(ghOurModule, gsSelfExe, countof(gsSelfExe));
	if (!nSelfLen || (nSelfLen >= countof(gsSelfExe)))
	{
		_ASSERTE(FALSE && "GetModuleFileNameW(nullptr) failed");
		gsSelfExe[0] = 0;
	}
	else
	{
		lstrcpyn(gsSelfPath, gsSelfExe, countof(gsSelfPath));
		wchar_t* pszSlash = const_cast<wchar_t*>(PointToName(gsSelfPath));
		if (pszSlash && (pszSlash > gsSelfPath))
			*pszSlash = 0;
		else
			gsSelfPath[0] = 0;
	}
}

void UnlockCurrentDirectory()
{
	if ((gState.runMode_ == RunMode::Server) && gsSelfPath[0])
	{
		SetCurrentDirectory(gsSelfPath);
	}
}


bool CheckAndWarnHookSetters()
{
	if (gbSkipHookersCheck)
	{
		if (gpLogSize) gpLogSize->LogString(L"CheckAndWarnHookers skipped due to /OMITHOOKSWARN switch");
		return false;
	}

	MPerfCounter perf(2);
	PerfCounter c_scan{0}, c_write{1};

	bool bHooked = false;
	struct CheckModules {
		LPCWSTR Title, File;
	} modules [] = {
		{L"MacType", WIN3264TEST(L"MacType.dll", L"MacType64.dll")},
		{L"AnsiCon", WIN3264TEST(L"ANSI32.dll", L"ANSI64.dll")},
		{nullptr}
	};
	CEStr szMessage;
	wchar_t szPath[MAX_PATH+16] = L"", szAddress[32] = L"";
	LPCWSTR pszTitle = nullptr, pszName = nullptr;
	HMODULE hModule = nullptr;
	bool bConOutChecked = false, bRedirected = false;
	//BOOL bColorChanged = FALSE;
	CONSOLE_SCREEN_BUFFER_INFO sbi = {};
	HANDLE hOut = nullptr;

	for (INT_PTR i = 0; modules[i].Title; i++)
	{
		perf.Start(c_scan);
		pszTitle = modules[i].Title;
		pszName = modules[i].File;

		hModule = GetModuleHandle(pszName);
		if (!hModule)
		{
			perf.Stop(c_scan);
		}
		else
		{
			bHooked = true;
			if (!GetModuleFileName(hModule, szPath, countof(szPath)))
			{
				wcscpy_c(szPath, pszName); // Must not get here, but show a name at least on errors
			}
			perf.Stop(c_scan);

			perf.Start(c_write);
			if (!bConOutChecked)
			{
				bConOutChecked = true;
				hOut = GetStdHandle(STD_OUTPUT_HANDLE);
				GetConsoleScreenBufferInfo(hOut, &sbi);
				bRedirected = IsOutputRedirected();

				#if 0
				// Useless in most cases, most of users are running cmd.exe
				// but it clears all existing text attributes on start
				if (!bRedirected)
					bColorChanged = SetConsoleTextAttribute(hOut, 12); // LightRed on Black
				#endif
			}

			swprintf_c(szAddress, WIN3264TEST(L"0x%08X",L"0x%08X%08X"), WIN3264WSPRINT((DWORD_PTR)hModule));
			szMessage = lstrmerge(
				L"WARNING! The ", pszTitle, L"'s hooks are detected at ", szAddress, L"\r\n"
				L"         ");
			LPCWSTR pszTail = L"\r\n"
				L"         Please add ConEmuC.exe and ConEmuC64.exe\r\n"
				L"         to the exclusion list to avoid crashes!\r\n"
				L"         " CEMACTYPEWARN L"\r\n\r\n";
			_wprintf(szMessage);
			_wprintf(szPath);
			_wprintf(pszTail);

			perf.Stop(c_write);
		}
	}

	#if 0
	// If we've set warning colors - return original ones
	if (bColorChanged)
		SetConsoleTextAttribute(hOut, sbi.wAttributes);
	#endif

	wchar_t szLog[120];
	const auto scan_stat = perf.GetStats(c_scan.ID);
	const auto write_stat = perf.GetStats(c_write.ID);
	swprintf_c(szLog, L"CheckAndWarnHookers finished, Scan(%u%%, %ums), Write(%u%%, %ums)",
		scan_stat.Percentage, scan_stat.MilliSeconds, write_stat.Percentage, write_stat.MilliSeconds);
	LogString(szLog);

	return bHooked;
}

// Is used both in normal (server) and debugging mode
int AttachRootProcessHandle()
{
	if (gpWorker->IsDebugCmdLineSet())
	{
		_ASSERTE(gpWorker->IsDebuggerActive());
		return 0; // Started from DebuggingThread
	}

	// ReSharper disable once CppInitializedValueIsAlwaysRewritten
	DWORD dwErr = 0;
	// Нужно открыть HANDLE корневого процесса
	_ASSERTE(gpWorker->RootProcessHandle()==nullptr || gpWorker->RootProcessHandle()==GetCurrentProcess());
	if (gpWorker->RootProcessId() == GetCurrentProcessId())
	{
		if (gpWorker->RootProcessHandle() == nullptr)
		{
			gpWorker->SetRootProcessHandle(GetCurrentProcess());
		}
	}
	else if (gpWorker->IsDebuggerActive())
	{
		if (gpWorker->RootProcessHandle() == nullptr)
		{
			gpWorker->SetRootProcessHandle(gpWorker->DbgInfo().GetProcessHandleForDebug(gpWorker->RootProcessId()));
		}
	}
	else if (gpWorker->RootProcessHandle() == nullptr)
	{
		gpWorker->SetRootProcessHandle(OpenProcess(MY_PROCESS_ALL_ACCESS, FALSE, gpWorker->RootProcessId()));
		if (gpWorker->RootProcessHandle() == nullptr)
		{
			gpWorker->SetRootProcessHandle(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION, FALSE, gpWorker->RootProcessId()));
		}
	}

	if (!gpWorker->RootProcessHandle())
	{
		dwErr = GetLastError();
		wchar_t* lpMsgBuf = nullptr;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, dwErr,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&lpMsgBuf), 0, nullptr);
		_printf("\nCan't open process (%i) handle, ErrCode=0x%08X, Description:\n", //-V576
		        gpWorker->RootProcessId(), dwErr, (lpMsgBuf == nullptr) ? L"<Unknown error>" : lpMsgBuf);

		if (lpMsgBuf) LocalFree(lpMsgBuf);
		SetLastError(dwErr);

		return CERR_CREATEPROCESS;
	}

	if (gpWorker->IsDebuggerActive())
	{
		wchar_t szTitle[64];
		swprintf_c(szTitle, L"Debugging PID=%u, Debugger PID=%u", gpWorker->RootProcessId(), GetCurrentProcessId());
		SetTitle(szTitle);

		gpWorker->DbgInfo().UpdateDebuggerTitle();
	}

	return 0;
}

void InitConsoleOutMode(const ConsoleMainMode anWorkMode)
{
	if (anWorkMode == ConsoleMainMode::Normal)
	{
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleMode(hOut, ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT);

		// #SETTINGS Config this attribute in Gui settings
		#if 0
		SetConsoleTextAttribute(hOut, 7);
		#endif
	}
}

bool CheckDllMainLinkerFailure()
{
	// Check linker fails!
	if (ghOurModule == nullptr)
	{
		wchar_t szTitle[128]; swprintf_c(szTitle, ConEmuCD_DLL_3264 L", PID=%u", GetCurrentProcessId());
		MessageBox(nullptr, L"ConsoleMain: ghOurModule is nullptr\nDllMain was not executed", szTitle, MB_ICONSTOP|MB_SYSTEMMODAL);
		return false;
	}
	return true;
}

void PrintConsoleOutputCP()
{
	#ifdef _DEBUG
	{
		wchar_t szCpInfo[128];
		DWORD nCP = GetConsoleOutputCP();
		swprintf_c(szCpInfo, L"Current Output CP = %u\n", nCP);
		DEBUGSTRCP(szCpInfo);
	}
	#endif
}



// Main entry point for ConEmuC.exe
int __stdcall ConsoleMain3(const ConsoleMainMode anWorkMode, LPCWSTR asCmdLine)
{
	ShowMainStartedMsgBox(anWorkMode, asCmdLine);

	InitConsoleOutMode(anWorkMode);

	if (!CheckDllMainLinkerFailure())
		return CERR_DLLMAIN_SKIPPED;

	LPCWSTR pszFullCmdLine = asCmdLine;

	gpConsoleArgs = new ConsoleArgs();

	// Parse command line
	const auto argsRc = gpConsoleArgs->ParseCommandLine(pszFullCmdLine, anWorkMode);
	switch (argsRc)
	{
	case 0:
		break; // ok
	case CERR_HELPREQUESTED:
		Help();
		return argsRc;
	case CERR_CMDLINEEMPTY:
		Help();
		_printf("\n\nParsing command line failed (/C argument not found):\n");
		_wprintf(GetCommandLineW());
		_printf("\n");
		return argsRc;
	default:
		_printf("Parsing command line failed:\n");
		_wprintf(asCmdLine);
		_printf("\n");
		return argsRc;
	}

	// Validate current work mode
	switch (anWorkMode)
	{
	case ConsoleMainMode::AltServer:
		_ASSERTE(gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer);
		gState.runMode_ = RunMode::AltServer;
		break;
	case ConsoleMainMode::GuiMacro:
		_ASSERTE(gState.runMode_ == RunMode::Undefined || gState.runMode_ == RunMode::GuiMacro);
		gState.runMode_ = RunMode::GuiMacro;
		break;
	default:
		_ASSERTE(gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer
			|| gState.runMode_ == RunMode::Comspec || gState.runMode_ == RunMode::Undefined
			|| gState.runMode_ == RunMode::SetHook64);
		break;
	}

	// #SERVER remove gpSrv
	if (!gpSrv)
	{
		gpSrv = static_cast<SrvInfo*>(calloc(sizeof(SrvInfo), 1));
		gpSrv->InitFields();
	}

	switch (gState.runMode_)
	{
	case RunMode::Server:
	case RunMode::AltServer:
	case RunMode::AutoAttach:
		gpWorker = new WorkerServer;
		break;
	case RunMode::Comspec:
	case RunMode::Undefined:
		gpWorker = new WorkerComspec;
		break;
	case RunMode::GuiMacro:
	case RunMode::SetHook64:
		gpWorker = new WorkerBase;
		break;
	default:
		_ASSERTE(FALSE && "Run mode should be known already");
		gpWorker = new WorkerBase;
		break;
	}

	int iRc = 100;

	// Let use pi_ from gpState to simplify debugging
	PROCESS_INFORMATION& pi = gState.pi_;
	// We should try to be completely transparent for calling process
	STARTUPINFOW si = gState.si_;

	DWORD dwErr = 0, nWait = 0, nWaitExitEvent = -1, nWaitDebugExit = -1, nWaitComspecExit = -1;
	DWORD dwWaitGui = -1, dwWaitRoot = -1;
	BOOL lbRc = FALSE;

	ShowStartedMsgBox();
	ShowStartedAssert();

	LoadExePath();

	PRINT_COMSPEC(L"ConEmuC started: %s\n", asCmdLine);
	nExitPlaceStep = 50;
	xf_check();

	PrintConsoleOutputCP();

	// Event is used in almost all modes, even in "debugger"
	if (!ghExitQueryEvent
		&& (gState.runMode_ != RunMode::GuiMacro)
		)
	{
		ghExitQueryEvent = CreateEvent(nullptr, TRUE/*используется в нескольких нитях, manual*/, FALSE, nullptr);
	}

	// Checks and actions which are done without starting processes
	if (gState.runMode_ == RunMode::GuiMacro)
	{
		// Separate parser function
		iRc = GuiMacroCommandLine(pszFullCmdLine);
		_ASSERTE(iRc == CERR_GUIMACRO_SUCCEEDED || iRc == CERR_GUIMACRO_FAILED);
		gbInShutdown = true;
		goto wrap;
	}
	if (gpConsoleArgs->eStateCheck_ != ConEmuStateCheck::None)
	{
		bool bOn = DoStateCheck(gpConsoleArgs->eStateCheck_);
		iRc = bOn ? CERR_CHKSTATE_ON : CERR_CHKSTATE_OFF;
		gbInShutdown = true;
		goto wrap;
	}
	if (gpConsoleArgs->eExecAction_ != ConEmuExecAction::None)
	{
		MacroInstance macroInst{};
		if (!gpConsoleArgs->guiMacro_.IsEmpty())
			ArgGuiMacro(gpConsoleArgs->guiMacro_.GetStr(), macroInst);
		iRc = DoExecAction(gpConsoleArgs->eExecAction_, gpConsoleArgs->command_, macroInst);
		gbInShutdown = true;
		goto wrap;
	}

	_ASSERTE((anWorkMode == ConsoleMainMode::AltServer) == (GetRunMode() == RunMode::AltServer));
	if ((iRc = gpWorker->ProcessCommandLineArgs()) != 0)
	{
		wchar_t szLog[80];
		swprintf_c(szLog, L"ProcessCommandLineArgs returns %i, exiting", iRc);
		LogFunction(szLog);
		// Especially if our process was started under non-interactive account,
		// than ExitWaitForKey causes the infinitely running process, which user can't kill easily
		gbInShutdown = true;
		goto wrap;
	}
	if ((iRc = gpWorker->PostProcessParams()) != 0)
	{
		wchar_t szLog[80];
		swprintf_c(szLog, L"PostProcessParams returns %i, exiting", iRc);
		LogFunction(szLog);
		// Especially if our process was started under non-interactive account,
		// than ExitWaitForKey causes the infinitely running process, which user can't kill easily
		gbInShutdown = true;
		goto wrap;
	}

	if (gbInShutdown)
	{
		goto wrap;
	}

	// After debugger call the flow after ParseCommandLine is expected to be sent to exit
	_ASSERTE(!(gpWorker->IsDebuggerActive() || gpWorker->IsDebugProcess() || gpWorker->IsDebugProcessTree()));

	// Force change current handles to STD ConIn/ConOut? "-STD" switch in the command line
	if (gState.reopenStdHandles_)
	{
		StdCon::ReopenConsoleHandles(&si);
	}

	// Continue server initialization
	if (gState.runMode_ == RunMode::Server)
	{
		// Until the root process is not terminated - set to STILL_ACTIVE
		gnExitCode = STILL_ACTIVE;

		#ifdef _DEBUG
		// отладка для Wine
		#ifdef USE_PIPE_DEBUG_BOXES
		gbPipeDebugBoxes = true;
		#endif

		if (gState.isWine_)
		{
			wchar_t szMsg[128];
			msprintf(szMsg, countof(szMsg), L"ConEmuC Started, Wine detected\r\nConHWND=x%08X(%u), PID=%u\r\nCmdLine: ",
				LODWORD(gState.realConWnd_), LODWORD(gState.realConWnd_), gnSelfPID);
			_wprintf(szMsg);
			_wprintf(asCmdLine);
			_wprintf(L"\r\n");
		}
		#endif

		// Warn about external hook setters
		CheckAndWarnHookSetters();
	}

	_ASSERTE(!gpWorker->RootProcessGui() || ((LODWORD(gpWorker->RootProcessGui()))!=0xCCCCCCCC && IsWindow(gpWorker->RootProcessGui())));

	//#ifdef _DEBUG
	//CreateLogSizeFile();
	//#endif
	nExitPlaceStep = 100;
	xf_check();


	if ((gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer) && !IsDebuggerPresent() && gState.noCreateProcess_)
	{
		ShowServerStartedMsgBox(asCmdLine);
	}


	/* ***************************** */
	/* *** "Общая" инициализация *** */
	/* ***************************** */
	nExitPlaceStep = 150;

	if (!ghExitQueryEvent)
	{
		dwErr = GetLastError();
		_printf("CreateEvent() failed, ErrCode=0x%08X\n", dwErr);
		iRc = CERR_EXITEVENT; goto wrap;
	}

	ResetEvent(ghExitQueryEvent);

	if (!ghQuitEvent)
		ghQuitEvent = CreateEvent(nullptr, TRUE/*used in several threads, manual!*/, FALSE, nullptr);

	if (!ghQuitEvent)
	{
		dwErr = GetLastError();
		_printf("CreateEvent() failed, ErrCode=0x%08X\n", dwErr);
		iRc = CERR_EXITEVENT; goto wrap;
	}

	ResetEvent(ghQuitEvent);
	xf_check();


	if (gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer || gState.runMode_ == RunMode::AutoAttach)
	{
		if ((HANDLE)ghConOut == INVALID_HANDLE_VALUE)
		{
			dwErr = GetLastError();
			_printf("CreateFile(CONOUT$) failed, ErrCode=0x%08X\n", dwErr);
			iRc = CERR_CONOUTFAILED; goto wrap;
		}
	}

	nExitPlaceStep = 200;

	/* ******************************** */
	/* ****** "Server-mode" init ****** */
	/* ******************************** */
	{
		const auto runMode = gState.runMode_;
		if (runMode == RunMode::Server || runMode == RunMode::AltServer || runMode == RunMode::AutoAttach)
		{
			_ASSERTE((anWorkMode != ConsoleMainMode::Normal) == (gState.runMode_ == RunMode::AltServer));
		}
	}
	if ((iRc = gpWorker->Init()) != 0)
	{
		nExitPlaceStep = 250;
		goto wrap;
	}

	/* ********************************* */
	/* ****** Start child process ****** */
	/* ********************************* */
#ifdef SHOW_STARTED_PRINT
	sprintf_c(szDbgString, "ConEmuC: PID=%u", GetCurrentProcessId());
	MessageBoxA(0, "Press Ok to continue", szDbgString, MB_SYSTEMMODAL);
#endif

	// Don't use CREATE_NEW_PROCESS_GROUP, or Ctrl-C stops working

	// We need to set `0` before CreateProcess, otherwise we may get timeout,
	// due to misc antivirus software, BEFORE CreateProcess finishes
	if (!(gState.attachMode_ & am_Modes))
		gpWorker->EnableProcessMonitor(false);

	if (gState.noCreateProcess_)
	{
		// Process already started, just attach RealConsole to ConEmu (VirtualConsole)
		lbRc = TRUE;
		pi.hProcess = gpWorker->RootProcessHandle();
		pi.dwProcessId = gpWorker->RootProcessId();
	}
	else
	{
		_ASSERTE(gState.runMode_ != RunMode::AltServer);
		nExitPlaceStep = 350;

		// Process environment variables
		wchar_t* pszExpandedCmd = ParseConEmuSubst(gpszRunCmd);
		if (pszExpandedCmd)
		{
			SafeFree(gpszRunCmd);
			gpszRunCmd = pszExpandedCmd;
		}

		#ifdef _DEBUG
		if (ghFarInExecuteEvent && wcsstr(gpszRunCmd,L"far.exe"))
			ResetEvent(ghFarInExecuteEvent);
		#endif

		wchar_t szSelf[MAX_PATH*2] = L"";
		LPCWSTR pszCurDir = nullptr;
		WARNING("The process handle must have the PROCESS_VM_OPERATION access right!");

		if (gState.dosbox_.use_)
		{
			DosBoxHelp();
		}
		else if (gState.runMode_ == RunMode::Server && !gState.runViaCmdExe_)
		{
			// Проверить, может пытаются запустить GUI приложение как вкладку в ConEmu?
			if (((si.dwFlags & STARTF_USESHOWWINDOW) && (si.wShowWindow == SW_HIDE))
				|| !(si.dwFlags & STARTF_USESHOWWINDOW) || (si.wShowWindow != SW_SHOWNORMAL))
			{
				//_ASSERTEX(si.wShowWindow != SW_HIDE); -- да, окно сервера (консоль) спрятана

				// Имеет смысл, только если окно хотят изначально спрятать
				const wchar_t *psz = gpszRunCmd, *pszStart;
				CmdArg szExe;
				if ((psz = NextArg(psz, szExe, &pszStart)))
				{
					MWow64Disable wow;
					if (!gState.bSkipWowChange_)
						wow.Disable();

					DWORD RunImageSubsystem = 0, RunImageBits = 0, RunFileAttrs = 0;
					bool bSubSystem = GetImageSubsystem(szExe, RunImageSubsystem, RunImageBits, RunFileAttrs);

					if (!bSubSystem)
					{
						// szExe may be simple "notepad", we must seek for executable...
						CEStr szFound;
						// We are interesting only on ".exe" files,
						// supposing that other executable extensions can't be GUI applications
						if (apiSearchPath(nullptr, szExe, L".exe", szFound))
							bSubSystem = GetImageSubsystem(szFound, RunImageSubsystem, RunImageBits, RunFileAttrs);
					}

					if (bSubSystem)
					{
						if (RunImageSubsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI)
						{
							si.dwFlags |= STARTF_USESHOWWINDOW;
							si.wShowWindow = SW_SHOWNORMAL;
						}
					}
				}
			}
		}

		// ConEmuC должен быть максимально прозрачен для конечного процесса
		WARNING("При компиляции gcc все равно прозрачно не получается");
		BOOL lbInheritHandle = TRUE;
		// Если не делать вставку ConEmuC.exe в промежуток между g++.exe и (as.exe или cc1plus.exe)
		// то все хорошо, если вставлять - то лезет куча ошибок вида
		// c:/gcc/mingw/bin/../libexec/gcc/mingw32/4.3.2/cc1plus.exe:1: error: stray '\220' in program
		// c:/gcc/mingw/bin/../libexec/gcc/mingw32/4.3.2/cc1plus.exe:1:4: warning: null character(s) ignored
		// c:/gcc/mingw/bin/../libexec/gcc/mingw32/4.3.2/cc1plus.exe:1: error: stray '\3' in program
		// c:/gcc/mingw/bin/../libexec/gcc/mingw32/4.3.2/cc1plus.exe:1:6: warning: null character(s) ignored
		// Возможно, проблема в наследовании pipe-ов, проверить бы... или в другом SecurityDescriptor.


		#ifdef _DEBUG
		LPCWSTR pszRunCmpApp = nullptr;
		#endif
		CmdArg szExeName;
		{
			LPCWSTR pszStart = gpszRunCmd;
			if ((pszStart = NextArg(pszStart, szExeName)))
			{
				#ifdef _DEBUG
				if (FileExists(szExeName))
				{
					pszRunCmpApp = szExeName;
					pszRunCmpApp = nullptr;
				}
				#endif
			}
		}

		LPSECURITY_ATTRIBUTES lpSec = nullptr; //LocalSecurity();
		//#ifdef _DEBUG
		//		lpSec = nullptr;
		//#endif
		// Не будем разрешать наследование, если нужно - сделаем DuplicateHandle
		lbRc = createProcess(!gState.bSkipWowChange_, nullptr, gpszRunCmd, lpSec,lpSec, lbInheritHandle,
		                      NORMAL_PRIORITY_CLASS/*|CREATE_NEW_PROCESS_GROUP*/
		                      |CREATE_SUSPENDED/*((gpStatus->runMode_ == RunMode::RM_SERVER) ? CREATE_SUSPENDED : 0)*/,
		                      nullptr, pszCurDir, &si, &pi);
		dwErr = GetLastError();

		if (!lbRc && (gState.runMode_ == RunMode::Server) && dwErr == ERROR_FILE_NOT_FOUND)
		{
			// Фикс для перемещения ConEmu.exe в подпапку фара. т.е. far.exe находится на одну папку выше
			if (gsSelfExe[0] != 0)
			{
				wcscpy_c(szSelf, gsSelfExe);

				wchar_t* pszSlash = wcsrchr(szSelf, L'\\');

				if (pszSlash)
				{
					*pszSlash = 0; // получили папку с exe-шником
					pszSlash = wcsrchr(szSelf, L'\\');

					if (pszSlash)
					{
						*pszSlash = 0; // получили родительскую папку
						pszCurDir = szSelf;
						SetCurrentDirectory(pszCurDir);
						// Пробуем еще раз, в родительской директории
						// Не будем разрешать наследование, если нужно - сделаем DuplicateHandle
						lbRc = createProcess(!gState.bSkipWowChange_, nullptr, gpszRunCmd, nullptr,nullptr, FALSE/*TRUE*/,
						                      NORMAL_PRIORITY_CLASS/*|CREATE_NEW_PROCESS_GROUP*/
						                      |CREATE_SUSPENDED/*((gpStatus->runMode_ == RunMode::RM_SERVER) ? CREATE_SUSPENDED : 0)*/,
						                      nullptr, pszCurDir, &si, &pi);
						dwErr = GetLastError();
					}
				}
			}
		}

		//wow.Restore();

		if (lbRc) // && (gpStatus->runMode_ == RunMode::RM_SERVER))
		{
			nExitPlaceStep = 400;

			#ifdef SHOW_INJECT_MSGBOX
			wchar_t szDbgMsg[128], szTitle[128];
			swprintf_c(szTitle, L"%s PID=%u", gsModuleName, GetCurrentProcessId());
			szDbgMsg[0] = 0;
			#endif

			if (gpConsoleArgs->doNotInjectConEmuHk_
				|| (!szExeName.IsEmpty() && IsConsoleServer(szExeName)))
			{
				#ifdef SHOW_INJECT_MSGBOX
				swprintf_c(szDbgMsg, L"%s PID=%u\nConEmuHk injects skipped, PID=%u", gsModuleName, GetCurrentProcessId(), pi.dwProcessId);
				#endif
			}
			else
			{
				TODO("Не только в сервере, но и в ComSpec, чтобы дочерние КОНСОЛЬНЫЕ процессы могли пользоваться редиректами");
				//""F:\VCProject\FarPlugin\ConEmu\Bugs\DOS\TURBO.EXE ""
				// #TODO При выполнении DOS приложений - VirtualAllocEx(hProcess, обламывается!
				TODO("В принципе - завелось, но в сочетании с Anamorphosis получается странное зацикливание far->conemu->anamorph->conemu");

				#ifdef SHOW_INJECT_MSGBOX
				swprintf_c(szDbgMsg, L"%s PID=%u\nInjecting hooks into PID=%u", gsModuleName, GetCurrentProcessId(), pi.dwProcessId);
				MessageBoxW(nullptr, szDbgMsg, szTitle, MB_SYSTEMMODAL);
				#endif

				//BOOL gbLogProcess = FALSE;
				//TODO("Получить из мэппинга glt_Process");
				//#ifdef _DEBUG
				//gbLogProcess = TRUE;
				//#endif
				CINJECTHK_EXIT_CODES iHookRc = CIH_GeneralError/*-1*/;
				if (((gnImageSubsystem == IMAGE_SUBSYSTEM_DOS_EXECUTABLE) || (gnImageBits == 16))
					&& !gState.dosbox_.use_)
				{
					// Если запускается ntvdm.exe - все-равно хук поставить не даст
					iHookRc = CIH_OK/*0*/;
				}
				else
				{
					// Чтобы модуль хуков в корневом процессе знал, что оно корневое
					_ASSERTE(ghRootProcessFlag==nullptr);
					wchar_t szEvtName[64];
					msprintf(szEvtName, countof(szEvtName), CECONEMUROOTPROCESS, pi.dwProcessId);
					ghRootProcessFlag = CreateEvent(LocalSecurity(), TRUE, TRUE, szEvtName);
					if (ghRootProcessFlag)
					{
						SetEvent(ghRootProcessFlag);
					}
					else
					{
						_ASSERTE(ghRootProcessFlag!=nullptr);
					}

					// Теперь ставим хуки
					iHookRc = InjectHooks(pi, gbLogProcess, gsSelfPath, gState.realConWnd_);
				}

				if (iHookRc != CIH_OK/*0*/)
				{
					DWORD nErrCode = GetLastError();
					//_ASSERTE(iHookRc == 0); -- ассерт не нужен, есть MsgBox
					wchar_t szDbgMsg[255], szTitle[128];
					swprintf_c(szTitle, L"ConEmuC[%u], PID=%u", WIN3264TEST(32,64), GetCurrentProcessId());
					swprintf_c(szDbgMsg, L"ConEmuC.M, PID=%u\nInjecting hooks into PID=%u\nFAILED, code=%i:0x%08X", GetCurrentProcessId(), pi.dwProcessId, iHookRc, nErrCode);
					MessageBoxW(nullptr, szDbgMsg, szTitle, MB_SYSTEMMODAL);
				}

				if (gState.dosbox_.use_)
				{
					// Если запустился - то сразу добавим в список процессов (хотя он и не консольный)
					gState.dosbox_.handle_ = pi.hProcess; gState.dosbox_.pid_ = pi.dwProcessId;
					//ProcessAdd(pi.dwProcessId);
				}
			}

			#ifdef SHOW_INJECT_MSGBOX
			wcscat_c(szDbgMsg, L"\nPress OK to resume started process");
			MessageBoxW(nullptr, szDbgMsg, szTitle, MB_SYSTEMMODAL);
			#endif

			// Отпустить процесс (это корневой процесс консоли, например far.exe)
			ResumeThread(pi.hThread);
		}

		if (!lbRc && dwErr == 0x000002E4)
		{
			nExitPlaceStep = 450;
			// Допустимо только в режиме comspec - тогда запустится новая консоль
			_ASSERTE(gState.runMode_ != RunMode::Server);
			PRINT_COMSPEC(L"Vista+: The requested operation requires elevation (ErrCode=0x%08X).\n", dwErr);
			// Vista: The requested operation requires elevation.
			LPCWSTR pszCmd = gpszRunCmd;
			wchar_t szVerb[10];
			CmdArg szExec;

			if ((pszCmd = NextArg(pszCmd, szExec)))
			{
				SHELLEXECUTEINFO sei = {sizeof(SHELLEXECUTEINFO)};
				sei.hwnd = gState.conemuWnd_;
				sei.fMask = SEE_MASK_NO_CONSOLE; //SEE_MASK_NOCLOSEPROCESS; -- смысла ждать завершения нет - процесс запускается в новой консоли
				wcscpy_c(szVerb, L"open"); sei.lpVerb = szVerb;
				sei.lpFile = szExec.ms_Val;
				sei.lpParameters = pszCmd;
				sei.lpDirectory = pszCurDir;
				sei.nShow = SW_SHOWNORMAL;
				MWow64Disable wow; wow.Disable();
				lbRc = ShellExecuteEx(&sei);
				dwErr = GetLastError();
				#ifdef _DEBUG
				OnProcessCreatedDbg(lbRc, dwErr, nullptr, &sei);
				#endif
				wow.Restore();

				if (lbRc)
				{
					// OK
					pi.hProcess = nullptr; pi.dwProcessId = 0;
					pi.hThread = nullptr; pi.dwThreadId = 0;
					// т.к. запустилась новая консоль - подтверждение на закрытие этой точно не нужно
					gState.DisableAutoConfirmExit();
					iRc = 0; goto wrap;
				}
			}
		}
	}

	if (!lbRc)
	{
		nExitPlaceStep = 900;

		PrintExecuteError(gpszRunCmd, dwErr);

		iRc = CERR_CREATEPROCESS; goto wrap;
	}

	if ((gState.attachMode_ & am_Modes))
	{
		// мы цепляемся к уже существующему процессу:
		// аттач из фар плагина или запуск dos-команды в новой консоли через -new_console
		// в последнем случае отключение подтверждения закрытия однозначно некорректно
		// -- DisableAutoConfirmExit(); - низя
	}
	else
	{
		gpWorker->EnableProcessMonitor(true);
	}

	if (pi.dwProcessId)
		AllowSetForegroundWindow(pi.dwProcessId);

#ifdef _DEBUG
	xf_validate(nullptr);
#endif

	/* *************************** */
	/* *** Waiting for startup *** */
	/* *************************** */

	// Don't "lock" startup folder
	UnlockCurrentDirectory();

	if (gState.runMode_ == RunMode::Server)
	{
		//DWORD dwWaitGui = -1;

		nExitPlaceStep = 500;
		gpWorker->SetRootProcessHandle(pi.hProcess); pi.hProcess = nullptr; // Required for Win2k
		gpWorker->SetRootThreadHandle(pi.hThread);  pi.hThread  = nullptr;
		gpWorker->SetRootProcessId(pi.dwProcessId);
		gpWorker->SetRootThreadId(pi.dwThreadId);
		gpWorker->SetRootStartTime(GetTickCount());
		// Скорее всего процесс в консольном списке уже будет
		_ASSERTE(gpSrv != nullptr);
		gpWorker->Processes().CheckProcessCount(TRUE);

		#ifdef _DEBUG
		if (gpWorker->Processes().nProcessCount && !gpWorker->IsDebuggerActive())
		{
			_ASSERTE(gpWorker->Processes().pnProcesses[gpWorker->Processes().nProcessCount-1]!=0);
		}
		#endif

		//if (pi.hProcess) SafeCloseHandle(pi.hProcess);
		//if (pi.hThread) SafeCloseHandle(pi.hThread);

		if (gpSrv->hConEmuGuiAttached)
		{
			DEBUGTEST(DWORD t1 = timeGetTime());

			dwWaitGui = WaitForSingleObject(gpSrv->hConEmuGuiAttached, 1000);

			#ifdef _DEBUG
			DWORD t2 = timeGetTime(), tDur = t2-t1;
			if (tDur > GUIATTACHEVENT_TIMEOUT)
			{
				_ASSERTE(tDur <= GUIATTACHEVENT_TIMEOUT);
			}
			#endif

			if (dwWaitGui == WAIT_OBJECT_0)
			{
				// GUI пайп готов
				swprintf_c(gpSrv->szGuiPipeName, CEGUIPIPENAME, L".", LODWORD(gState.realConWnd_)); // был gnSelfPID //-V205
			}
		}

		// Ждем, пока в консоли не останется процессов (кроме нашего)
		TODO("Проверить, может ли так получиться, что CreateProcess прошел, а к консоли он не прицепился? Может, если процесс GUI");
		// "Подцепление" процесса к консоли наверное может задержать антивирус
		nWait = nWaitExitEvent = WaitForSingleObject(ghExitQueryEvent, CHECK_ANTIVIRUS_TIMEOUT);

		if (nWait != WAIT_OBJECT_0)  // Если таймаут
		{
			iRc = gpWorker->Processes().nProcessCount
				+ (((gpWorker->Processes().nProcessCount==1) && gState.dosbox_.use_ && (WaitForSingleObject(gState.dosbox_.handle_,0)==WAIT_TIMEOUT)) ? 1 : 0);

			// И процессов в консоли все еще нет
			if (iRc == 1 && !gpWorker->IsDebuggerActive())
			{
				if (!gbInShutdown)
				{
					gbTerminateOnCtrlBreak = TRUE;
					gbCtrlBreakStopWaitingShown = TRUE;
					//_printf("Process was not attached to console. Is it GUI?\nCommand to be executed:\n");
					//_wprintf(gpszRunCmd);
					PrintExecuteError(gpszRunCmd, 0, L"Process was not attached to console. Is it GUI?\n");
					_printf("\n\nPress Ctrl+Break to stop waiting\n");

					while (!gbInShutdown && (nWait != WAIT_OBJECT_0))
					{
						nWait = nWaitExitEvent = WaitForSingleObject(ghExitQueryEvent, 250);

						if (nWaitExitEvent == WAIT_OBJECT_0)
						{
							dwWaitRoot = WaitForSingleObject(gpWorker->RootProcessHandle(), 0);
							if (dwWaitRoot == WAIT_OBJECT_0)
							{
								gbTerminateOnCtrlBreak = FALSE;
								gbCtrlBreakStopWaitingShown = FALSE; // сбросим, чтобы ассерты не лезли
							}
							else
							{
								// Root process must be terminated at this point (can't start/initialize)
								_ASSERTE(dwWaitRoot == WAIT_OBJECT_0);
							}
						}

						if ((nWait != WAIT_OBJECT_0) && (gpWorker->Processes().nProcessCount > 1))
						{
							gbTerminateOnCtrlBreak = FALSE;
							gbCtrlBreakStopWaitingShown = FALSE; // сбросим, чтобы ассерты не лезли
							goto wait; // OK, переходим в основной цикл ожидания завершения
						}
					}
				}

				// Что-то при загрузке компа иногда все-таки не дожидается, когда процесс в консоли появится
				_ASSERTE(FALSE && gbTerminateOnCtrlBreak && gbInShutdown);
				iRc = CERR_PROCESSTIMEOUT; goto wrap;
			}
		}
	}
	else if (gState.runMode_ == RunMode::Comspec)
	{
		// В режиме ComSpec нас интересует завершение ТОЛЬКО дочернего процесса
		_ASSERTE(pi.dwProcessId!=0);
		gpWorker->SetRootProcessId(pi.dwProcessId);
		gpWorker->SetRootThreadId(pi.dwThreadId);
	}

	/* *************************** */
	/* *** Ожидание завершения *** */
	/* *************************** */
wait:
#ifdef _DEBUG
	xf_validate(nullptr);
#endif

#ifdef _DEBUG
	if (gState.runMode_ == RunMode::Server)
	{
		gbPipeDebugBoxes = false;
	}
#endif


	// JIC, if there was "goto"
	UnlockCurrentDirectory();


	if (gState.runMode_ == RunMode::AltServer)
	{
		// Alternative server, we can't wait for "self" termination
		iRc = 0;
		goto AltServerDone;
	} // (gpStatus->runMode_ == RunMode::RM_ALTSERVER)
	else if (gState.runMode_ == RunMode::Server)
	{
		nExitPlaceStep = EPS_WAITING4PROCESS/*550*/;
		// There is at least one process in console. Wait until there would be nobody except us.
		nWait = WAIT_TIMEOUT; nWaitExitEvent = -2;

		_ASSERTE(!gpWorker->IsDebuggerActive());

		#ifdef _DEBUG
		while (nWait == WAIT_TIMEOUT)
		{
			nWait = nWaitExitEvent = WaitForSingleObject(ghExitQueryEvent, 100);
			// Not actual? Что-то при загрузке компа иногда все-таки не дожидается, когда процесс в консоли появится
			_ASSERTE(!(gbCtrlBreakStopWaitingShown && (nWait != WAIT_TIMEOUT)));
		}
		#else
		nWait = nWaitExitEvent = WaitForSingleObject(ghExitQueryEvent, INFINITE);
		_ASSERTE(!(gbCtrlBreakStopWaitingShown && (nWait != WAIT_TIMEOUT)));
		#endif
		ShutdownSrvStep(L"ghExitQueryEvent was set");

		#ifdef _DEBUG
		xf_validate(nullptr);
		#endif


		// Root ExitCode
		GetExitCodeProcess(gpWorker->RootProcessHandle(), &gnExitCode);

		nExitPlaceStep = EPS_ROOTPROCFINISHED/*560*/;

		#ifdef _DEBUG
		if (nWait == WAIT_OBJECT_0)
		{
			DEBUGSTRFIN(L"*** FinalizeEvent was set!\n");
		}
		#endif

	} // (gpStatus->runMode_ == RunMode::RM_SERVER)
	else
	{
		nExitPlaceStep = 600;
		//HANDLE hEvents[3];
		//hEvents[0] = pi.hProcess;
		//hEvents[1] = ghCtrlCEvent;
		//hEvents[2] = ghCtrlBreakEvent;
		//WaitForSingleObject(pi.hProcess, INFINITE);
		#ifdef _DEBUG
		xf_validate(nullptr);
		#endif

		DWORD nWaitMS = gpConsoleArgs->asyncRun_ ? 0 : INFINITE;
		nWaitComspecExit = WaitForSingleObject(pi.hProcess, nWaitMS);

		#ifdef _DEBUG
		xf_validate(nullptr);
		#endif

		// Получить ExitCode
		GetExitCodeProcess(pi.hProcess, &gnExitCode);

		#ifdef _DEBUG
		xf_validate(nullptr);
		#endif

		// Close all handles now
		if (pi.hProcess)
			SafeCloseHandle(pi.hProcess);

		if (pi.hThread)
			SafeCloseHandle(pi.hThread);

		#ifdef _DEBUG
		xf_validate(nullptr);
		#endif
	} // (gpStatus->runMode_ == RunMode::RM_COMSPEC)

	/* *********************** */
	/* *** Finalizing work *** */
	/* *********************** */
	iRc = 0;
wrap:
	ShutdownSrvStep(L"Finalizing.1");

#if defined(SHOW_STARTED_PRINT_LITE)
	if (gState.runMode_ == RunMode::Server)
	{
		_printf("\n" WIN3264TEST("ConEmuC.exe","ConEmuC64.exe") " finalizing, PID=%u\n", GetCurrentProcessId());
	}
#endif

	#ifdef VALIDATE_AND_DELAY_ON_TERMINATE
	// Проверка кучи
	xf_validate(nullptr);
	// Отлов изменения высоты буфера
	if (gState.runMode_ == RunMode::Server)
		Sleep(1000);
	#endif

	// К сожалению, HandlerRoutine может быть еще не вызван, поэтому
	// в самой процедуре ExitWaitForKey вставлена проверка флага gbInShutdown
	PRINT_COMSPEC(L"Finalizing. gbInShutdown=%i\n", gbInShutdown);
#ifdef SHOW_STARTED_MSGBOX
	MessageBox(GetConEmuHWND(2), L"Finalizing", (gState.runMode_ == RunMode::Server) ? L"ConEmuC.Server" : L"ConEmuC.ComSpec", 0);
#endif

	#ifdef VALIDATE_AND_DELAY_ON_TERMINATE
	xf_validate(nullptr);
	#endif

	if (iRc == CERR_GUIMACRO_SUCCEEDED)
	{
		iRc = 0;
	}

	if (gState.runMode_ == RunMode::Server && gpWorker->RootProcessHandle())
		GetExitCodeProcess(gpWorker->RootProcessHandle(), &gnExitCode);
	else if (pi.hProcess)
		GetExitCodeProcess(pi.hProcess, &gnExitCode);
	// Ассерт может быть если был запрос на аттач, который не удался
	_ASSERTE(gnExitCode!=STILL_ACTIVE || (iRc==CERR_ATTACHFAILED) || (iRc==CERR_RUNNEWCONSOLE) || gpConsoleArgs->asyncRun_);

	// Log exit code
	if (((gState.runMode_ == RunMode::Server && gpWorker->RootProcessHandle()) ? gpWorker->RootProcessId() : pi.dwProcessId) != 0)
	{
		wchar_t szInfo[80];
		LPCWSTR pszName = (gState.runMode_ == RunMode::Server && gpWorker->RootProcessHandle()) ? L"Shell" : L"Process";
		DWORD nPID = (gState.runMode_ == RunMode::Server && gpWorker->RootProcessHandle()) ? gpWorker->RootProcessId() : pi.dwProcessId;
		if (gnExitCode >= 0x80000000)
			swprintf_c(szInfo, L"\n%s PID=%u ExitCode=%u (%i) {x%08X}", pszName, nPID, gnExitCode, static_cast<int>(gnExitCode), gnExitCode);
		else
			swprintf_c(szInfo, L"\n%s PID=%u ExitCode=%u {x%08X}", pszName, nPID, gnExitCode, gnExitCode);
		LogFunction(szInfo+1);

		if (gpConsoleArgs && gpConsoleArgs->printRetErrLevel_)
		{
			wcscat_c(szInfo, L"\n");
			_wprintf(szInfo);
		}

		// Post information to GUI
		if (gnMainServerPID && !gState.bWasDetached_)
		{
			_ASSERTE(gpWorker != nullptr);
			CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_GETROOTINFO, sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_ROOT_INFO));
			if (pIn && gpWorker->Processes().GetRootInfo(pIn))
			{
				CESERVER_REQ *pSrvOut = ExecuteGuiCmd(gState.realConWnd_, pIn, gState.realConWnd_, TRUE/*async*/);
				ExecuteFreeResult(pSrvOut);
			}
			ExecuteFreeResult(pIn);
		}
	}

	if (iRc && (gState.attachMode_ & am_Auto))
	{
		// Issue 1003: Non zero exit codes leads to problems in some applications...
		iRc = 0;
	}

	ShutdownSrvStep(L"Finalizing.2");

	if (!gbInShutdown  // только если юзер не нажал крестик в заголовке окна, или не удался /ATTACH (чтобы в консоль не гадить)
	        && ((iRc!=0 && iRc!=CERR_RUNNEWCONSOLE && iRc!=CERR_EMPTY_COMSPEC_CMDLINE
					&& iRc!=CERR_UNICODE_CHK_FAILED && iRc!=CERR_UNICODE_CHK_OKAY
					&& iRc!=CERR_GUIMACRO_SUCCEEDED && iRc!=CERR_GUIMACRO_FAILED
					&& iRc!=CERR_AUTOATTACH_NOT_ALLOWED && iRc!=CERR_ATTACHFAILED
					&& iRc!=CERR_WRONG_GUI_VERSION
					&& !(gState.runMode_!=RunMode::Server && iRc==CERR_CREATEPROCESS))
				|| gState.alwaysConfirmExit_)
	  )
	{
		UnlockCurrentDirectory();

		//#ifdef _DEBUG
		//if (!gbInShutdown)
		//	MessageBox(0, L"ExitWaitForKey", L"ConEmuC", MB_SYSTEMMODAL);
		//#endif

		BOOL lbDontShowConsole = FALSE;
		BOOL lbLineFeedAfter = TRUE;

		// console could not react in time for root process exit, so we did the check
		const auto processes = gpWorker->Processes().GetSpawnedProcesses();
		const auto lbProcessesLeft = !processes.empty();

		LPCWSTR pszMsg = nullptr;

		if (lbProcessesLeft)
		{
			pszMsg = L"\n\nPress Enter or Esc to exit...";
			lbDontShowConsole = gState.runMode_ != RunMode::Server;
		}
		else if ((gpConsoleArgs->confirmExitParm_ == RConStartArgs::eConfEmpty)
				|| (gpConsoleArgs->confirmExitParm_ == RConStartArgs::eConfHalt))
		{
			lbLineFeedAfter = FALSE; // Don't print anything to console
		}
		else
		{
			if (gbRootWasFoundInCon == 1)
			{
				// If root process has been working less than CHECK_ROOTOK_TIMEOUT
				if (gState.rootAliveLess10sec_ && (gpConsoleArgs->confirmExitParm_ != RConStartArgs::eConfAlways))
				{
					static wchar_t szMsg[255];
					if (gnExitCode)
					{
						PrintExecuteError(gpszRunCmd, 0, L"\n");
					}
					wchar_t szExitCode[40];
					if (gnExitCode > 255)
						swprintf_c(szExitCode, L"x%08X(%u)", gnExitCode, gnExitCode);
					else
						swprintf_c(szExitCode, L"%u", gnExitCode);
					swprintf_c(szMsg,
						L"\n\nConEmuC: Root process was alive less than 10 sec, ExitCode=%s.\nPress Enter or Esc to close console...",
						szExitCode);
					pszMsg = szMsg;
				}
				else
				{
					pszMsg = L"\n\nPress Enter or Esc to close console...";
				}
			}
		}

		if (!pszMsg
			&& (gpConsoleArgs->confirmExitParm_ != RConStartArgs::eConfEmpty)
			&& (gpConsoleArgs->confirmExitParm_ != RConStartArgs::eConfHalt))
		{
			// Let's show anything (default message)
			pszMsg = L"\n\nPress Enter or Esc to close console, or wait...";

			#ifdef _DEBUG
			static wchar_t szDbgMsg[255];
			swprintf_c(szDbgMsg,
			          L"\n\ngbInShutdown=%i, iRc=%i, gState.alwaysConfirmExit_=%i, nExitQueryPlace=%i"
			          L"%s",
			          (int)gbInShutdown, iRc, (int)gState.alwaysConfirmExit_, nExitQueryPlace,
			          pszMsg);
			pszMsg = szDbgMsg;
			#endif
		}

		DWORD keys = (gpConsoleArgs->confirmExitParm_ == RConStartArgs::eConfHalt) ? 0
			: (VK_RETURN|(VK_ESCAPE<<8));
		ExitWaitForKey(keys, pszMsg, lbLineFeedAfter, lbDontShowConsole);

		// fight optimizer
		std::ignore = processes.size();

		// During the wait, new process may be started in our console
		{
			int nCount = gpWorker->Processes().nProcessCount;

			if ((gpSrv->ConnectInfo.bConnected && (nCount > 1))
				|| gpWorker->IsDebuggerActive())
			{
				// OK, new root found, wait for it
				goto wait;
			}
		}
	}

	// На всякий случай - выставим событие
	if (ghExitQueryEvent)
	{
		_ASSERTE(gbTerminateOnCtrlBreak==FALSE);
		if (!nExitQueryPlace) nExitQueryPlace = 11+(nExitPlaceStep);

		SetTerminateEvent(ste_ConsoleMain);
	}

	// Завершение RefreshThread, InputThread, ServerThread
	if (ghQuitEvent) SetEvent(ghQuitEvent);

	ShutdownSrvStep(L"Finalizing.3");

#ifdef _DEBUG
	xf_validate(nullptr);
#endif

	/* ***************************** */
	/* *** "Режимное" завершение *** */
	/* ***************************** */

	if (gState.runMode_ == RunMode::Server)
	{
		gpWorker->Done(iRc, true);
		//MessageBox(0,L"Server done...",L"ConEmuC",0);
	}
	else if (gState.runMode_ == RunMode::Comspec)
	{
		_ASSERTE(iRc==CERR_RUNNEWCONSOLE || gbComspecInitCalled);
		if (gbComspecInitCalled)
		{
			gpWorker->Done(iRc, false);
		}
		//MessageBox(0,L"Comspec done...",L"ConEmuC",0);
	}

	ShutdownSrvStep(L"Finalizing.4");

	/* ************************** */
	/* *** "Общее" завершение *** */
	/* ************************** */

	#ifdef _DEBUG
	#if 0
	if (gpStatus->runMode_ == RunMode::RM_COMSPEC)
	{
		if (gpszPrevConTitle)
		{
			if (gState.realConWnd)
				SetTitle(gpszPrevConTitle);

		}
	}
	#endif
	SafeFree(gpszPrevConTitle);
	#endif

	SafeCloseHandle(ghRootProcessFlag);

	LogSize(nullptr, 0, "Shutdown");
	//ghConIn.Close();
	ghConOut.Close();
	SafeDelete(gpLogSize);

	//if (wpszLogSizeFile)
	//{
	//	//DeleteFile(wpszLogSizeFile);
	//	free(wpszLogSizeFile); wpszLogSizeFile = nullptr;
	//}

#ifdef _DEBUG
	SafeCloseHandle(ghFarInExecuteEvent);
#endif

	SafeFree(gpszRunCmd);
	SafeFree(gpszForcedTitle);

	CommonShutdown();

	ShutdownSrvStep(L"Finalizing.5");

	// Heap is initialized in DllMain

	// Если режим ComSpec - вернуть код возврата из запущенного процесса
	if (iRc == 0 && gState.runMode_ == RunMode::Comspec)
		iRc = gnExitCode;

#ifdef SHOW_STARTED_MSGBOX
	MessageBox(GetConEmuHWND(2), L"Exiting", (gState.runMode_ == RunMode::Server) ? L"ConEmuC.Server" : L"ConEmuC.ComSpec", 0);
#endif
	if (gpSrv)
	{
		gpSrv->FinalizeFields();
		free(gpSrv);
		gpSrv = nullptr;
	}

AltServerDone:
	// In alternative server mode worker lives in background
	if (gState.runMode_ != RunMode::AltServer)
	{
		SafeDelete(gpWorker);
	}

	ShutdownSrvStep(L"Finalizing done");
	UNREFERENCED_PARAMETER(nWaitDebugExit);
	UNREFERENCED_PARAMETER(nWaitComspecExit);
#if 0
	if (gpStatus->runMode_ == RunMode::RM_SERVER)
	{
		xf_dump();
	}
#endif
	return iRc;
}


int __stdcall ConsoleMain2(const ConsoleMainMode anWorkMode)
{
	return ConsoleMain3(anWorkMode, GetCommandLineW());
}


int WINAPI LogHooksFunction(const wchar_t* str)
{
	if (!gpLogSize) return -1;
	if (!str || !*str) return 0;
	return LogString(str) ? 1 : 0;
}

// Function called from ConEmuHk, ExtendedConsole or ConEmuPlugin to start in-process AltServer.
// Exported as "PrivateEntry" (FN_CONEMUCD_REQUEST_LOCAL_SERVER_NAME).
int WINAPI RequestLocalServer(/*[IN/OUT]*/RequestLocalServerParm* Parm)
{
	//_ASSERTE(FALSE && "ConEmuCD. Continue to RequestLocalServer");

	int iRc = 0;
	wchar_t szName[64];

	if (!Parm || (Parm->StructSize != sizeof(*Parm)))
	{
		iRc = CERR_CARGUMENT;
		goto wrap;
	}

	// Если больше ничего кроме регистрации событий нет
	if ((Parm->Flags & slsf__EventsOnly) == Parm->Flags)
	{
		goto DoEvents;
	}

	Parm->pAnnotation = nullptr;
	Parm->Flags &= ~slsf_PrevAltServerPID;

	// If we have a handle to created screen buffer
	if (Parm->Flags & slsf_SetOutHandle)
	{
		ghConOut.SetHandlePtr(Parm->ppConOutBuffer);
	}

	if (gState.runMode_ != RunMode::AltServer)
	{
		#ifdef SHOW_ALTERNATIVE_MSGBOX
		if (!IsDebuggerPresent())
		{
			wchar_t szMsg[128] = L"";
			msprintf(szMsg, countof(szMsg), L"AltServer: " ConEmuCD_DLL_3264 L" loaded, PID=%u, TID=%u", GetCurrentProcessId(), GetCurrentThreadId());
			MessageBoxW(nullptr, szMsg, L"ConEmu AltServer" WIN3264TEST(""," x64"), 0);
		}
		#endif

		_ASSERTE(gpSrv == nullptr);
		_ASSERTE(gpWorker == nullptr);
		_ASSERTE(gState.runMode_ == RunMode::Undefined);

		// ReSharper disable once CppLocalVariableMayBeConst
		HWND hConEmu = GetConEmuHWND(1/*Gui Main window*/);
		if (!hConEmu || !IsWindow(hConEmu))
		{
			iRc = CERR_GUI_NOT_FOUND;
			goto wrap;
		}

		CONSOLE_SCREEN_BUFFER_INFO csbi{};
		// ReSharper disable once CppLocalVariableMayBeConst
		HANDLE hStdOut = (Parm->Flags & slsf_SetOutHandle) ? ghConOut.GetHandle() : GetStdHandle(STD_OUTPUT_HANDLE);
		if (GetConsoleScreenBufferInfo(hStdOut, &csbi))
		{
			gcrVisibleSize.X = csbi.srWindow.Right - csbi.srWindow.Left + 1;
			gcrVisibleSize.Y = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
			gbParmVisibleSize = FALSE;
			gnBufferHeight = (csbi.dwSize.Y == gcrVisibleSize.Y) ? 0 : csbi.dwSize.Y;
			gnBufferWidth = (csbi.dwSize.X == gcrVisibleSize.X) ? 0 : csbi.dwSize.X;
			gbParmBufSize = (gnBufferHeight != 0);
		}
		_ASSERTE(gcrVisibleSize.X>0 && gcrVisibleSize.X<=400 && gcrVisibleSize.Y>0 && gcrVisibleSize.Y<=300);

		iRc = ConsoleMain2(ConsoleMainMode::AltServer);

		_ASSERTE(gpWorker != nullptr);

		if (iRc == 0)
		{
			if ((Parm->nPrevAltServerPID = WorkerServer::Instance().GetPrevAltServerPid()) != 0)
			{
				Parm->Flags |= slsf_PrevAltServerPID;
			}
		}
	}

	// Если поток RefreshThread был "заморожен" при запуске другого сервера
	if (gpWorker && gpWorker->IsRefreshFreezeRequests() && !(Parm->Flags & slsf_OnAllocConsole))
	{
		gpWorker->ThawRefreshThread();
	}

	TODO("Инициализация TrueColor буфера - Parm->ppAnnotation");

DoEvents:
	if (Parm->Flags & slsf_GetFarCommitEvent)
	{
		if (gpSrv)
		{
			_ASSERTE(gpSrv->hFarCommitEvent != nullptr); // Уже должно быть создано!
		}
		else
		{
			;
		}

		swprintf_c(szName, CEFARWRITECMTEVENT, gnSelfPID);
		Parm->hFarCommitEvent = OpenEvent(EVENT_MODIFY_STATE, FALSE, szName);
		_ASSERTE(Parm->hFarCommitEvent!=nullptr);

		if (Parm->Flags & slsf_FarCommitForce && gpSrv)
		{
			gpSrv->bFarCommitRegistered = TRUE;
		}
	}

	if (Parm->Flags & slsf_GetCursorEvent)
	{
		_ASSERTE(gpSrv && gpSrv->hCursorChangeEvent != nullptr); // Уже должно быть создано!

		swprintf_c(szName, CECURSORCHANGEEVENT, gnSelfPID);
		Parm->hCursorChangeEvent = OpenEvent(EVENT_MODIFY_STATE, FALSE, szName);
		_ASSERTE(Parm->hCursorChangeEvent!=nullptr);

		if (gpSrv)
		{
			gpSrv->bCursorChangeRegistered = TRUE;
		}
	}

	if ((Parm->Flags & slsf_OnFreeConsole) && gpWorker)
	{
		gpWorker->FreezeRefreshThread();
	}

	if (Parm->Flags & slsf_OnAllocConsole)
	{
		gState.realConWnd_ = GetConEmuHWND(2);
		LoadSrvInfoMap();
		//TODO: Request AltServer state from MainServer?

		if (gpWorker)
		{
			gpWorker->ThawRefreshThread();
		}
	}

	Parm->fSrvLogString = LogHooksFunction;

wrap:
	return iRc;
}

//#if defined(CRTSTARTUP)
//extern "C"{
//  BOOL WINAPI _DllMainCRTStartup(HANDLE hDll,DWORD dwReason,LPVOID lpReserved);
//};
//
//BOOL WINAPI mainCRTStartup(HANDLE hDll,DWORD dwReason,LPVOID lpReserved)
//{
//  DllMain(hDll, dwReason, lpReserved);
//  return TRUE;
//}
//#endif

void PrintVersion()
{
	char szProgInfo[255];
	sprintf_c(szProgInfo,
		"ConEmuC build %s %s. " CECOPYRIGHTSTRING_A "\n",
		CONEMUVERS, WIN3264TEST("x86","x64"));
	_printf(szProgInfo);
}

void Help()
{
	PrintVersion();

	// See definition in "ConEmuCD/ConsoleHelp.h"
	_wprintf(pConsoleHelp);
	_wprintf(pNewConsoleHelp);

	// Don't ask keypress before exit
	gbInShutdown = TRUE;
}

void DosBoxHelp()
{
	// See definition in "ConEmuCD/ConsoleHelp.h"
	_wprintf(pDosBoxHelp);
}

void PrintExecuteError(LPCWSTR asCmd, DWORD dwErr, LPCWSTR asSpecialInfo/*=nullptr*/)
{
	if (asSpecialInfo)
	{
		if (*asSpecialInfo)
			_wprintf(asSpecialInfo);
	}
	else
	{
		wchar_t* lpMsgBuf = nullptr;
		DWORD nFmtRc, nFmtErr = 0;

		if (dwErr == 5)
		{
			lpMsgBuf = (wchar_t*)LocalAlloc(LPTR, 128*sizeof(wchar_t));
			_wcscpy_c(lpMsgBuf, 128, L"Access is denied.\nThis may be cause of antiviral or file permissions denial.");
		}
		else
		{
			nFmtRc = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr, dwErr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&lpMsgBuf, 0, nullptr);

			if (!nFmtRc)
				nFmtErr = GetLastError();
		}

		_printf("Can't create process, ErrCode=0x%08X, Description:\n", dwErr);
		_wprintf((lpMsgBuf == nullptr) ? L"<Unknown error>" : lpMsgBuf);
		if (lpMsgBuf) LocalFree(lpMsgBuf);
		UNREFERENCED_PARAMETER(nFmtErr);
	}

	size_t nCchMax = MAX_PATH*2+32;
	wchar_t* lpInfo = (wchar_t*)calloc(nCchMax,sizeof(*lpInfo));
	if (lpInfo)
	{
		_wcscpy_c(lpInfo, nCchMax, L"\nCurrent directory:\n");
			_ASSERTE(nCchMax>=(MAX_PATH*2+32));
		if (gpStartEnv && gpStartEnv->pszWorkDir)
		{
			_wcscat_c(lpInfo, nCchMax, gpStartEnv->pszWorkDir);
		}
		else
		{
			_ASSERTE(gpStartEnv && gpStartEnv->pszWorkDir);
			GetCurrentDirectory(MAX_PATH*2, lpInfo+lstrlen(lpInfo));
		}
		_wcscat_c(lpInfo, nCchMax, L"\n");
		_wprintf(lpInfo);
		free(lpInfo);
	}

	_printf("\nCommand to be executed:\n");
	_wprintf(asCmd ? asCmd : L"<null>");
	_printf("\n");
}

// 1. Substitute vars like: !ConEmuHWND!, !ConEmuDrawHWND!, !ConEmuBackHWND!, !ConEmuWorkDir!
// 2. Expand environment variables (e.g. PowerShell doesn't accept %vars% as arguments)
wchar_t* ParseConEmuSubst(LPCWSTR asCmd)
{
	if (!asCmd || !*asCmd)
	{
		LogFunction(L"ParseConEmuSubst - skipped");
		return nullptr;
	}

	LogFunction(L"ParseConEmuSubst");

	// Only these "ConEmuXXX" variables make sense to pass by "!" instead of "%"
	LPCWSTR szNames[] = {ENV_CONEMUHWND_VAR_W, ENV_CONEMUDRAW_VAR_W, ENV_CONEMUBACK_VAR_W, ENV_CONEMUWORKDIR_VAR_W};

	// If neither of our variables are defined - nothing to do
	wchar_t szFind[] = L"!ConEmu";
	const bool bExclSubst = (StrStrI(asCmd, szFind) != nullptr);
	if (!bExclSubst && (wcschr(asCmd, L'%') == nullptr))
		return nullptr;

#ifdef _DEBUG
	// Variables should be already set for processing
	for (const auto& varName : szNames)
	{
		wchar_t szDbg[MAX_PATH + 1] = L"";
		GetEnvironmentVariable(varName, szDbg, countof(szDbg));
		if (!*szDbg)
		{
			LogFunction(L"Variables must be set already!");
			_ASSERTE(*szDbg && "ConEmuXXX variables should be set already!");
			break; // skip other debug asserts
		}
	}
#endif

	//_ASSERTE(FALSE && "Continue to ParseConEmuSubst");

	wchar_t* pszCmdCopy = nullptr;

	if (bExclSubst)
	{
		if (!((pszCmdCopy = lstrdup(asCmd))))
			return nullptr;

		for (const auto& varName : szNames)
		{
			wchar_t szName[64]; swprintf_c(szName, L"!%s!", varName);
			const size_t iLen = lstrlen(szName);

			wchar_t* pszStart = StrStrI(pszCmdCopy, szName);
			if (!pszStart)
				continue;

			while (pszStart)
			{
				pszStart[0] = L'%';
				pszStart[iLen - 1] = L'%';

				pszStart = StrStrI(pszStart + iLen, szName);
			}
		}

		asCmd = pszCmdCopy;
	}

	wchar_t* pszExpand = ExpandEnvStr(pszCmdCopy ? pszCmdCopy : asCmd);

	SafeFree(pszCmdCopy);
	return pszExpand;
}

BOOL SetTitle(LPCWSTR lsTitle)
{
	LogFunction(L"SetTitle");

	LPCWSTR pszSetTitle = lsTitle ? lsTitle : L"";

	#ifdef SHOW_SETCONTITLE_MSGBOX
	MessageBox(nullptr, pszSetTitle, WIN3264TEST(L"ConEmuCD - set title",L"ConEmuCD64 - set title"), MB_SYSTEMMODAL);
	#endif

	BOOL bRc = SetConsoleTitle(pszSetTitle);

	if (gpLogSize)
	{
		wchar_t* pszLog = lstrmerge(bRc ? L"Done: " : L"Fail: ", pszSetTitle);
		LogFunction(pszLog);
		SafeFree(pszLog);
	}

	return bRc;
}

void UpdateConsoleTitle()
{
	LogFunction(L"UpdateConsoleTitle");

	CmdArg  szTemp;
	wchar_t *pszBuffer = nullptr;
	LPCWSTR  pszSetTitle = nullptr, pszCopy;
	LPCWSTR  pszReq = gpszForcedTitle ? gpszForcedTitle : gpszRunCmd;

	if (!pszReq || !*pszReq)
	{
		// Не должны сюда попадать - сброс заголовка не допустим
		#ifdef _DEBUG
		if (!(gState.attachMode_ & am_Modes))
		{
			_ASSERTE(pszReq && *pszReq);
		}
		#endif
		return;
	}

	pszBuffer = ParseConEmuSubst(pszReq);
	if (pszBuffer)
		pszReq = pszBuffer;
	pszCopy = pszReq;

	if (!gpszForcedTitle && (pszCopy = NextArg(pszCopy, szTemp)))
	{
		wchar_t* pszName = (wchar_t*)PointToName(szTemp.ms_Val);
		wchar_t* pszExt = (wchar_t*)PointToExt(pszName);
		if (pszExt)
			*pszExt = 0;
		pszSetTitle = pszName;
	}
	else
	{
		pszSetTitle = pszReq;
	}

	// Need to change title? Do it.
	if (pszSetTitle && *pszSetTitle)
	{
		#ifdef _DEBUG
		int nLen = 4096; //GetWindowTextLength(gState.realConWnd); -- KIS2009 гундит "Посылка оконного сообщения"...
		gpszPrevConTitle = (wchar_t*)calloc(nLen+1,2);
		if (gpszPrevConTitle)
			GetConsoleTitleW(gpszPrevConTitle, nLen+1);
		#endif

		SetTitle(pszSetTitle);
	}

	SafeFree(pszBuffer);
}

// #ConsoleArgs remove ParseCommandLine
#if 0
// Parse ConEmuC command line switches
int ParseCommandLine(LPCWSTR asCmdLine)
{
	int iRc = CERR_CMDLINEEMPTY;
	CmdArg szArg;
	CEStr szExeTest;
	LPCWSTR pszArgStarts = nullptr;
	gbRunViaCmdExe = TRUE;
	gbRootIsCmdExe = TRUE;
	gbRunInBackgroundTab = FALSE;
	size_t nCmdLine = 0;
	LPCWSTR pwszStartCmdLine = asCmdLine;
	LPCWSTR lsCmdLine = asCmdLine;
	BOOL lbNeedCutStartEndQuot = FALSE;
	bool lbNeedCdToProfileDir = false;

	ConEmuStateCheck eStateCheck = ConEmuStateCheck::None;
	ConEmuExecAction eExecAction = ConEmuExecAction::None;
	MacroInstance MacroInst = {}; // Special ConEmu instance for GUIMACRO and other options

	if (!lsCmdLine || !*lsCmdLine)
	{
		DWORD dwErr = GetLastError();
		_printf("GetCommandLineW failed! ErrCode=0x%08X\n", dwErr);
		return CERR_GETCOMMANDLINE;
	}

	#ifdef _DEBUG
	// To catch debugger start
	//_ASSERTE(wcsstr(lsCmdLine, L"/DEBUGPID=")==0);
	#endif

	// gpStatus->runMode_ = RunMode::Undefined; -- moved to gpStatus ctor

	BOOL lbAttachGuiApp = FALSE;

	struct {
		CEStr szConEmuAddArgs;
		void Append(LPCWSTR asSwitch, LPCWSTR asValue)
		{
			lstrmerge(&szConEmuAddArgs.ms_Val, asSwitch);
			if (asValue && *asValue)
			{
				bool needQuot = IsQuotationNeeded(asValue);
				lstrmerge(&szConEmuAddArgs.ms_Val,
					needQuot ? L" \"" : L" ",
					asValue,
					needQuot ? L"\"" : nullptr);
			}
			SetEnvironmentVariable(ENV_CONEMU_EXEARGS_W, szConEmuAddArgs);
		}
	} AddArgs;

	while ((lsCmdLine = NextArg(lsCmdLine, szArg, &pszArgStarts)))
	{
		iRc = 0;
		xf_check();

		if ((szArg[0] == L'/' || szArg[0] == L'-')
		        && (szArg[1] == L'?' || ((szArg[1] & ~0x20) == L'H'))
		        && szArg[2] == 0)
		{
			Help();
			return CERR_HELPREQUESTED;
		}

		// Following code wants '/'style arguments
		// Change '-'style to '/'style
		if (szArg[0] == L'-')
			szArg.SetAt(0, L'/');
		else if (szArg[0] != L'/')
			continue;

		#ifdef _DEBUG
		if (lstrcmpi(szArg, L"/DEBUGTRAP")==0)
		{
			int i, j = 1; j--; i = 1 / j;
		}
		else
		#endif

		// **** Unit tests ****
		if (lstrcmpi(szArg, L"/Args")==0 || lstrcmpi(szArg, L"/ParseArgs")==0)
		{
			eExecAction = ConEmuExecAction::ParseArgs;
			break;
		}
		else if (lstrcmpi(szArg, L"/ConInfo")==0)
		{
			eExecAction = ConEmuExecAction::PrintConsoleInfo;
			break;
		}
		else if (lstrcmpi(szArg, L"/CheckUnicode")==0)
		{
			eExecAction = ConEmuExecAction::CheckUnicodeFont;
			break;
		}
		else if (lstrcmpi(szArg, L"/TestUnicode")==0)
		{
			eExecAction = ConEmuExecAction::TestUnicodeCvt;
			break;
		}
		else if (lstrcmpi(szArg, L"/OsVerInfo")==0)
		{
			eExecAction = ConEmuExecAction::OsVerInfo;
			break;
		}
		else if (lstrcmpi(szArg, L"/ErrorLevel")==0)
		{
			eExecAction = ConEmuExecAction::ErrorLevel;
			break;
		}
		else if (lstrcmpi(szArg, L"/Result")==0)
		{
			gbPrintRetErrLevel = TRUE;
		}
		else if (lstrcmpi(szArg, L"/echo")==0 || lstrcmpi(szArg, L"/e")==0)
		{
			eExecAction = ConEmuExecAction::OutEcho;
			break;
		}
		else if (lstrcmpi(szArg, L"/type")==0 || lstrcmpi(szArg, L"/t")==0)
		{
			eExecAction = ConEmuExecAction::OutType;
			break;
		}
		// **** Regular use ****
		else if (wcsncmp(szArg, L"/REGCONFONT=", 12)==0)
		{
			eExecAction = ConEmuExecAction::RegConFont;
			lsCmdLine = szArg.Mid(12);
			break;
		}
		else if (wcsncmp(szArg, L"/SETHOOKS=", 10) == 0)
		{
			eExecAction = ConEmuExecAction::InjectHooks;
			lsCmdLine = szArg.Mid(10);
			break;
		}
		else if (wcsncmp(szArg, L"/INJECT=", 8) == 0)
		{
			eExecAction = ConEmuExecAction::InjectRemote;
			lsCmdLine = szArg.Mid(8);
			break;
		}
		else if (wcsncmp(szArg, L"/DEFTRM=", 8) == 0)
		{
			eExecAction = ConEmuExecAction::InjectDefTrm;
			lsCmdLine = szArg.Mid(8);
			break;
		}
		// /GUIMACRO[:PID|0xHWND][:T<tab>][:S<split>] <Macro string>
		else if (lstrcmpni(szArg, L"/GuiMacro", 9) == 0)
		{
			// Все что в lsCmdLine - выполнить в Gui
			ArgGuiMacro(szArg, MacroInst);
			eExecAction = ConEmuExecAction::GuiMacro;
			break;
		}
		else if (lstrcmpi(szArg, L"/STORECWD") == 0)
		{
			eExecAction = ConEmuExecAction::StoreCWD;
			break;
		}
		else if (lstrcmpi(szArg, L"/STRUCT") == 0)
		{
			eExecAction = ConEmuExecAction::DumpStruct;
			break;
		}
		else if (lstrcmpi(szArg, L"/SILENT")==0)
		{
			gpConsoleArgs->preferSilentMode_ = true;
		}
		else if (lstrcmpi(szArg, L"/USEEXPORT")==0)
		{
			gpConsoleArgs->macroExportResult_ = true;
		}
		else if (lstrcmpni(szArg, L"/EXPORT", 7)==0)
		{
			//_ASSERTE(FALSE && "Continue to export");
			if (lstrcmpi(szArg, L"/EXPORT=ALL")==0 || lstrcmpi(szArg, L"/EXPORTALL")==0)
				eExecAction = ConEmuExecAction::ExportAll;
			else if (lstrcmpi(szArg, L"/EXPORT=CON")==0 || lstrcmpi(szArg, L"/EXPORTCON")==0)
				eExecAction = ConEmuExecAction::ExportCon;
			else if (lstrcmpi(szArg, L"/EXPORT=GUI")==0 || lstrcmpi(szArg, L"/EXPORTGUI")==0)
				eExecAction = ConEmuExecAction::ExportGui;
			else
				eExecAction = ConEmuExecAction::ExportTab;
			break;
		}
		else if (lstrcmpi(szArg, L"/IsConEmu")==0)
		{
			eStateCheck = ConEmuStateCheck::IsConEmu;
			break;
		}
		else if (lstrcmpi(szArg, L"/IsTerm")==0)
		{
			eStateCheck = ConEmuStateCheck::IsTerm;
			break;
		}
		else if (lstrcmpi(szArg, L"/IsAnsi")==0)
		{
			eStateCheck = ConEmuStateCheck::IsAnsi;
			break;
		}
		else if (lstrcmpi(szArg, L"/IsAdmin")==0)
		{
			eStateCheck = ConEmuStateCheck::IsAdmin;
			break;
		}
		else if (lstrcmpi(szArg, L"/IsRedirect")==0)
		{
			eStateCheck = ConEmuStateCheck::IsRedirect;
			break;
		}
		else if ((wcscmp(szArg, L"/CONFIRM")==0)
			|| (wcscmp(szArg, L"/CONFHALT")==0)
			|| (wcscmp(szArg, L"/ECONFIRM")==0))
		{
			gpConsoleArgs->confirmExitParm_ = (wcscmp(szArg, L"/CONFIRM")==0) ? RConStartArgs::eConfAlways
				: (wcscmp(szArg, L"/CONFHALT")==0) ? RConStartArgs::eConfHalt
				: RConStartArgs::eConfEmpty;
			gState.alwaysConfirmExit_ = TRUE; gState.autoDisableConfirmExit_ = FALSE;
		}
		else if (wcscmp(szArg, L"/NOCONFIRM")==0)
		{
			gpConsoleArgs->confirmExitParm_ = RConStartArgs::eConfNever;
			gState.alwaysConfirmExit_ = FALSE; gState.autoDisableConfirmExit_ = FALSE;
		}
		else if (wcscmp(szArg, L"/OMITHOOKSWARN")==0)
		{
			gbSkipHookersCheck = true;
		}
		else if (wcscmp(szArg, L"/ADMIN")==0)
		{
			#if defined(SHOW_ATTACH_MSGBOX)
			if (!IsDebuggerPresent())
			{
				wchar_t szTitle[100]; swprintf_c(szTitle, L"%s PID=%u /ADMIN", gsModuleName, gnSelfPID);
				const wchar_t* pszCmdLine = GetCommandLineW();
				MessageBox(nullptr,pszCmdLine,szTitle,MB_SYSTEMMODAL);
			}
			#endif

			gState.attachMode_ |= am_Admin;
			gState.runMode_ = RunMode::Server;
		}
		else if (wcscmp(szArg, L"/ATTACH")==0)
		{
			#if defined(SHOW_ATTACH_MSGBOX)
			if (!IsDebuggerPresent())
			{
				wchar_t szTitle[100]; swprintf_c(szTitle, L"%s PID=%u /ATTACH", gsModuleName, gnSelfPID);
				const wchar_t* pszCmdLine = GetCommandLineW();
				MessageBox(nullptr,pszCmdLine,szTitle,MB_SYSTEMMODAL);
			}
			#endif

			if (!(gState.attachMode_ & am_Modes))
				gState.attachMode_ |= am_Simple;
			gState.runMode_ = RunMode::Server;
		}
		else if ((lstrcmpi(szArg, L"/AUTOATTACH")==0) || (lstrcmpi(szArg, L"/ATTACHDEFTERM")==0))
		{
			#if defined(SHOW_ATTACH_MSGBOX)
			if (!IsDebuggerPresent())
			{
				wchar_t szTitle[100]; swprintf_c(szTitle, L"%s PID=%u %s", gsModuleName, gnSelfPID, szArg.ms_Val);
				const wchar_t* pszCmdLine = GetCommandLineW();
				MessageBox(nullptr,pszCmdLine,szTitle,MB_SYSTEMMODAL);
			}
			#endif

			gState.attachMode_ |= am_Auto;
			gState.alienMode_ = TRUE;
			gState.noCreateProcess_ = TRUE;
			if (lstrcmpi(szArg, L"/AUTOATTACH")==0)
			{
				gState.runMode_ = RunMode::AutoAttach;
				gState.attachMode_ |= am_Async;
			}
			if (lstrcmpi(szArg, L"/ATTACHDEFTERM")==0)
			{
				gState.runMode_ = RunMode::Server;
				gState.attachMode_ |= am_DefTerm;
			}

			// Еще может быть "/GHWND=NEW" но оно ниже. Там ставится "gState.bRequestNewGuiWnd=TRUE"

			//ConEmu autorun (c) Maximus5
			//Starting "%ConEmuPath%" in "Attach" mode (NewWnd=%FORCE_NEW_WND%)

			if (!WorkerServer::Instance().IsAutoAttachAllowed())
			{
				if (gState.realConWnd_ && IsWindowVisible(gState.realConWnd_))
				{
					_printf("AutoAttach was requested, but skipped\n");
				}
				DisableAutoConfirmExit();
				//_ASSERTE(FALSE && "AutoAttach was called while Update process is in progress?");
				return CERR_AUTOATTACH_NOT_ALLOWED;
			}
		}
		else if (wcsncmp(szArg, L"/GUIATTACH=", 11)==0)
		{
			#if defined(SHOW_ATTACH_MSGBOX)
			if (!IsDebuggerPresent())
			{
				wchar_t szTitle[100]; swprintf_c(szTitle, L"%s PID=%u /GUIATTACH", gsModuleName, gnSelfPID);
				const wchar_t* pszCmdLine = GetCommandLineW();
				MessageBox(nullptr,pszCmdLine,szTitle,MB_SYSTEMMODAL);
			}
			#endif

			if (!(gState.attachMode_ & am_Modes))
				gState.attachMode_ |= am_Simple;
			lbAttachGuiApp = TRUE;
			wchar_t* pszEnd;
			HWND2 hAppWnd{ wcstoul(szArg.Mid(11), &pszEnd, 16) };
			if (IsWindow(hAppWnd))
				gpWorker->SetRootProcessGui(hAppWnd);
			gState.runMode_ = RunMode::Server;
		}
		else if (wcscmp(szArg, L"/NOCMD")==0)
		{
			gState.runMode_ = RunMode::Server;
			gState.noCreateProcess_ = TRUE;
			gState.alienMode_ = TRUE;
		}
		else if (wcsncmp(szArg, L"/PARENTFARPID=", 14)==0)
		{
			// Для режима RunMode::RM_COMSPEC нужно будет сохранить "длинный вывод"
			wchar_t* pszEnd = nullptr, *pszStart;
			pszStart = szArg.ms_Val+14;
			gpWorker->SetParentFarPid(wcstoul(pszStart, &pszEnd, 10));
		}
		else if (wcscmp(szArg, L"/CREATECON")==0)
		{
			gpConsoleArgs->creatingHiddenConsole_ = TRUE;
			//_ASSERTE(FALSE && "Continue to create con");
		}
		else if (wcscmp(szArg, L"/ROOTEXE")==0)
		{
			if ((lsCmdLine = NextArg(lsCmdLine, szArg)))
				gpszRootExe = lstrmerge(L"\"", szArg, L"\"");
		}
		else if (wcsncmp(szArg, L"/PID=", 5)==0 || wcsncmp(szArg, L"/TRMPID=", 8)==0
			|| wcsncmp(szArg, L"/FARPID=", 8)==0 || wcsncmp(szArg, L"/CONPID=", 8)==0)
		{
			gState.runMode_ = RunMode::Server;
			gState.noCreateProcess_ = TRUE; // Процесс УЖЕ запущен
			gState.alienMode_ = TRUE; // Консоль создана НЕ нами
			wchar_t* pszEnd = nullptr, *pszStart;

			if (wcsncmp(szArg, L"/TRMPID=", 8)==0)
			{
				// This is called from *.vshost.exe when "AllocConsole" just created
				gbDefTermCall = TRUE;
				gbDontInjectConEmuHk = TRUE;
				pszStart = szArg.ms_Val+8;
			}
			else if (wcsncmp(szArg, L"/FARPID=", 8)==0)
			{
				gbAttachFromFar = TRUE;
				gbRootIsCmdExe = FALSE;
				pszStart = szArg.ms_Val+8;
			}
			else if (wcsncmp(szArg, L"/CONPID=", 8)==0)
			{
				//_ASSERTE(FALSE && "Continue to alternative attach mode");
				gbAlternativeAttach = TRUE;
				gbRootIsCmdExe = FALSE;
				pszStart = szArg.ms_Val+8;
			}
			else
			{
				pszStart = szArg.ms_Val+5;
			}

			gpWorker->SetRootProcessId(wcstoul(pszStart, &pszEnd, 10));

			if ((gpWorker->RootProcessId() == 0) && gpConsoleArgs->creatingHiddenConsole_)
			{
				gpWorker->SetRootProcessId(WaitForRootConsoleProcess(30000));
			}

			// --
			//if (gpWorker->RootProcessId())
			//{
			//	_ASSERTE(gpWorker->RootProcessHandle()==nullptr); // Еще не должен был быть открыт
			//	gpWorker->RootProcessHandle() = OpenProcess(MY_PROCESS_ALL_ACCESS, FALSE, gpWorker->RootProcessId());
			//	if (gpWorker->RootProcessHandle() == nullptr)
			//	{
			//		gpWorker->RootProcessHandle() = OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION, FALSE, gpWorker->RootProcessId());
			//	}
			//}

			if (gbAlternativeAttach && gpWorker->RootProcessId())
			{
				// Если процесс был запущен "с консольным окном"
				if (gState.realConWnd_)
				{
					#ifdef _DEBUG
					SafeCloseHandle(ghFarInExecuteEvent);
					#endif
				}

				BOOL bAttach = StdCon::AttachParentConsole(gpWorker->RootProcessId());
				if (!bAttach)
				{
					gbInShutdown = TRUE;
					gState.alwaysConfirmExit_ = FALSE;
					LogString(L"CERR_CARGUMENT: (gbAlternativeAttach && gpWorker->RootProcessId())");
					return CERR_CARGUMENT;
				}

				// Need to be set, because of new console === new handler
				SetConsoleCtrlHandler((PHANDLER_ROUTINE)HandlerRoutine, true);

				#ifdef _DEBUG
				_ASSERTE(ghFarInExecuteEvent==nullptr);
				_ASSERTE(gState.realConWnd_!=nullptr);
				#endif
			}
			else if (gpWorker->RootProcessId() == 0)
			{
				LogString("CERR_CARGUMENT: Attach to GUI was requested, but invalid PID specified");
				_printf("Attach to GUI was requested, but invalid PID specified:\n");
				_wprintf(GetCommandLineW());
				_printf("\n");
				_ASSERTE(FALSE && "Attach to GUI was requested, but invalid PID specified");
				return CERR_CARGUMENT;
			}
		}
		else if (wcsncmp(szArg, L"/CINMODE=", 9)==0)
		{
			wchar_t* pszEnd = nullptr, *pszStart = szArg.ms_Val+9;
			gnConsoleModeFlags = wcstoul(pszStart, &pszEnd, 16);
			// если передан 0 - включится (ENABLE_QUICK_EDIT_MODE|ENABLE_EXTENDED_FLAGS|ENABLE_INSERT_MODE)
			gbConsoleModeFlags = (gnConsoleModeFlags != 0);
		}
		else if (wcscmp(szArg, L"/HIDE")==0)
		{
			gbForceHideConWnd = TRUE;
		}
		else if (wcsncmp(szArg, L"/B", 2)==0)
		{
			wchar_t* pszEnd = nullptr;

			if (wcsncmp(szArg, L"/BW=", 4)==0)
			{
				gcrVisibleSize.X = /*_wtoi(szArg+4);*/(SHORT)wcstol(szArg.Mid(4),&pszEnd,10); gbParmVisibleSize = TRUE;
			}
			else if (wcsncmp(szArg, L"/BH=", 4)==0)
			{
				gcrVisibleSize.Y = /*_wtoi(szArg+4);*/(SHORT)wcstol(szArg.Mid(4),&pszEnd,10); gbParmVisibleSize = TRUE;
			}
			else if (wcsncmp(szArg, L"/BZ=", 4)==0)
			{
				gnBufferHeight = /*_wtoi(szArg+4);*/(SHORT)wcstol(szArg.Mid(4),&pszEnd,10); gbParmBufSize = TRUE;
			}
			TODO("/BX для ширины буфера?");
		}
		else if (wcsncmp(szArg, L"/F", 2)==0 && szArg[2] && szArg[3] == L'=')
		{
			wchar_t* pszEnd = nullptr;
			_ASSERTE(gpSrv != nullptr);

			if (wcsncmp(szArg, L"/FN=", 4)==0) //-V112
			{
				lstrcpynW(gpSrv->szConsoleFont, szArg.Mid(4), 32); //-V112
			}
			else if (wcsncmp(szArg, L"/FW=", 4)==0) //-V112
			{
				gpSrv->nConFontWidth = /*_wtoi(szArg+4);*/(SHORT)wcstol(szArg.Mid(4),&pszEnd,10);
			}
			else if (wcsncmp(szArg, L"/FH=", 4)==0) //-V112
			{
				gpSrv->nConFontHeight = /*_wtoi(szArg+4);*/(SHORT)wcstol(szArg.Mid(4),&pszEnd,10);
				//} else if (wcsncmp(szArg, L"/FF=", 4)==0) {
				//  lstrcpynW(gpSrv->szConsoleFontFile, szArg+4, MAX_PATH);
			}
		}
		else if (lstrcmpni(szArg, L"/LOG", 4) == 0) //-V112
		{
			int nLevel = 0;
			if (szArg[4]==L'1') nLevel = 1; else if (szArg[4]>=L'2') nLevel = 2;

			CreateLogSizeFile(nLevel);
		}
		else if (wcsncmp(szArg, L"/GID=", 5)==0)
		{
			gState.runMode_ = RunMode::Server;
			wchar_t* pszEnd = nullptr;
			gState.conemuPid_ = wcstoul(szArg.Mid(5), &pszEnd, 10);

			if (gState.conemuPid_ == 0)
			{
				LogString(L"CERR_CARGUMENT: Invalid GUI PID specified");
				_printf("Invalid GUI PID specified:\n");
				_wprintf(GetCommandLineW());
				_printf("\n");
				_ASSERTE(FALSE);
				return CERR_CARGUMENT;
			}
		}
		else if (wcsncmp(szArg, L"/AID=", 5)==0)
		{
			wchar_t* pszEnd = nullptr;
			gState.dwGuiAID = wcstoul(szArg.Mid(5), &pszEnd, 10);
		}
		else if (wcsncmp(szArg, L"/GHWND=", 7)==0)
		{
			if (gState.runMode_ == RunMode::Undefined)
			{
				gState.runMode_ = RunMode::Server;
			}
			else
			{
				_ASSERTE(gState.runMode_ == RunMode::AutoAttach || gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer);
			}

			wchar_t* pszEnd = nullptr;
			if (lstrcmpi(szArg.Mid(7), L"NEW") == 0)
			{
				gState.hGuiWnd = nullptr;
				_ASSERTE(gState.conemuPid_ == 0);
				gState.conemuPid_ = 0;
				gState.bRequestNewGuiWnd = TRUE;
			}
			else
			{
				wchar_t szLog[120];
				LPCWSTR pszDescr = szArg.Mid(7);
				if (pszDescr[0] == L'0' && (pszDescr[1] == L'x' || pszDescr[1] == L'X'))
					pszDescr += 2; // That may be useful for calling from batch files
				gState.hGuiWnd = (HWND)(UINT_PTR)wcstoul(pszDescr, &pszEnd, 16);
				gState.bRequestNewGuiWnd = FALSE;

				BOOL isWnd = gState.hGuiWnd ? IsWindow(gState.hGuiWnd) : FALSE;
				DWORD nErr = gState.hGuiWnd ? GetLastError() : 0;

				swprintf_c(szLog, L"GUI HWND=0x%08X, %s, ErrCode=%u", LODWORD(gState.hGuiWnd), isWnd ? L"Valid" : L"Invalid", nErr);
				LogString(szLog);

				if (!isWnd)
				{
					LogString(L"CERR_CARGUMENT: Invalid GUI HWND was specified in /GHWND arg");
					_printf("Invalid GUI HWND specified: ");
					_wprintf(szArg);
					_printf("\n" "Command line:\n");
					_wprintf(GetCommandLineW());
					_printf("\n");
					_ASSERTE(FALSE && "Invalid window was specified in /GHWND arg");
					return CERR_CARGUMENT;
				}

				DWORD nPID = 0;
				GetWindowThreadProcessId(gState.hGuiWnd, &nPID);
				_ASSERTE(gState.conemuPid_ == 0 || gState.conemuPid_ == nPID);
				gState.conemuPid_ = nPID;
			}
		}
		else if (wcsncmp(szArg, L"/TA=", 4)==0)
		{
			wchar_t* pszEnd = nullptr;
			DWORD nColors = wcstoul(szArg.Mid(4), &pszEnd, 16);
			if (nColors)
			{
				DWORD nTextIdx = (nColors & 0xFF);
				DWORD nBackIdx = ((nColors >> 8) & 0xFF);
				DWORD nPopTextIdx = ((nColors >> 16) & 0xFF);
				DWORD nPopBackIdx = ((nColors >> 24) & 0xFF);

				if ((nTextIdx <= 15) && (nBackIdx <= 15) && (nTextIdx != nBackIdx))
					gnDefTextColors = MAKECONCOLOR(nTextIdx, nBackIdx);

				if ((nPopTextIdx <= 15) && (nPopBackIdx <= 15) && (nPopTextIdx != nPopBackIdx))
					gnDefPopupColors = MAKECONCOLOR(nPopTextIdx, nPopBackIdx);

				HANDLE hConOut = ghConOut;
				CONSOLE_SCREEN_BUFFER_INFO csbi5 = {};
				GetConsoleScreenBufferInfo(hConOut, &csbi5);

				if (gnDefTextColors || gnDefPopupColors)
				{
					BOOL bPassed = FALSE;

					if (gnDefPopupColors && (gnOsVer >= 0x600))
					{
						MY_CONSOLE_SCREEN_BUFFER_INFOEX csbi = {sizeof(csbi)};
						if (apiGetConsoleScreenBufferInfoEx(hConOut, &csbi))
						{
							// Microsoft bug? When console is started elevated - it does NOT show
							// required attributes, BUT GetConsoleScreenBufferInfoEx returns them.
							if (!(gState.attachMode_ & am_Admin)
								&& (!gnDefTextColors || (csbi.wAttributes = gnDefTextColors))
								&& (!gnDefPopupColors || (csbi.wPopupAttributes = gnDefPopupColors)))
							{
								bPassed = TRUE; // Менять не нужно, консоль соответствует
							}
							else
							{
								if (gnDefTextColors)
									csbi.wAttributes = gnDefTextColors;
								if (gnDefPopupColors)
									csbi.wPopupAttributes = gnDefPopupColors;

								_ASSERTE(FALSE && "Continue to SetConsoleScreenBufferInfoEx");

								// Vista/Win7. _SetConsoleScreenBufferInfoEx unexpectedly SHOWS console window
								//if (gnOsVer == 0x0601)
								//{
								//	RECT rcGui = {};
								//	if (gState.hGuiWnd)
								//		GetWindowRect(gState.hGuiWnd, &rcGui);
								//	//SetWindowPos(gState.realConWnd, HWND_BOTTOM, rcGui.left+3, rcGui.top+3, 0,0, SWP_NOSIZE|SWP_SHOWWINDOW|SWP_NOZORDER);
								//	SetWindowPos(gState.realConWnd, nullptr, -30000, -30000, 0,0, SWP_NOSIZE|SWP_SHOWWINDOW|SWP_NOZORDER);
								//	apiShowWindow(gState.realConWnd, SW_SHOWMINNOACTIVE);
								//	#ifdef _DEBUG
								//	apiShowWindow(gState.realConWnd, SW_SHOWNORMAL);
								//	apiShowWindow(gState.realConWnd, SW_HIDE);
								//	#endif
								//}

								bPassed = apiSetConsoleScreenBufferInfoEx(hConOut, &csbi);

								// Что-то Win7 хулиганит
								if (!gbVisibleOnStartup)
								{
									apiShowWindow(gState.realConWnd_, SW_HIDE);
								}
							}
						}
					}


					if (!bPassed && gnDefTextColors)
					{
						SetConsoleTextAttribute(hConOut, gnDefTextColors);
						RefillConsoleAttributes(csbi5, csbi5.wAttributes, gnDefTextColors);
					}
				}
			}
		}
		else if (lstrcmpni(szArg, L"/DEBUGPID=", 10)==0)
		{
			//gpStatus->runMode_ = RunMode::RM_SERVER; -- don't set, the RunMode::RM_UNDEFINED is flag for debugger only
			const auto dbgRc = gpWorker->SetDebuggingPid(szArg.Mid(10));
			if (dbgRc != 0)
				return dbgRc;
		}
		else if (lstrcmpi(szArg, L"/DEBUGEXE")==0 || lstrcmpi(szArg, L"/DEBUGTREE")==0)
		{
			//gpStatus->runMode_ = RunMode::RM_SERVER; -- don't set, the RunMode::RM_UNDEFINED is flag for debugger only
			const bool debugTree = (lstrcmpi(szArg, L"/DEBUGTREE") == 0);
			const auto dbgRc = gpWorker->SetDebuggingExe(lsCmdLine, debugTree);
			if (dbgRc != 0)
				return dbgRc;
			// STOP processing rest of command line, it goes to debugger
			break;
		}
		else if (lstrcmpi(szArg, L"/DUMP")==0)
		{
			gpWorker->SetDebugDumpType(DumpProcessType::AskUser);
		}
		else if (lstrcmpi(szArg, L"/MINIDUMP")==0 || lstrcmpi(szArg, L"/MINI")==0)
		{
			gpWorker->SetDebugDumpType(DumpProcessType::MiniDump);
		}
		else if (lstrcmpi(szArg, L"/FULLDUMP")==0 || lstrcmpi(szArg, L"/FULL")==0)
		{
			gpWorker->SetDebugDumpType(DumpProcessType::FullDump);
		}
		else if (lstrcmpi(szArg, L"/AUTOMINI")==0)
		{
			//_ASSERTE(FALSE && "Continue to /AUTOMINI");
			const wchar_t* interval = nullptr;
			if (lsCmdLine && *lsCmdLine && isDigit(lsCmdLine[0])
				&& ((lsCmdLine = NextArg(lsCmdLine, szArg, &pszArgStarts))))
				interval = szArg.ms_Val;
			gpWorker->SetDebugAutoDump(interval);
		}
		else if (lstrcmpi(szArg, L"/PROFILECD")==0)
		{
			lbNeedCdToProfileDir = true;
		}
		else if (lstrcmpi(szArg, L"/CONFIG")==0)
		{
			if (!(lsCmdLine = NextArg(lsCmdLine, szArg)))
			{
				iRc = CERR_CMDLINEEMPTY;
				_ASSERTE(FALSE && "Config name was not specified!");
				_wprintf(L"Config name was not specified!\r\n");
				break;
			}
			// Reuse config if starting "ConEmu.exe" from console server!
			SetEnvironmentVariable(ENV_CONEMU_CONFIG_W, szArg);
			AddArgs.Append(L"-config", szArg);
		}
		else if (lstrcmpi(szArg, L"/LoadCfgFile")==0)
		{
			// Reuse specified xml file if starting "ConEmu.exe" from console server!
			#ifdef SHOW_LOADCFGFILE_MSGBOX
			MessageBox(nullptr, lsCmdLine, L"/LoadCfgFile", MB_SYSTEMMODAL);
			#endif
			if (!(lsCmdLine = NextArg(lsCmdLine, szArg)))
			{
				iRc = CERR_CMDLINEEMPTY;
				_ASSERTE(FALSE && "Xml file name was not specified!");
				_wprintf(L"Xml file name was not specified!\r\n");
				break;
			}
			AddArgs.Append(L"-LoadCfgFile", szArg);
		}
		else if (lstrcmpi(szArg, L"/ASYNC") == 0 || lstrcmpi(szArg, L"/FORK") == 0)
		{
			gpConsoleArgs->asyncRun_ = TRUE;
		}
		else if (lstrcmpi(szArg, L"/NOINJECT")==0)
		{
			gbDontInjectConEmuHk = TRUE;
		}
		else if (lstrcmpi(szArg, L"/DOSBOX")==0)
		{
			gState.dosbox_.use_ = TRUE;
		}
		else if (szArg.IsSwitch(L"-STD"))
		{
			StdCon::gState.reopenStdHandles_ = true;
		}
		// После этих аргументов - идет то, что передается в CreateProcess!
		else if (lstrcmpi(szArg, L"/ROOT")==0)
		{
			#ifdef SHOW_SERVER_STARTED_MSGBOX
			ShowServerStartedMsgBox(asCmdLine);
			#endif
			gState.runMode_ = RunMode::Server; gState.noCreateProcess_ = FALSE;
			gpConsoleArgs->asyncRun_ = FALSE;
			SetWorkEnvVar();
			break; // lsCmdLine уже указывает на запускаемую программу
		}
		// После этих аргументов - идет то, что передается в COMSPEC (CreateProcess)!
		//if (wcscmp(szArg, L"/C")==0 || wcscmp(szArg, L"/c")==0 || wcscmp(szArg, L"/K")==0 || wcscmp(szArg, L"/k")==0) {
		else if (szArg[0] == L'/' && (((szArg[1] & ~0x20) == L'C') || ((szArg[1] & ~0x20) == L'K')))
		{
			gState.noCreateProcess_ = FALSE;

			//_ASSERTE(FALSE && "ConEmuC -c ...");

			if (szArg[2] == 0)  // "/c" или "/k"
				gState.runMode_ = RunMode::Comspec;

			if (gState.runMode_ == RunMode::Undefined && szArg[4] == 0
			        && ((szArg[2] & ~0x20) == L'M') && ((szArg[3] & ~0x20) == L'D'))
			{
				_ASSERTE(FALSE && "'/cmd' obsolete switch. use /c, /k, /root");
				gState.runMode_ = RunMode::Server;
			}

			// Если тип работа до сих пор не определили - считаем что режим ComSpec
			// и команда начинается сразу после /c (может быть "cmd /cecho xxx")
			if (gState.runMode_ == RunMode::Undefined)
			{
				gState.runMode_ = RunMode::Comspec;
				// Поддержка возможности "cmd /cecho xxx"
				lsCmdLine = SkipNonPrintable(pszArgStarts + 2);
			}

			if (gState.runMode_ == RunMode::Comspec)
			{
				gpWorker->SetCmdK((szArg[1] & ~0x20) == L'K');
			}

			if (lsCmdLine && (lsCmdLine[0] == TaskBracketLeft) && wcschr(lsCmdLine, TaskBracketRight))
			{
				// Allow smth like: ConEmuC -c {Far} /e text.txt
				gpszTaskCmd = ExpandTaskCmd(lsCmdLine);
				if (gpszTaskCmd && *gpszTaskCmd)
					lsCmdLine = gpszTaskCmd;
			}

			break; // lsCmdLine уже указывает на запускаемую программу
		}
		else
		{
			_ASSERTE(FALSE && "Unknown switch!");
			_wprintf(L"Unknown switch: ");
			_wprintf(szArg);
			_wprintf(L"\r\n");
		}
	}

	LogFunction(L"ParseCommandLine{in-progress}");

	// Switch "/PROFILECD" used when server to be started under different credentials as GUI.
	// So, we need to do "cd %USERPROFILE%" which is more suitable to user.
	if (lbNeedCdToProfileDir)
	{
		CdToProfileDir();
	}

	// Some checks or actions
	if ((eStateCheck != ConEmuStateCheck::None) || (eExecAction != ConEmuExecAction::None))
	{
		int iFRc = CERR_CARGUMENT;

		if (eStateCheck != ConEmuStateCheck::None)
		{
			bool bOn = DoStateCheck(eStateCheck);
			iFRc = bOn ? CERR_CHKSTATE_ON : CERR_CHKSTATE_OFF;
		}
		else if (eExecAction != ConEmuExecAction::None)
		{
			iFRc = DoExecAction(eExecAction, lsCmdLine, MacroInst);
		}

		// immediately go to exit
		gbInShutdown = TRUE;
		return iFRc;
	}

	if ((gState.attachMode_ & am_DefTerm) && !gbParmVisibleSize)
	{
		// To avoid "small" and trimmed text after starting console
		_ASSERTE(gcrVisibleSize.X==80 && gcrVisibleSize.Y==25);
		gbParmVisibleSize = TRUE;
	}

	// Параметры из комстроки разобраны. Здесь могут уже быть известны
	// gState.hGuiWnd {/GHWND}, gState.conemuPid_ {/GPID}, gState.dwGuiAID {/AID}
	// gpStatus->attachMode_ для ключей {/ADMIN}, {/ATTACH}, {/AUTOATTACH}, {/GUIATTACH}

	// В принципе, gpStatus->attachMode_ включается и при "/ADMIN", но при запуске из ConEmu такого быть не может,
	// будут установлены и gState.hGuiWnd, и gState.conemuPid_

	// Issue 364, например, идет билд в VS, запускается CustomStep, в этот момент автоаттач нафиг не нужен
	// Теоретически, в Студии не должно бы быть запуска ConEmuC.exe, но он может оказаться в "COMSPEC", так что проверим.
	if (gState.attachMode_
		&& ((gState.runMode_ == RunMode::Server) || (gState.runMode_ == RunMode::AutoAttach))
		&& (gState.conemuPid_ == 0))
	{
		//-- ассерт не нужен вроде
		//_ASSERTE(!gbAlternativeAttach && "Alternative mode must be already processed!");

		BOOL lbIsWindowVisible = FALSE;
		// Добавим проверку на telnet
		if (!gState.realConWnd_
			|| !(lbIsWindowVisible = gpConsoleArgs->IsAutoAttachAllowed())
			|| isTerminalMode())
		{
			if (gpLogSize)
			{
				if (!gState.realConWnd_) { LogFunction(L"!gState.realConWnd"); }
				else if (!lbIsWindowVisible) { LogFunction(L"!IsAutoAttachAllowed"); }
				else { LogFunction(L"isTerminalMode"); }
			}
			// Но это может быть все-таки наше окошко. Как проверить...
			// Найдем первый параметр
			LPCWSTR pszSlash = lsCmdLine ? wcschr(lsCmdLine, L'/') : nullptr;
			if (pszSlash)
			{
				LogFunction(pszSlash);
				// И сравним с используемыми у нас. Возможно потом еще что-то добавить придется
				if (wmemcmp(pszSlash, L"/DEBUGPID=", 10) != 0)
					pszSlash = nullptr;
			}
			if (pszSlash == nullptr)
			{
				// Не наше окошко, выходим
				gbInShutdown = TRUE;
				return CERR_ATTACH_NO_CONWND;
			}
		}

		if (!gpConsoleArgs->alternativeAttach_ && !(gState.attachMode_ & am_DefTerm) && !gpWorker->RootProcessId())
		{
			// В принципе, сюда мы можем попасть при запуске, например: "ConEmuC.exe /ADMIN /ROOT cmd"
			// Но только не при запуске "из ConEmu" (т.к. будут установлены gState.hGuiWnd, gState.conemuPid_)

			// Из батника убрал, покажем инфу тут
			PrintVersion();
			char szAutoRunMsg[128];
			sprintf_c(szAutoRunMsg, "Starting attach autorun (NewWnd=%s)\n",
				gpConsoleArgs->requestNewGuiWnd_ ? "YES" : "NO");
			_printf(szAutoRunMsg);
		}
	}

	xf_check();

	// Debugger or minidump requested?
	// Switches ‘/DEBUGPID=PID1[,PID2[...]]’ to debug already running process
	// or ‘/DEBUGEXE <your command line>’ or ‘/DEBUGTREE <your command line>’
	// to start new process and debug it (and its children if ‘/DEBUGTREE’)
	if (gpWorker->IsDebugProcess())
	{
		_ASSERTE(gState.runMode_ == RunMode::Undefined);
		// Run debugger thread and wait for its completion
		int iDbgRc = gpWorker->DbgInfo().RunDebugger();
		return iDbgRc;
	}

	// Validate Сonsole (find it may be) or ChildGui process we need to attach into ConEmu window
	if (((gState.runMode_ == RunMode::Server) || (gState.runMode_ == RunMode::AutoAttach))
		&& (gState.noCreateProcess_ && gState.attachMode_))
	{
		// Проверить процессы в консоли, подобрать тот, который будем считать "корневым"
		int nChk = CheckAttachProcess();

		if (nChk != 0)
			return nChk;

		gpszRunCmd = lstrdup(L"");

		if (!gpszRunCmd)
		{
			_printf("Can't allocate 1 wchar!\n");
			return CERR_NOTENOUGHMEM1;
		}

		return 0;
	}

	xf_check();

	// iRc is result of our ‘NextArg(&lsCmdLine,...)’
	if (iRc != 0)
	{
		if (iRc == CERR_CMDLINEEMPTY)
		{
			Help();
			_printf("\n\nParsing command line failed (/C argument not found):\n");
			_wprintf(GetCommandLineW());
			_printf("\n");
		}
		else
		{
			_printf("Parsing command line failed:\n");
			_wprintf(asCmdLine);
			_printf("\n");
		}

		return iRc;
	}

	if (gState.runMode_ == RunMode::Undefined)
	{
		LogString(L"CERR_CARGUMENT: Parsing command line failed (/C argument not found)");
		_printf("Parsing command line failed (/C argument not found):\n");
		_wprintf(GetCommandLineW());
		_printf("\n");
		_ASSERTE(FALSE);
		return CERR_CARGUMENT;
	}

	xf_check();

	// Prepare our environment and GUI window
	if (gState.runMode_ == RunMode::Server)
	{
		// We need to reserve or start new ConEmu tab/window...

		// Если уже известен HWND ConEmu (root window)
		if (gState.hGuiWnd)
		{
			DWORD nGuiPID = 0; GetWindowThreadProcessId(gState.hGuiWnd, &nGuiPID);
			DWORD nWrongValue = 0;
			SetLastError(0);
			LGSResult lgsRc = ReloadGuiSettings(nullptr, &nWrongValue);
			if (lgsRc < lgs_Succeeded)
			{
				wchar_t szLgsError[200], szLGS[80];
				swprintf_c(szLGS, L"LGS=%u, Code=%u, GUI PID=%u, Srv PID=%u", lgsRc, GetLastError(), nGuiPID, GetCurrentProcessId());
				switch (lgsRc)
				{
				case lgs_WrongVersion:
					swprintf_c(szLgsError, L"Failed to load ConEmu info!\n"
						L"Found ProtocolVer=%u but Required=%u.\n"
						L"%s.\n"
						L"Please update all ConEmu components!",
						nWrongValue, (DWORD)CESERVER_REQ_VER, szLGS);
					break;
				case lgs_WrongSize:
					swprintf_c(szLgsError, L"Failed to load ConEmu info!\n"
						L"Found MapSize=%u but Required=%u."
						L"%s.\n"
						L"Please update all ConEmu components!",
						nWrongValue, (DWORD)sizeof(ConEmuGuiMapping), szLGS);
					break;
				default:
					swprintf_c(szLgsError, L"Failed to load ConEmu info!\n"
						L"%s.\n"
						L"Please update all ConEmu components!",
						szLGS);
				}
				// Add log info
				LogFunction(szLGS);
				// Show user message
				wchar_t szTitle[128];
				swprintf_c(szTitle, L"ConEmuC[Srv]: PID=%u", GetCurrentProcessId());
				MessageBox(nullptr, szLgsError, szTitle, MB_ICONSTOP|MB_SYSTEMMODAL);
				return CERR_WRONG_GUI_VERSION;
			}
		}
	}

	xf_check();

	if (gState.runMode_ == RunMode::Comspec)
	{
		// New console was requested?
		if (IsNewConsoleArg(lsCmdLine))
		{
			auto* comspec = dynamic_cast<WorkerComspec*>(gpWorker);
			if (!comspec)
			{
				_ASSERTE(comspec != nullptr);
			}
			else
			{
				const auto iNewConRc = comspec->ProcessNewConsoleArg(lsCmdLine);
				if (iNewConRc != 0)
				{
					return iNewConRc;
				}
			}
		}
	}

	LPCWSTR pszArguments4EnvVar = nullptr;

	if (gState.runMode_ == RunMode::Comspec && (!lsCmdLine || !*lsCmdLine))
	{
		if (gpWorker->IsCmdK())
		{
			gbRunViaCmdExe = TRUE;
		}
		else
		{
			// В фаре могут повесить пустую ассоциацию на маску
			// *.ini -> "@" - тогда фар как бы ничего не делает при запуске этого файла, но ComSpec зовет...
			gbNonGuiMode = TRUE;
			DisableAutoConfirmExit();
			return CERR_EMPTY_COMSPEC_CMDLINE;
		}
	}
	else
	{
		BOOL bAlwaysConfirmExit = gState.alwaysConfirmExit_, bAutoDisableConfirmExit = gState.autoDisableConfirmExit_;

		if (gState.runMode_ == RunMode::Server)
		{
			LogFunction(L"ProcessSetEnvCmd {set, title, chcp, etc.}");
			// Console may be started as follows:
			// "set PATH=C:\Program Files;%PATH%" & ... & cmd
			// Supported commands:
			//  set abc=val
			//  "set PATH=C:\Program Files;%PATH%"
			//  chcp [utf8|ansi|oem|<cp_no>]
			//  title "Console init title"
			if (!gpSetEnv)
				gpSetEnv = new CProcessEnvCmd();
			CStartEnvTitle setTitleVar(&gpszForcedTitle);
			ProcessSetEnvCmd(lsCmdLine, gpSetEnv, &setTitleVar);
		}

		gpszCheck4NeedCmd = lsCmdLine; // Для отладки

		gbRunViaCmdExe = IsNeedCmd((gState.runMode_ == RunMode::Server), lsCmdLine, szExeTest,
			&pszArguments4EnvVar, &lbNeedCutStartEndQuot, &gbRootIsCmdExe, &bAlwaysConfirmExit, &bAutoDisableConfirmExit);

		if (gpConsoleArgs->confirmExitParm_ == 0)
		{
			gState.alwaysConfirmExit_ = bAlwaysConfirmExit;
			gState.autoDisableConfirmExit_ = bAutoDisableConfirmExit;
		}
	}


	#ifndef WIN64
	// Команды вида: C:\Windows\SysNative\reg.exe Query "HKCU\Software\Far2"|find "Far"
	// Для них нельзя отключать редиректор (wow.Disable()), иначе SysNative будет недоступен
	CheckNeedSkipWowChange(lsCmdLine);
	#endif

	nCmdLine = lstrlenW(lsCmdLine);

	if (!gbRunViaCmdExe)
	{
		nCmdLine += 1; // только место под 0
		if (pszArguments4EnvVar && *szExeTest)
			nCmdLine += lstrlen(szExeTest)+3;
	}
	else
	{
		gszComSpec[0] = 0;

		if (!GetEnvironmentVariable(L"ComSpec", gszComSpec, MAX_PATH) || gszComSpec[0] == 0)
			gszComSpec[0] = 0;

		// ComSpec/ComSpecC не определен, используем cmd.exe
		if (gszComSpec[0] == 0)
		{
			LPCWSTR pszFind = GetComspecFromEnvVar(gszComSpec, countof(gszComSpec));
			if (!pszFind || !wcschr(pszFind, L'\\') || !FileExists(pszFind))
			{
				_ASSERTE("cmd.exe not found!");
				_printf("Can't find cmd.exe!\n");
				return CERR_CMDEXENOTFOUND;
			}
		}

		nCmdLine += lstrlenW(gszComSpec)+15; // "/C", кавычки и возможный "/U"
	}

	size_t nCchLen = nCmdLine+1; // nCmdLine учитывает длину lsCmdLine + gszComSpec + еще чуть-чуть на "/C" и прочее
	gpszRunCmd = (wchar_t*)calloc(nCchLen,sizeof(wchar_t));

	if (!gpszRunCmd)
	{
		_printf("Can't allocate %i wchars!\n", (DWORD)nCmdLine);
		return CERR_NOTENOUGHMEM1;
	}

	// это нужно для смены заголовка консоли. при необходимости COMSPEC впишем ниже, после смены
	if (pszArguments4EnvVar && *szExeTest && !gbRunViaCmdExe)
	{
		gpszRunCmd[0] = L'"';
		_wcscat_c(gpszRunCmd, nCchLen, szExeTest);
		if (*pszArguments4EnvVar)
		{
			_wcscat_c(gpszRunCmd, nCchLen, L"\" ");
			_wcscat_c(gpszRunCmd, nCchLen, pszArguments4EnvVar);
		}
		else
		{
			_wcscat_c(gpszRunCmd, nCchLen, L"\"");
		}
	}
	else
	{
		_wcscpy_c(gpszRunCmd, nCchLen, lsCmdLine);
	}
	// !!! gpszRunCmd может поменяться ниже!

	// ====
	if (gbRunViaCmdExe)
	{
		// -- always quote
		gpszRunCmd[0] = L'"';
		_wcscpy_c(gpszRunCmd+1, nCchLen-1, gszComSpec);

		_wcscat_c(gpszRunCmd, nCchLen, gpWorker->IsCmdK() ? L"\" /K " : L"\" /C ");

		// Собственно, командная строка
		_wcscat_c(gpszRunCmd, nCchLen, lsCmdLine);
	}
	else if (lbNeedCutStartEndQuot)
	{
		// ""c:\arc\7z.exe -?"" - не запустится!
		_wcscpy_c(gpszRunCmd, nCchLen, lsCmdLine+1);
		wchar_t *pszEndQ = gpszRunCmd + lstrlenW(gpszRunCmd) - 1;
		_ASSERTE(pszEndQ && *pszEndQ == L'"');

		if (pszEndQ && *pszEndQ == L'"') *pszEndQ = 0;
	}

	// Теперь выкусить и обработать "-new_console" / "-cur_console"
	RConStartArgs args;
	args.pszSpecialCmd = gpszRunCmd;
	args.ProcessNewConArg();
	args.pszSpecialCmd = nullptr; // Чтобы не разрушилась память отведенная в gpszRunCmd
	// Если указана рабочая папка
	if (args.pszStartupDir && *args.pszStartupDir)
	{
		SetCurrentDirectory(args.pszStartupDir);
	}
	//
	gbRunInBackgroundTab = (args.BackgroundTab == crb_On);
	if (args.BufHeight == crb_On)
	{
		TODO("gcrBufferSize - и ширину буфера");
		gnBufferHeight = args.nBufHeight;
		gbParmBufSize = TRUE;
	}
	// DosBox?
	if (args.ForceDosBox == crb_On)
	{
		gState.dosbox_.use_ = TRUE;
	}
	// Overwrite mode in Prompt?
	if (args.OverwriteMode == crb_On)
	{
		gnConsoleModeFlags |= (ENABLE_INSERT_MODE << 16); // Mask
		gnConsoleModeFlags &= ~ENABLE_INSERT_MODE; // Turn bit OFF

		// Поскольку ключик указан через "-cur_console/-new_console"
		// смену режима нужно сделать сразу, т.к. функа зовется только для сервера
		// #Server this is called twice during startup
		WorkerServer::Instance().ServerInitConsoleMode();
	}

	#ifdef _DEBUG
	OutputDebugString(gpszRunCmd); OutputDebugString(L"\n");
	#endif
	UNREFERENCED_PARAMETER(pwszStartCmdLine);
	_ASSERTE(pwszStartCmdLine==asCmdLine);
	return 0;
}
#endif

#if 0
RunMode DetectRunModeFromCmdLine(LPCWSTR asCmdLine)
{
	CmdArg szArg;
	CEStr szExeTest;
	LPCWSTR pwszStartCmdLine = asCmdLine;
	LPCWSTR lsCmdLine = asCmdLine;
	LPCWSTR pszArgStarts = nullptr;

	if (!lsCmdLine || !*lsCmdLine)
	{
		return RunMode::Undefined;
	}

	RunMode runMode = RunMode::Undefined;

	while ((lsCmdLine = NextArg(lsCmdLine, szArg, &pszArgStarts)))
	{
		if ((szArg[0] == L'/' || szArg[0] == L'-')
		        && (szArg[1] == L'?' || ((szArg[1] & ~0x20) == L'H'))
		        && szArg[2] == 0)
		{
			break; // help requested
		}

		// Following code wants '/'style arguments
		// Change '-'style to '/'style
		if (szArg[0] == L'-')
			szArg.SetAt(0, L'/');
		else if (szArg[0] != L'/')
			continue;

		if ((wcscmp(szArg, L"/ADMIN")==0)
			|| (wcscmp(szArg, L"/ATTACH")==0)
			|| (wcsncmp(szArg, L"/GUIATTACH=", 11)==0)
			|| (wcscmp(szArg, L"/NOCMD")==0)
			|| (wcscmp(szArg, L"/ROOTEXE")==0)
			|| (wcsncmp(szArg, L"/PID=", 5)==0 || wcsncmp(szArg, L"/TRMPID=", 8)==0
			|| wcsncmp(szArg, L"/FARPID=", 8)==0 || wcsncmp(szArg, L"/CONPID=", 8)==0)
			|| (wcsncmp(szArg, L"/GID=", 5)==0)
			)
		{
			runMode = RunMode::Server;
		}
		else if ((lstrcmpi(szArg, L"/AUTOATTACH")==0) || (lstrcmpi(szArg, L"/ATTACHDEFTERM")==0))
		{
			if (lstrcmpi(szArg, L"/AUTOATTACH")==0)
			{
				runMode = RunMode::AutoAttach;
				gState.attachMode_ |= am_Async;
			}
			else //if (lstrcmpi(szArg, L"/ATTACHDEFTERM")==0)
			{
				runMode = RunMode::Server;
			}
		}
		else if (wcsncmp(szArg, L"/GHWND=", 7)==0)
		{
			if (runMode == RunMode::Undefined)
			{
				runMode = RunMode::Server;
			}
		}
		// После этих аргументов - идет то, что передается в CreateProcess!
		else if (lstrcmpi(szArg, L"/ROOT")==0)
		{
			runMode = RunMode::Server;
			break; // lsCmdLine points to starting app
		}
		else if ((lstrcmpi(szArg, L"/ConInfo")==0)
			|| (lstrcmpi(szArg, L"/CheckUnicode")==0))
		{
			break;
		}
		else if ((wcscmp(szArg, L"/CONFIRM") == 0)
			|| (wcscmp(szArg, L"/CONFHALT") == 0)
			|| (wcscmp(szArg, L"/CREATECON")==0)
			|| (wcsncmp(szArg, L"/CONPID=", 8)==0)
			|| (wcsncmp(szArg, L"/CINMODE=", 9)==0)
			|| (lstrcmpi(szArg, L"/CONFIG")==0)
			)
		{
			continue;
		}
		// COMSPEC (CreateProcess)!
		else if (szArg[0] == L'/' && (((szArg[1] & ~0x20) == L'C') || ((szArg[1] & ~0x20) == L'K')))
		{
			if (szArg[2] == 0)  // "/c" или "/k"
			{
				runMode = RunMode::Comspec;
			}

			if (runMode == RunMode::Undefined && szArg[4] == 0
			        && ((szArg[2] & ~0x20) == L'M') && ((szArg[3] & ~0x20) == L'D'))
			{
				_ASSERTE(FALSE && "'/cmd' obsolete switch. use /c, /k, /root");
				runMode = RunMode::Server;
			}

			if (runMode == RunMode::Undefined)
			{
				runMode = RunMode::Comspec;
			}
			break; // lsCmdLine уже указывает на запускаемую программу
		}
	}

	return runMode;
}
#endif

// Проверить, что nPID это "ConEmuC.exe" или "ConEmuC64.exe"
bool IsMainServerPID(DWORD nPID)
{
	PROCESSENTRY32 Info;
	if (!GetProcessInfo(nPID, &Info))
		return false;
	if ((lstrcmpi(Info.szExeFile, L"ConEmuC.exe") == 0)
		|| (lstrcmpi(Info.szExeFile, L"ConEmuC64.exe") == 0))
	{
		return true;
	}
	return false;
}

int ExitWaitForKey(DWORD vkKeys, LPCWSTR asConfirm, BOOL abNewLine, BOOL abDontShowConsole, DWORD anMaxTimeout /*= 0*/)
{
	gState.inExitWaitForKey_ = TRUE;

	int nKeyPressed = -1;

	//-- Don't exit on ANY key if -new_console:c1
	//if (!vkKeys) vkKeys = VK_ESCAPE;

	// Чтобы ошибку было нормально видно
	if (!abDontShowConsole)
	{
		BOOL lbNeedVisible = FALSE;

		if (!gState.realConWnd_) gState.realConWnd_ = GetConEmuHWND(2);

		if (gState.realConWnd_)  // Если консоль была скрыта
		{
			WARNING("Если GUI жив - отвечает на запросы SendMessageTimeout - показывать консоль не нужно. Не красиво получается");

			if (!IsWindowVisible(gState.realConWnd_))
			{
				BOOL lbGuiAlive = FALSE;

				if (gState.conemuWndDC_ && !isConEmuTerminated())
				{
					// ConEmu will deal the situation?
					// EmergencyShow annoys user if parent window was killed (InsideMode)
					lbGuiAlive = TRUE;
				}
				else if (gState.conemuWnd_ && IsWindow(gState.conemuWnd_))
				{
					DWORD_PTR dwLRc = 0;

					if (SendMessageTimeout(gState.conemuWnd_, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &dwLRc))
						lbGuiAlive = TRUE;
				}

				if (!lbGuiAlive && !IsWindowVisible(gState.realConWnd_))
				{
					lbNeedVisible = TRUE;
					// не надо наверное... // поставить "стандартный" 80x25, или то, что было передано к ком.строке
					//SMALL_RECT rcNil = {0}; SetConsoleSize(0, gcrVisibleSize, rcNil, ":Exiting");
					//SetConsoleFontSizeTo(gState.realConWnd, 8, 12); // установим шрифт побольше
					//apiShowWindow(gState.realConWnd, SW_SHOWNORMAL); // и покажем окошко
					EmergencyShow(gState.realConWnd_);
				}
			}
		}
	}

	HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

	// Сначала почистить буфер
	INPUT_RECORD r = {0}; DWORD dwCount = 0;
	DWORD nPreFlush = 0, nPostFlush = 0;
	#ifdef _DEBUG
	DWORD nPreQuit = 0;
	#endif

	GetNumberOfConsoleInputEvents(hIn, &nPreFlush);

	FlushConsoleInputBuffer(hIn);

	PRINT_COMSPEC(L"Finalizing. gbInShutdown=%i\n", gbInShutdown);
	GetNumberOfConsoleInputEvents(hIn, &nPostFlush);

	DWORD nStartTick = 0, nDelta = 0;

	if (gbInShutdown)
		goto wrap; // Event закрытия мог припоздниться

	SetConsoleMode(hOut, ENABLE_PROCESSED_OUTPUT|ENABLE_WRAP_AT_EOL_OUTPUT);

	//
	if (asConfirm && *asConfirm)
	{
		_wprintf(asConfirm);
	}

	nStartTick = GetTickCount();

	while (TRUE)
	{
		if (gbStopExitWaitForKey)
		{
			// Был вызван HandlerRoutine(CLOSE)
			break;
		}

		// Allow exit by timeout
		if (anMaxTimeout)
		{
			nDelta = GetTickCount() - nStartTick;
			if (nDelta >= anMaxTimeout)
			{
				break;
			}
		}

		// If server was connected to GUI, but we get here because
		// root process was not attached to console yet (antivirus lags?)
		if (gpSrv->ConnectInfo.bConnected)
		{
			TODO("It would be nice to check for new console processed started in console?");
		}

		if (!PeekConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &r, 1, &dwCount))
			dwCount = 0;

		if (!gState.inExitWaitForKey_)
		{
			if (gState.runMode_ == RunMode::Server)
			{
				int nCount = gpWorker->Processes().nProcessCount;

				if (nCount > 1)
				{
					// Теперь Peek, так что просто выходим
					//// ! Процесс таки запустился, закрываться не будем. Вернуть событие в буфер!
					//WriteConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &r, 1, &dwCount);
					break;
				}
			}

			if (gbInShutdown)
			{
				break;
			}
		}

		if (dwCount)
		{
			#ifdef _DEBUG
			GetNumberOfConsoleInputEvents(hIn, &nPreQuit);
			#endif

			// Avoid ConIn overflow, even if (vkKeys == 0)
			if (ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &r, 1, &dwCount)
				&& dwCount
				&& vkKeys)
			{
				bool lbMatch = false;

				if (r.EventType == KEY_EVENT && r.Event.KeyEvent.bKeyDown)
				{
					for (DWORD vk = vkKeys; !lbMatch && LOBYTE(vk); vk=vk>>8)
					{
						lbMatch = (r.Event.KeyEvent.wVirtualKeyCode == LOBYTE(vk));
					}
				}

				if (lbMatch)
				{
					nKeyPressed = r.Event.KeyEvent.wVirtualKeyCode;
					break;
				}
			}
		}

		Sleep(50);
	}

	//MessageBox(0,L"Debug message...............1",L"ConEmuC",0);
	//int nCh = _getch();
	if (abNewLine)
		_printf("\n");

wrap:
	#if defined(_DEBUG) && defined(SHOW_EXITWAITKEY_MSGBOX)
	wchar_t szTitle[128];
	swprintf_c(szTitle, L"ConEmuC[Srv]: PID=%u", GetCurrentProcessId());
	if (!gbStopExitWaitForKey)
		MessageBox(nullptr, asConfirm ? asConfirm : L"???", szTitle, MB_ICONEXCLAMATION|MB_SYSTEMMODAL);
	#endif
	return nKeyPressed;
}


void SetTerminateEvent(SetTerminateEventPlace eFrom)
{
	#ifdef DEBUG_ISSUE_623
	_ASSERTE((eFrom == ste_ProcessCountChanged) && "Calling SetTerminateEvent");
	#endif

	if (!gTerminateEventPlace)
		gTerminateEventPlace = eFrom;
	SetEvent(ghExitQueryEvent);
}


bool isConEmuTerminated()
{
	// HWND of our VirtualConsole
	if (!gState.conemuWndDC_)
	{
		_ASSERTE(FALSE && "gState.conemuWndDC_ is expected to be NOT nullptr");
		return false;
	}

	if (::IsWindow(gState.conemuWndDC_))
	{
		// ConEmu is alive
		return false;
	}

	//TODO: It would be better to check process presence via connected Pipe
	//TODO: Same as in ConEmu, it must check server presence via pipe
	// For now, don't annoy user with RealConsole if all processes were finished
	if (gState.inExitWaitForKey_ // We are waiting for Enter or Esc
		&& (gpWorker->Processes().nProcessCount <= 1) // No active processes are found in console (except our SrvPID)
		)
	{
		// Let RealConsole remain invisible, ConEmu will deal the situation
		return false;
	}

	// ConEmu was killed?
	return true;
}

static BOOL CALLBACK FindConEmuByPidProc(HWND hwnd, LPARAM lParam)
{
	DWORD dwPID;
	GetWindowThreadProcessId(hwnd, &dwPID);
	if (dwPID == gState.conemuPid_)
	{
		wchar_t szClass[128];
		if (GetClassName(hwnd, szClass, countof(szClass)))
		{
			if (lstrcmp(szClass, VirtualConsoleClassMain) == 0)
			{
				*(HWND*)lParam = hwnd;
				return FALSE;
			}
		}
	}
	return TRUE;
}

HWND FindConEmuByPID(DWORD anSuggestedGuiPID /*= 0*/)
{
	LogFunction(L"FindConEmuByPID");

	HWND hConEmuWnd = nullptr;
	DWORD nConEmuPID = anSuggestedGuiPID ? anSuggestedGuiPID : gState.conemuPid_;
	DWORD dwGuiThreadId = 0, dwGuiProcessId = 0;

	// В большинстве случаев PID GUI передан через параметры
	if (nConEmuPID == 0)
	{
		// GUI может еще "висеть" в ожидании или в отладчике, так что пробуем и через Snapshot
		//TODO: Reuse MToolHelp.h
		HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);

		if (hSnap != INVALID_HANDLE_VALUE)
		{
			PROCESSENTRY32 prc = {sizeof(PROCESSENTRY32)};

			if (Process32First(hSnap, &prc))
			{
				do
				{
					if (prc.th32ProcessID == gnSelfPID)
					{
						nConEmuPID = prc.th32ParentProcessID;
						break;
					}
				}
				while (Process32Next(hSnap, &prc));
			}

			CloseHandle(hSnap);
		}
	}

	if (nConEmuPID)
	{
		HWND hGui = nullptr;

		while ((hGui = FindWindowEx(nullptr, hGui, VirtualConsoleClassMain, nullptr)) != nullptr)
		{
			dwGuiThreadId = GetWindowThreadProcessId(hGui, &dwGuiProcessId);

			if (dwGuiProcessId == nConEmuPID)
			{
				hConEmuWnd = hGui;
				break;
			}
		}

		// Если "в лоб" по имени класса ничего не нашли - смотрим
		// среди всех дочерних для текущего десктопа
		if ((hConEmuWnd == nullptr) && !anSuggestedGuiPID)
		{
			HWND hDesktop = GetDesktopWindow();
			EnumChildWindows(hDesktop, FindConEmuByPidProc, (LPARAM)&hConEmuWnd);
		}
	}

	// Ensure that returned hConEmuWnd match gState.conemuPid_
	if (!anSuggestedGuiPID && hConEmuWnd)
	{
		GetWindowThreadProcessId(hConEmuWnd, &gState.conemuPid_);
	}

	return hConEmuWnd;
}

CESERVER_CONSOLE_APP_MAPPING* GetAppMapPtr()
{
	if (!gpSrv || !gpSrv->pAppMap)
		return nullptr;
	return gpSrv->pAppMap->Ptr();
}


void SendStarted()
{
	LogFunction(L"SendStarted");

	WARNING("Подозрение, что слишком много вызовов при старте сервера. Неаккуратно");

	static bool bSent = false;

	if (bSent)
	{
		_ASSERTE(FALSE && "SendStarted repetition");
		return; // отсылать только один раз
	}

	//crNewSize = gpSrv->sbi.dwSize;
	//_ASSERTE(crNewSize.X>=MIN_CON_WIDTH && crNewSize.Y>=MIN_CON_HEIGHT);
	HWND hConWnd = GetConEmuHWND(2);

	if (!gnSelfPID)
	{
		_ASSERTE(gnSelfPID!=0);
		gnSelfPID = GetCurrentProcessId();
	}

	if (!hConWnd)
	{
		// Это Detached консоль. Скорее всего запущен вместо COMSPEC
		_ASSERTE(gState.runMode_ == RunMode::Comspec);
		gbNonGuiMode = TRUE; // Не посылать ExecuteGuiCmd при выходе. Это не наша консоль
		return;
	}

	_ASSERTE(hConWnd == gState.realConWnd_);
	gState.realConWnd_ = hConWnd;

	DWORD nMainServerPID = 0, nAltServerPID = 0, nGuiPID = 0;

	// Для ComSpec-а сразу можно проверить, а есть-ли сервер в этой консоли...
	if ((gState.runMode_ != RunMode::AutoAttach) && (gState.runMode_ /*== RunMode::RM_COMSPEC*/ > RunMode::Server))
	{
		MFileMapping<CESERVER_CONSOLE_MAPPING_HDR> ConsoleMap;
		ConsoleMap.InitName(CECONMAPNAME, LODWORD(hConWnd)); //-V205
		const CESERVER_CONSOLE_MAPPING_HDR* pConsoleInfo = ConsoleMap.Open();

		if (!pConsoleInfo)
		{
			_ASSERTE((gState.runMode_ == RunMode::Comspec) && (gState.realConWnd_ && !gState.conemuWnd_ && IsWindowVisible(gState.realConWnd_)) && "ConsoleMap was not initialized for AltServer/ComSpec");
		}
		else
		{
			nMainServerPID = pConsoleInfo->nServerPID;
			nAltServerPID = pConsoleInfo->nAltServerPID;
			nGuiPID = pConsoleInfo->nGuiPID;

			if (pConsoleInfo->cbSize >= sizeof(CESERVER_CONSOLE_MAPPING_HDR))
			{
				if (pConsoleInfo->nLogLevel)
					CreateLogSizeFile(pConsoleInfo->nLogLevel, pConsoleInfo);
			}

			//UnmapViewOfFile(pConsoleInfo);
			ConsoleMap.CloseMap();
		}

		if (nMainServerPID == 0)
		{
			gbNonGuiMode = TRUE; // Не посылать ExecuteGuiCmd при выходе. Это не наша консоль
			return; // Режим ComSpec, но сервера нет, соответственно, в GUI ничего посылать не нужно
		}
	}
	else
	{
		nGuiPID = gState.conemuPid_;
	}

	CESERVER_REQ *pIn = nullptr, *pOut = nullptr, *pSrvOut = nullptr;
	int nSize = sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_STARTSTOP);
	pIn = ExecuteNewCmd(CECMD_CMDSTARTSTOP, nSize);

	if (pIn)
	{
		if (!GetModuleFileName(nullptr, pIn->StartStop.sModuleName, countof(pIn->StartStop.sModuleName)))
			pIn->StartStop.sModuleName[0] = 0;
		#ifdef _DEBUG
		LPCWSTR pszFileName = PointToName(pIn->StartStop.sModuleName);
		#endif

		// Cmd/Srv режим начат
		switch (gState.runMode_)
		{
		case RunMode::Server:
			pIn->StartStop.nStarted = sst_ServerStart;
			gpWorker->IsKeyboardLayoutChanged(pIn->StartStop.dwKeybLayout);
			break;
		case RunMode::AltServer:
			pIn->StartStop.nStarted = sst_AltServerStart;
			gpWorker->IsKeyboardLayoutChanged(pIn->StartStop.dwKeybLayout);
			break;
		case RunMode::Comspec:
			pIn->StartStop.nParentFarPID = gpWorker->ParentFarPid();
			pIn->StartStop.nStarted = sst_ComspecStart;
			break;
		default:
			pIn->StartStop.nStarted = sst_AppStart;
		}
		pIn->StartStop.hWnd = gState.realConWnd_;
		pIn->StartStop.dwPID = gnSelfPID;
		pIn->StartStop.dwAID = gState.dwGuiAID;
		#ifdef _WIN64
		pIn->StartStop.nImageBits = 64;
		#else
		pIn->StartStop.nImageBits = 32;
		#endif
		TODO("Ntvdm/DosBox -> 16");

		//pIn->StartStop.dwInputTID = (gpStatus->runMode_ == RunMode::RM_SERVER) ? gpSrv->dwInputThreadId : 0;
		if ((gState.runMode_ == RunMode::Server) || (gState.runMode_ == RunMode::AltServer))
			pIn->StartStop.bUserIsAdmin = IsUserAdmin();

		// Перед запуском 16бит приложений нужно подресайзить консоль...
		gnImageSubsystem = 0;
		LPCWSTR pszTemp = gpszRunCmd;
		CmdArg lsRoot;

		if (gState.runMode_ == RunMode::Server && gpWorker->IsDebuggerActive())
		{
			// "Debugger"
			gnImageSubsystem = IMAGE_SUBSYSTEM_INTERNAL_DEBUGGER;
			gState.rootIsCmdExe_ = true; // for bufferheight
		}
		else if (gpConsoleArgs->attachFromFar_)
		{
			// Attach from Far Manager plugin Аттач из фар-плагина
			gnImageSubsystem = IMAGE_SUBSYSTEM_FAR_PLUGIN;
		}
		else if (gpszRunCmd && ((pszTemp = NextArg(pszTemp, lsRoot))))
		{
			PRINT_COMSPEC(L"Starting: <%s>", lsRoot);

			MWow64Disable wow;
			if (!gState.bSkipWowChange_)
				wow.Disable();

			DWORD nImageFileAttr = 0;
			if (!GetImageSubsystem(lsRoot, gnImageSubsystem, gnImageBits, nImageFileAttr))
				gnImageSubsystem = 0;

			PRINT_COMSPEC(L", Subsystem: <%i>\n", gnImageSubsystem);
			PRINT_COMSPEC(L"  Args: %s\n", pszTemp);
		}
		else
		{
			GetImageSubsystem(gnImageSubsystem, gnImageBits);
		}

		pIn->StartStop.nSubSystem = gnImageSubsystem;
		if ((gnImageSubsystem == IMAGE_SUBSYSTEM_DOS_EXECUTABLE) || (gState.dosbox_.use_))
			pIn->StartStop.nImageBits = 16;
		else if (gnImageBits)
			pIn->StartStop.nImageBits = gnImageBits;

		pIn->StartStop.bRootIsCmdExe = gState.rootIsCmdExe_;

		// Don't MyGet... to avoid dead locks
		DWORD dwErr1 = 0;
		// ReSharper disable once CppJoinDeclarationAndAssignment
		BOOL lbRc1;

		{
			ScreenBufferInfo sbi{};
			// ReSharper disable once CppJoinDeclarationAndAssignment
			lbRc1 = gpWorker->LoadScreenBufferInfo(sbi);
			if (lbRc1)
			{
				pIn->StartStop.sbi = sbi.csbi;
				pIn->StartStop.crMaxSize = sbi.crMaxSize;
			}
			else
			{
				dwErr1 = GetLastError();
			}
		}

		// Если (для ComSpec) указан параметр "-cur_console:h<N>"
		if (gbParmBufSize)
		{
			pIn->StartStop.bForceBufferHeight = TRUE;
			TODO("gcrBufferSize - и ширину буфера");
			pIn->StartStop.nForceBufferHeight = gnBufferHeight;
		}

		PRINT_COMSPEC(L"Starting %s mode (ExecuteGuiCmd started)\n", (RunMode==RunMode::Server) ? L"Server" : (RunMode==RunMode::AltServer) ? L"AltServer" : L"ComSpec");

		// CECMD_CMDSTARTSTOP
		if (gState.runMode_ == RunMode::Server)
		{
			_ASSERTE(nGuiPID!=0 && gState.runMode_==RunMode::Server);
			pIn->StartStop.hServerProcessHandle = DuplicateProcessHandle(nGuiPID);

			// послать CECMD_CMDSTARTSTOP/sst_ServerStart в GUI
			pOut = ExecuteGuiCmd(gState.conemuWnd_, pIn, gState.realConWnd_);
		}
		else if (gState.runMode_ == RunMode::AltServer)
		{
			// Подготовить хэндл своего процесса для MainServer
			pIn->StartStop.hServerProcessHandle = DuplicateProcessHandle(nMainServerPID);

			_ASSERTE(pIn->hdr.nCmd == CECMD_CMDSTARTSTOP);
			pSrvOut = ExecuteSrvCmd(nMainServerPID, pIn, gState.realConWnd_);

			// MainServer должен был вернуть PID предыдущего AltServer (если он был)
			if (pSrvOut && (pSrvOut->DataSize() >= sizeof(CESERVER_REQ_STARTSTOPRET)))
			{
				WorkerServer::Instance().SetPrevAltServerPid(pSrvOut->StartStopRet.dwPrevAltServerPID);
			}
			else
			{
				_ASSERTE(pSrvOut && (pSrvOut->DataSize() >= sizeof(CESERVER_REQ_STARTSTOPRET)) && "StartStopRet.dwPrevAltServerPID expected");
			}
		}
		else
		{
			WARNING("TODO: Может быть это тоже в главный сервер посылать?");
			_ASSERTE(nGuiPID!=0 && gState.runMode_==RunMode::Comspec);

			pOut = ExecuteGuiCmd(gState.realConWnd_, pIn, gState.realConWnd_);
			// pOut должен содержать инфу, что должен сделать ComSpec
			// при завершении (вернуть высоту буфера)
			_ASSERTE(pOut!=nullptr && "Prev buffer size must be returned!");
		}

		#if 0
		if (nServerPID && (nServerPID != gnSelfPID))
		{
			_ASSERTE(nServerPID!=0 && (gpStatus->runMode_==RunMode::RM_ALTSERVER || gpStatus->runMode_==RunMode::RM_COMSPEC));
			if ((gpStatus->runMode_ == RunMode::RM_ALTSERVER) || (gpStatus->runMode_ == RunMode::RM_SERVER))
			{
				pIn->StartStop.hServerProcessHandle = DuplicateProcessHandle(nServerPID);
			}

			WARNING("Optimize!!!");
			WARNING("Async");
			pSrvOut = ExecuteSrvCmd(nServerPID, pIn, gState.realConWnd);
			if (gpStatus->runMode_ == RunMode::RM_ALTSERVER)
			{
				if (pSrvOut && (pSrvOut->DataSize() >= sizeof(CESERVER_REQ_STARTSTOPRET)))
				{
					gpSrv->dwPrevAltServerPID = pSrvOut->StartStopRet.dwPrevAltServerPID;
				}
				else
				{
					_ASSERTE(pSrvOut && (pSrvOut->DataSize() >= sizeof(CESERVER_REQ_STARTSTOPRET)));
				}
			}
		}
		else
		{
			_ASSERTE(gpStatus->runMode_==RunMode::RM_SERVER && (nServerPID && (nServerPID != gnSelfPID)) && "nServerPID MUST be known already!");
		}
		#endif

		#if 0
		WARNING("Только для RunMode::RM_SERVER. Все остальные должны докладываться главному серверу, а уж он разберется");
		if (gpStatus->runMode_ != RunMode::RM_APPLICATION)
		{
			_ASSERTE(nGuiPID!=0 || gpStatus->runMode_==RunMode::RM_SERVER);
			if ((gpStatus->runMode_ == RunMode::RM_ALTSERVER) || (gpStatus->runMode_ == RunMode::RM_SERVER))
			{
				pIn->StartStop.hServerProcessHandle = DuplicateProcessHandle(nGuiPID);
			}

			WARNING("Optimize!!!");
			pOut = ExecuteGuiCmd(gState.realConWnd, pIn, gState.realConWnd);
		}
		#endif




		PRINT_COMSPEC(L"Starting %s mode (ExecuteGuiCmd finished)\n",
			(gState.runMode_ == RunMode::Server) ? L"Server" : (gState.runMode_ == RunMode::AltServer) ? L"AltServer" : L"ComSpec");

		if (pOut == nullptr)
		{
			if (gState.runMode_ != RunMode::Comspec)
			{
				// для RunMode::RM_APPLICATION будет pOut==nullptr?
				_ASSERTE(gState.runMode_ == RunMode::AltServer);
			}
			else
			{
				gbNonGuiMode = TRUE; // Не посылать ExecuteGuiCmd при выходе. Это не наша консоль
			}
		}
		else
		{
			bSent = true;
			const BOOL bAlreadyBufferHeight = pOut->StartStopRet.bWasBufferHeight;
			nGuiPID = pOut->StartStopRet.dwPID;

			WorkerServer::SetConEmuWindows(pOut->StartStopRet.hWnd, pOut->StartStopRet.hWndDc, pOut->StartStopRet.hWndBack);

			if (gpSrv)
			{
				_ASSERTE(gState.conemuPid_ == pOut->StartStopRet.dwPID);
				gState.conemuPid_ = pOut->StartStopRet.dwPID;
				#ifdef _DEBUG
				DWORD dwPID; GetWindowThreadProcessId(gState.conemuWnd_, &dwPID);
				_ASSERTE(gState.conemuWnd_==nullptr || dwPID==gState.conemuPid_);
				#endif
			}

			if ((gState.runMode_ == RunMode::Server) || (gState.runMode_ == RunMode::AltServer))
			{
				gState.bWasDetached_ = FALSE;

				if (gpWorker)
				{
					gpWorker->UpdateConsoleMapHeader(L"SendStarted");
				}
				else
				{
					_ASSERTE(gpWorker != nullptr);
				}
			}

			_ASSERTE(gnMainServerPID==0 || gnMainServerPID==pOut->StartStopRet.dwMainSrvPID || (gState.attachMode_ && gState.alienMode_ && (pOut->StartStopRet.dwMainSrvPID==gnSelfPID)));
			gnMainServerPID = pOut->StartStopRet.dwMainSrvPID;
			gnAltServerPID = pOut->StartStopRet.dwAltSrvPID;

			AllowSetForegroundWindow(nGuiPID);
			TODO("gnBufferHeight->gcrBufferSize");
			TODO("gcrBufferSize - и ширину буфера");
			gnBufferHeight  = (SHORT)pOut->StartStopRet.nBufferHeight;
			gbParmBufSize = TRUE;
			// gcrBufferSize переименован в gcrVisibleSize
			_ASSERTE(pOut->StartStopRet.nWidth && pOut->StartStopRet.nHeight);
			gcrVisibleSize.X = (SHORT)pOut->StartStopRet.nWidth;
			gcrVisibleSize.Y = (SHORT)pOut->StartStopRet.nHeight;
			gbParmVisibleSize = TRUE;

			if ((gState.runMode_ == RunMode::Server) || (gState.runMode_ == RunMode::AltServer))
			{
				// Если режим отладчика - принудительно включить прокрутку
				if (gpWorker->IsDebuggerActive() && !gnBufferHeight)
				{
					_ASSERTE(gState.runMode_ != RunMode::AltServer);
					gnBufferHeight = LONGOUTPUTHEIGHT_MAX;
				}

				SMALL_RECT rcNil = {0};
				gpWorker->SetConsoleSize(gnBufferHeight, gcrVisibleSize, rcNil, "::SendStarted");

				// Смена раскладки клавиатуры
				if ((gState.runMode_ != RunMode::AltServer) && pOut->StartStopRet.bNeedLangChange)
				{
					#ifndef INPUTLANGCHANGE_SYSCHARSET
					#define INPUTLANGCHANGE_SYSCHARSET 0x0001
					#endif

					WPARAM wParam = INPUTLANGCHANGE_SYSCHARSET;
					TODO("Проверить на x64, не будет ли проблем с 0xFFFFFFFFFFFFFFFFFFFFF");
					LPARAM lParam = (LPARAM)(DWORD_PTR)pOut->StartStopRet.NewConsoleLang;
					SendMessage(gState.realConWnd_, WM_INPUTLANGCHANGEREQUEST, wParam, lParam);
				}
			}
			else
			{
				// Может так получиться, что один COMSPEC запущен из другого.
				// 100628 - неактуально. COMSPEC сбрасывается в cmd.exe
				//if (bAlreadyBufferHeight)
				//	gpSrv->bNonGuiMode = TRUE; // Не посылать ExecuteGuiCmd при выходе - прокрутка должна остаться
				gbWasBufferHeight = bAlreadyBufferHeight;
			}

			//nNewBufferHeight = ((DWORD*)(pOut->Data))[0];
			//crNewSize.X = (SHORT)((DWORD*)(pOut->Data))[1];
			//crNewSize.Y = (SHORT)((DWORD*)(pOut->Data))[2];
			TODO("Если он запущен как COMSPEC - то к GUI никакого отношения иметь не должен");
			//if (rNewWindow.Right >= crNewSize.X) // размер был уменьшен за счет полосы прокрутки
			//    rNewWindow.Right = crNewSize.X-1;
			ExecuteFreeResult(pOut); //pOut = nullptr;
			//gnBufferHeight = nNewBufferHeight;

		} // (pOut != nullptr)

		ExecuteFreeResult(pIn);
		ExecuteFreeResult(pSrvOut);
	}
}

CESERVER_REQ* SendStopped(CONSOLE_SCREEN_BUFFER_INFO* psbi)
{
	LogFunction(L"SendStopped");

	int iHookRc = -1;
	if (gState.runMode_ == RunMode::AltServer)
	{
		// сообщение о завершении будет посылать ConEmuHk.dll
		HMODULE hHooks = GetModuleHandle(ConEmuHk_DLL_3264);
		RequestLocalServer_t fRequestLocalServer = (RequestLocalServer_t)(hHooks ? GetProcAddress(hHooks, "RequestLocalServer") : nullptr);
		if (fRequestLocalServer)
		{
			RequestLocalServerParm Parm = {sizeof(Parm), slsf_AltServerStopped};
			iHookRc = fRequestLocalServer(&Parm);
		}
		_ASSERTE((iHookRc == 0) && "SendStopped must be sent from ConEmuHk.dll");
		return nullptr;
	}

	CESERVER_REQ *pIn = nullptr, *pOut = nullptr;
	int nSize = sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_STARTSTOP);
	pIn = ExecuteNewCmd(CECMD_CMDSTARTSTOP,nSize);

	if (pIn)
	{
		switch (gState.runMode_)
		{
		case RunMode::Server:
			// По идее, sst_ServerStop не посылается
			_ASSERTE(gState.runMode_ != RunMode::Server);
			pIn->StartStop.nStarted = sst_ServerStop;
			break;
		case RunMode::AltServer:
			pIn->StartStop.nStarted = sst_AltServerStop;
			break;
		case RunMode::Comspec:
			pIn->StartStop.nStarted = sst_ComspecStop;
			pIn->StartStop.nOtherPID = gpWorker->RootProcessId();
			pIn->StartStop.nParentFarPID = gpWorker->ParentFarPid();
			break;
		default:
			pIn->StartStop.nStarted = sst_AppStop;
		}

		if (!GetModuleFileName(nullptr, pIn->StartStop.sModuleName, countof(pIn->StartStop.sModuleName)))
			pIn->StartStop.sModuleName[0] = 0;

		pIn->StartStop.hWnd = gState.realConWnd_;
		pIn->StartStop.dwPID = gnSelfPID;
		pIn->StartStop.nSubSystem = gnImageSubsystem;
		if ((gnImageSubsystem == IMAGE_SUBSYSTEM_DOS_EXECUTABLE) || (gState.dosbox_.use_))
			pIn->StartStop.nImageBits = 16;
		else if (gnImageBits)
			pIn->StartStop.nImageBits = gnImageBits;
		else
		{
			#ifdef _WIN64
			pIn->StartStop.nImageBits = 64;
			#else
			pIn->StartStop.nImageBits = 32;
			#endif
		}
		pIn->StartStop.bWasBufferHeight = gbWasBufferHeight;

		if (psbi != nullptr)
		{
			pIn->StartStop.sbi = *psbi;
		}
		else
		{
			// НЕ MyGet..., а то можем заблокироваться...
			// ghConOut может быть nullptr, если ошибка произошла во время разбора аргументов
			GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &pIn->StartStop.sbi);
		}

		pIn->StartStop.crMaxSize = MyGetLargestConsoleWindowSize(GetStdHandle(STD_OUTPUT_HANDLE));

		if (gState.runMode_ == RunMode::AltServer)
		{
			if (gnMainServerPID == 0)
			{
				_ASSERTE(gnMainServerPID!=0 && "MainServer PID must be determined");
			}
			else
			{
				pIn->StartStop.nOtherPID = WorkerServer::Instance().GetPrevAltServerPid();
				pOut = ExecuteSrvCmd(gnMainServerPID, pIn, gState.realConWnd_);
			}
		}
		else
		{
			PRINT_COMSPEC(L"Finalizing comspec mode (ExecuteGuiCmd started)\n",0);
			WARNING("Это надо бы совместить, но пока - нужно сначала передернуть главный сервер!");
			pOut = ExecuteSrvCmd(gnMainServerPID, pIn, gState.realConWnd_);
			_ASSERTE(pOut!=nullptr);
			ExecuteFreeResult(pOut);
			pOut = ExecuteGuiCmd(gState.realConWnd_, pIn, gState.realConWnd_);
			PRINT_COMSPEC(L"Finalizing comspec mode (ExecuteGuiCmd finished)\n",0);
		}

		ExecuteFreeResult(pIn); pIn = nullptr;
	}

	return pOut;
}

void CreateLogSizeFile(int /* level */, const CESERVER_CONSOLE_MAPPING_HDR* pConsoleInfo /*= nullptr*/)
{
	if (gpLogSize) return;  // уже

	DWORD dwErr = 0;
	wchar_t szFile[MAX_PATH+64], *pszDot, *pszName, *pszDir = nullptr;
	wchar_t szDir[MAX_PATH+16];

	if (!GetModuleFileName(nullptr, szFile, MAX_PATH))
	{
		dwErr = GetLastError();
		_printf("GetModuleFileName failed! ErrCode=0x%08X\n", dwErr);
		return; // не удалось
	}

	pszName = const_cast<wchar_t*>(PointToName(szFile));

	if ((pszDot = wcsrchr(pszName, L'.')) == nullptr)
	{
		_printf("wcsrchr failed!\n", 0, szFile); //-V576
		return; // ошибка
	}

	if (pszName && (pszName > szFile))
	{
		// If we were started from ConEmu, the pConsoleInfo must be defined...
		if (pConsoleInfo && pConsoleInfo->cbSize && pConsoleInfo->sConEmuExe[0])
		{
			lstrcpyn(szDir, pConsoleInfo->sConEmuExe, countof(szDir));
			wchar_t* pszConEmuExe = (wchar_t*)PointToName(szDir);
			if (pszConEmuExe)
			{
				*(pszConEmuExe-1) = 0;
				pszDir = szDir;
			}
		}

		if (!pszDir)
		{
			// if our exe lays in subfolder of ConEmu.exe?
			*(pszName-1) = 0;
			wcscpy_c(szDir, szFile);
			pszDir = wcsrchr(szDir, L'\\');

			if (!pszDir || (lstrcmpi(pszDir, L"\\ConEmu") != 0))
			{
				// Если ConEmuC.exe лежит НЕ в папке "ConEmu"
				pszDir = szDir; // писать в текущую папку или на десктоп
			}
			else
			{
				// Иначе - попытаться найти соответствующий файл GUI
				wchar_t szGuiFiles[] = L"\\ConEmu.exe" L"\0" L"\\ConEmu64.exe" L"\0\0";
				if (FilesExists(szDir, szGuiFiles))
				{
					pszDir = szFile; // GUI лежит в той же папке, что и "сервер"
				}
				else
				{
					// На уровень выше?
					*pszDir = 0;
					if (FilesExists(szDir, szGuiFiles))
					{
						*pszDir = 0;
						pszDir = szDir; // GUI лежит в родительской папке
					}
					else
					{
						pszDir = szFile; // GUI не нашли
					}
				}
			}
		}
	}

	gpLogSize = new MFileLogEx(L"ConEmu-srv", pszDir, GetCurrentProcessId());

	if (!gpLogSize)
		return;

	dwErr = (DWORD)gpLogSize->CreateLogFile();
	if (dwErr != 0)
	{
		_printf("Create console log file failed! ErrCode=0x%08X\n", dwErr, gpLogSize->GetLogFileName()); //-V576
		_ASSERTE(FALSE && "gpLogSize->CreateLogFile failed");
		SafeDelete(gpLogSize);
		return;
	}

	gpLogSize->LogStartEnv(gpStartEnv);

	LogSize(nullptr, 0, "Startup");
}

bool LogString(LPCSTR asText)
{
	if (!gpLogSize)
	{
		#ifdef _DEBUG
		if (asText && *asText)
		{
			wchar_t* pszWide = lstrdupW(asText);
			if (pszWide)
			{
				DEBUGSTR(pszWide);
				free(pszWide);
			}
		}
		#endif
		return false;
	}

	// ReSharper disable once CppLocalVariableMayBeConst
	LPCSTR pszThread = gpWorker ? gpWorker->GetCurrentThreadLabel() : " <unknown thread>";

	gpLogSize->LogString(asText, true, pszThread);
	return true;
}

bool LogString(LPCWSTR asText)
{
	if (!gpLogSize)
	{
		DEBUGSTR(asText);
		return false;
	}

	// ReSharper disable once CppLocalVariableMayBeConst
	LPCSTR pszThread = gpWorker ? gpWorker->GetCurrentThreadLabel() : " <unknown thread>";
	wchar_t nameBuffer[32] = L"";
	MultiByteToWideChar(CP_UTF8, 0, pszThread, -1, nameBuffer, static_cast<int>(std::size(nameBuffer) - 1));

	gpLogSize->LogString(asText, true, nameBuffer);
	return false;
}

void LogSize(const COORD* pcrSize, int newBufferHeight, LPCSTR pszLabel, bool bForceWriteLog)
{
	if (!gpLogSize) return;

	static DWORD nLastWriteTick = 0;
	const DWORD TickDelta = 1000;
	static LONG nSkipped = 0;
	const LONG SkipDelta = 50;
	static CONSOLE_SCREEN_BUFFER_INFO lsbiLast = {};
	bool bWriteLog = false;
	CONSOLE_SCREEN_BUFFER_INFO lsbi = {};
	CONSOLE_CURSOR_INFO ci = {(DWORD)-1};

	HANDLE hCon = ghConOut;
	BOOL bHandleOK = (hCon != nullptr);
	if (!bHandleOK)
		hCon = GetStdHandle(STD_OUTPUT_HANDLE);

	// В дебажный лог помещаем реальные значения
	BOOL bConsoleOK = GetConsoleScreenBufferInfo(hCon, &lsbi);
	char szInfo[400]; szInfo[0] = 0;
	DWORD nErrCode = GetLastError();

	// Cursor information (gh-718)
	GetConsoleCursorInfo(hCon, &ci);

	int fontX = 0, fontY = 0; wchar_t szFontName[LF_FACESIZE] = L""; char szFontInfo[60] = "<NA>";
	if (apiGetConsoleFontSize(hCon, fontY, fontX, szFontName))
	{
		LPCSTR szState = "";
		if (!g_LastSetConsoleFont.cbSize)
		{
			szState = " <?>";
		}
		else if ((g_LastSetConsoleFont.dwFontSize.Y != fontY)
			|| (g_LastSetConsoleFont.dwFontSize.X != fontX))
		{
			// nConFontHeight/nConFontWidth не совпадает с получаемым,
			// нужно организовать спец переменные в WConsole
			_ASSERTE(g_LastSetConsoleFont.dwFontSize.Y==fontY && g_LastSetConsoleFont.dwFontSize.X==fontX);
			szState = " <!>";
		}

		szFontInfo[0] = '`';
		int iCvt = WideCharToMultiByte(CP_UTF8, 0, szFontName, -1, szFontInfo+1, 40, nullptr, nullptr);
		if (iCvt <= 0) lstrcpynA(szFontInfo+1, "??", 40); else szFontInfo[iCvt+1] = 0;
		int iLen = lstrlenA(szFontInfo); // result of WideCharToMultiByte is not suitable (contains trailing zero)
		sprintf_c(szFontInfo+iLen, countof(szFontInfo)-iLen/*#SECURELEN*/, "` %ix%i%s",
			fontY, fontX, szState);
	}

	#ifdef _DEBUG
	wchar_t szClass[100] = L"";
	GetClassName(gState.realConWnd_, szClass, countof(szClass));
	_ASSERTE(lstrcmp(szClass, L"ConsoleWindowClass")==0);
	#endif

	char szWindowInfo[40] = "<NA>"; RECT rcConsole = {};
	if (GetWindowRect(gState.realConWnd_, &rcConsole))
	{
		sprintf_c(szWindowInfo, "{(%i,%i) (%ix%i)}", rcConsole.left, rcConsole.top, LOGRECTSIZE(rcConsole));
	}

	if (pcrSize)
	{
		bWriteLog = true;

		sprintf_c(szInfo, "CurSize={%i,%i,%i} ChangeTo={%i,%i,%i} Cursor={%i,%i,%i%%%c} ConRect={%i,%i}-{%i,%i} %s (skipped=%i) {%u:%u:x%X:%u} %s %s",
		           lsbi.dwSize.X, lsbi.srWindow.Bottom-lsbi.srWindow.Top+1, lsbi.dwSize.Y, pcrSize->X, pcrSize->Y, newBufferHeight,
		           lsbi.dwCursorPosition.X, lsbi.dwCursorPosition.Y, ci.dwSize, ci.bVisible ? L'V' : L'H',
		           lsbi.srWindow.Left, lsbi.srWindow.Top, lsbi.srWindow.Right, lsbi.srWindow.Bottom,
		           (pszLabel ? pszLabel : ""), nSkipped,
		           bConsoleOK, bHandleOK, (DWORD)(DWORD_PTR)hCon, nErrCode,
				   szWindowInfo, szFontInfo);
	}
	else
	{
		if (bForceWriteLog)
			bWriteLog = true;
		// Avoid numerous writing of equal lines to log
		else if ((lsbi.dwSize.X != lsbiLast.dwSize.X) || (lsbi.dwSize.Y != lsbiLast.dwSize.Y))
			bWriteLog = true;
		else if (((GetTickCount() - nLastWriteTick) >= TickDelta) || (nSkipped >= SkipDelta))
			bWriteLog = true;

		sprintf_c(szInfo, "CurSize={%i,%i,%i} Cursor={%i,%i,%i%%%c} ConRect={%i,%i}-{%i,%i} %s (skipped=%i) {%u:%u:x%X:%u} %s %s",
		           lsbi.dwSize.X, lsbi.srWindow.Bottom-lsbi.srWindow.Top+1, lsbi.dwSize.Y,
		           lsbi.dwCursorPosition.X, lsbi.dwCursorPosition.Y, ci.dwSize, ci.bVisible ? L'V' : L'H',
		           lsbi.srWindow.Left, lsbi.srWindow.Top, lsbi.srWindow.Right, lsbi.srWindow.Bottom,
		           (pszLabel ? pszLabel : ""), nSkipped,
		           bConsoleOK, bHandleOK, (DWORD)(DWORD_PTR)hCon, nErrCode,
				   szWindowInfo, szFontInfo);
	}

	lsbiLast = lsbi;

	if (!bWriteLog)
	{
		InterlockedIncrement(&nSkipped);
	}
	else
	{
		nSkipped = 0;
		LogFunction(szInfo);
		nLastWriteTick = GetTickCount();
	}
}

void LogModeChange(LPCWSTR asName, DWORD oldVal, DWORD newVal)
{
	if (!gpLogSize) return;

	LPCWSTR pszLabel = asName ? asName : L"???";
	CEStr lsInfo;
	INT_PTR cchLen = lstrlen(pszLabel) + 80;
	swprintf_c(lsInfo.GetBuffer(cchLen), cchLen/*#SECURELEN*/, L"Mode %s changed: old=x%04X new=x%04X", pszLabel, oldVal, newVal);
	LogString(lsInfo);
}


static void UndoConsoleWindowZoom()
{
	BOOL lbRc = FALSE;

	// Если юзер случайно нажал максимизацию, когда консольное окно видимо - ничего хорошего не будет
	if (IsZoomed(gState.realConWnd_))
	{
		SendMessage(gState.realConWnd_, WM_SYSCOMMAND, SC_RESTORE, 0);
		DWORD dwStartTick = GetTickCount();

		do
		{
			Sleep(20); // подождем чуть, но не больше секунды
		}
		while (IsZoomed(gState.realConWnd_) && (GetTickCount()-dwStartTick)<=1000);

		Sleep(20); // и еще чуть-чуть, чтобы консоль прочухалась
		// Теперь нужно вернуть (вдруг он изменился) размер буфера консоли
		// Если этого не сделать - размер консоли нельзя УМЕНЬШИТЬ
		RECT rcConPos;
		GetWindowRect(gState.realConWnd_, &rcConPos);
		MoveWindow(gState.realConWnd_, rcConPos.left, rcConPos.top, 1, 1, 1);

		TODO("Horizontal scroll");
		if (gnBufferHeight == 0)
		{
			//specified width and height cannot be less than the width and height of the console screen buffer's window
			lbRc = SetConsoleScreenBufferSize(ghConOut, gcrVisibleSize);
		}
		else
		{
			// ресайз для BufferHeight
			COORD crHeight = {gcrVisibleSize.X, gnBufferHeight};
			MoveWindow(gState.realConWnd_, rcConPos.left, rcConPos.top, 1, 1, 1);
			lbRc = SetConsoleScreenBufferSize(ghConOut, crHeight); // а не crNewSize - там "оконные" размеры
		}

		// И вернуть тот видимый прямоугольник, который был получен в последний раз (успешный раз)
		lbRc = SetConsoleWindowInfo(ghConOut, TRUE, &gpWorker->GetSbi().srWindow);
	}

	UNREFERENCED_PARAMETER(lbRc);
}

bool static NeedLegacyCursorCorrection()
{
	static bool bNeedCorrection = false, bChecked = false;

	if (!bChecked)
	{
		// gh-1051: In NON DBCS systems there are cursor problems too (Win10 stable build 15063 or higher)
		if (IsWin10() && !IsWinDBCS() && !IsWin10LegacyConsole())
		{
			OSVERSIONINFOEXW osvi = { sizeof(osvi), 10, 0, 15063 };
			DWORDLONG const dwlConditionMask = VerSetConditionMask(VerSetConditionMask(VerSetConditionMask(0,
				VER_MAJORVERSION, VER_GREATER_EQUAL),
				VER_MINORVERSION, VER_GREATER_EQUAL),
				VER_BUILDNUMBER, VER_GREATER_EQUAL);
			BOOL ibIsWin = _VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER, dwlConditionMask);
			if (ibIsWin)
			{
				bNeedCorrection = true;
			}
		}

		bChecked = true;
	}

	return bNeedCorrection;
}

// TODO: Optimize - combine ReadConsoleData/ReadConsoleInfo
void static CorrectDBCSCursorPosition(HANDLE ahConOut, CONSOLE_SCREEN_BUFFER_INFO& csbi)
{
	if (csbi.dwSize.X <= 0 || csbi.dwCursorPosition.X <= 0)
		return;

	// Issue 577: Chinese display error on Chinese
	// -- GetConsoleScreenBufferInfo returns DBCS cursor coordinates, but we require wchar_t !!!
	if (!IsConsoleDoubleCellCP()
	// gh-1057: In NON DBCS systems there are cursor problems too (Win10 stable build 15063)
		&& !NeedLegacyCursorCorrection())
		return;

	// The correction process
	{
		CHAR Chars[200];
		LONG cchMax = countof(Chars);
		LPSTR pChars = (csbi.dwCursorPosition.X <= cchMax) ? Chars : (LPSTR)calloc(csbi.dwCursorPosition.X, sizeof(*pChars));
		if (pChars)
			cchMax = csbi.dwCursorPosition.X;
		else
			pChars = Chars; // memory allocation fail? try part of line?
		COORD crRead = {0, csbi.dwCursorPosition.Y};
		// gh-1501: Windows 10 CJK version set COMMON_LVB_*_BYTE even for SOME non-ASCII characters (e.g. Greek "Α", Russian "Я", etc.)
		#if 0
		//120830 - DBCS uses 2 cells per hieroglyph
		// TODO: Optimize
		DWORD nRead = 0;
		BOOL bRead = NeedLegacyCursorCorrection() ? FALSE : ReadConsoleOutputCharacterA(ahConOut, pChars, cchMax, crRead, &nRead);
		// ReadConsoleOutputCharacterA may return "???" instead of DBCS codes in Non-DBCS systems
		if (bRead && (nRead == cchMax))
		{
			_ASSERTE(nRead==cchMax);
			int nXShift = 0;
			LPSTR p = pChars, pEnd = pChars+nRead;
			while (p < pEnd)
			{
				if (IsDBCSLeadByte(*p))
				{
					nXShift++;
					p++;
					_ASSERTE(p < pEnd);
				}
				p++;
			}
			_ASSERTE(nXShift <= csbi.dwCursorPosition.X);
			csbi.dwCursorPosition.X = std::max(0,(csbi.dwCursorPosition.X - nXShift));
		}
		else if (IsWin10() && (NeedLegacyCursorCorrection() || (GetConsoleOutputCP() == CP_UTF8)))
		#endif
		{
			// Temporary workaround for conhost bug!
			CHAR_INFO CharsEx[200];
			CHAR_INFO* pCharsEx = (cchMax <= countof(CharsEx)) ? CharsEx
				: (CHAR_INFO*)calloc(cchMax, sizeof(*pCharsEx));
			if (pCharsEx)
			{
				COORD bufSize = {MakeShort(cchMax), 1}; COORD bufCoord = {0,0};
				SMALL_RECT rgn = MakeSmallRect(0, csbi.dwCursorPosition.Y, cchMax-1, csbi.dwCursorPosition.Y);
				BOOL bRead = ReadConsoleOutputW(ahConOut, pCharsEx, bufSize, bufCoord, &rgn);
				if (bRead)
				{
					int nXShift = 0;
					CHAR_INFO *p = pCharsEx, *pEnd = pCharsEx+cchMax;
					// gh-1206: correct position after "こんばんはGood Evening"
					while (p < pEnd)
					{
						if (!(p->Attributes & COMMON_LVB_TRAILING_BYTE)
							/*&& get_wcwidth(p->Char.UnicodeChar) == 2*/)
						{
							_ASSERTE(p < pEnd);
							if (((p + 1) < pEnd)
								&& (p->Attributes & COMMON_LVB_LEADING_BYTE)
								&& ((p+1)->Attributes & COMMON_LVB_TRAILING_BYTE)
								&& (p->Char.UnicodeChar == (p+1)->Char.UnicodeChar))
							{
								p++; nXShift++;
							}
							_ASSERTE(p < pEnd);
						}
						p++;
					}

					_ASSERTE(nXShift <= csbi.dwCursorPosition.X);
					csbi.dwCursorPosition.X = std::max<int>(0, (csbi.dwCursorPosition.X - nXShift));

					if (pCharsEx != CharsEx)
						free(pCharsEx);
				}
			}
		}
		if (pChars != Chars)
			free(pChars);
	}
}


/**
Reload current console palette, IF it WAS NOT specified explicitly before

@param ahConOut << (HANDLE)ghConOut
@result true if palette was successfully retrieved
**/
bool MyLoadConsolePalette(HANDLE ahConOut, CESERVER_CONSOLE_PALETTE& Palette)
{
	bool bRc = false;

	if (gpSrv->ConsolePalette.bPalletteLoaded)
	{
		Palette = gpSrv->ConsolePalette;
		bRc = true;
	}
	else
	{
		Palette.bPalletteLoaded = false;
	}

	CONSOLE_SCREEN_BUFFER_INFO sbi = {};
	MY_CONSOLE_SCREEN_BUFFER_INFOEX sbi_ex = {sizeof(sbi_ex)};

	if (apiGetConsoleScreenBufferInfoEx(ahConOut, &sbi_ex))
	{
		Palette.wAttributes = sbi_ex.wAttributes;
		Palette.wPopupAttributes = sbi_ex.wPopupAttributes;

		if (!bRc)
		{
			_ASSERTE(sizeof(Palette.ColorTable) == sizeof(sbi_ex.ColorTable));
			memmove(Palette.ColorTable, sbi_ex.ColorTable, sizeof(Palette.ColorTable));

			// Store for future use?
			// -- gpSrv->ConsolePalette = Palette;

			bRc = true;
		}
	}
	else if (GetConsoleScreenBufferInfo(ahConOut, &sbi))
	{
		Palette.wAttributes = Palette.wPopupAttributes = sbi.wAttributes;
		// Load palette from registry?
	}

	return bRc;
}


// #WARNING Use MyGetConsoleScreenBufferInfo instead of GetConsoleScreenBufferInfo

// Действует аналогично функции WinApi (GetConsoleScreenBufferInfo), но в режиме сервера:
// 1. запрещает (то есть отменяет) максимизацию консольного окна
// 2. корректирует srWindow: сбрасывает горизонтальную прокрутку,
//    а если не обнаружен "буферный режим" - то и вертикальную.
BOOL MyGetConsoleScreenBufferInfo(HANDLE ahConOut, PCONSOLE_SCREEN_BUFFER_INFO apsc)
{
	BOOL lbRc = FALSE, lbSetRc = TRUE;
	DWORD nErrCode = 0;

	// ComSpec окно менять НЕ ДОЛЖЕН!
	if ((gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer)
		&& gState.conemuWnd_ && IsWindow(gState.conemuWnd_))
	{
		// Если юзер случайно нажал максимизацию, когда консольное окно видимо - ничего хорошего не будет
		if (IsZoomed(gState.realConWnd_))
		{
			UndoConsoleWindowZoom();
		}
	}

	SMALL_RECT srRealWindow = {};
	CONSOLE_SCREEN_BUFFER_INFO csbi = {};
	lbRc = GetConsoleScreenBufferInfo(ahConOut, &csbi);
	if (!lbRc)
	{
		nErrCode = GetLastError();
		if (gpLogSize) LogSize(nullptr, 0, ":MyGetConsoleScreenBufferInfo(FAIL)");
		_ASSERTE(FALSE && "GetConsoleScreenBufferInfo failed, conhost was destroyed?");
		goto wrap;
	}

	if ((csbi.dwSize.X <= 0)
		|| (csbi.dwSize.Y <= 0)
		|| (csbi.dwSize.Y > LONGOUTPUTHEIGHT_MAX)
		)
	{
		nErrCode = GetLastError();
		if (gpLogSize) LogSize(nullptr, 0, ":MyGetConsoleScreenBufferInfo(dwSize)");
		_ASSERTE(FALSE && "GetConsoleScreenBufferInfo failed, conhost was destroyed?");
		goto wrap;
	}

	if (csbi.srWindow.Left < 0 || csbi.srWindow.Top < 0 || csbi.srWindow.Right < csbi.srWindow.Left || csbi.srWindow.Bottom < csbi.srWindow.Top)
	{
		nErrCode = GetLastError();
		if (gpLogSize)
			LogSize(nullptr, 0, ":MyGetConsoleScreenBufferInfo(srWindow)");
		_ASSERTE(FALSE && "GetConsoleScreenBufferInfo failed, Invalid srWindow coordinates");
		goto wrap;
	}

	srRealWindow = csbi.srWindow;

	// Issue 373: wmic (Horizontal buffer)
	{
		TODO("Его надо и из ConEmu обновлять");
		if (csbi.dwSize.X && (gnBufferWidth != csbi.dwSize.X))
			gnBufferWidth = csbi.dwSize.X;
		else if (gnBufferWidth == (csbi.srWindow.Right - csbi.srWindow.Left + 1))
			gnBufferWidth = 0;
	}

	// CONSOLE_FULLSCREEN/*1*/ or CONSOLE_FULLSCREEN_HARDWARE/*2*/
	if (gpWorker->CheckHwFullScreen())
	{
		// While in hardware fullscreen - srWindow still shows window region
		// as it can be, if returned in GUI mode (I suppose)
		//_ASSERTE(!csbi.srWindow.Left && !csbi.srWindow.Top);
		csbi.dwSize.X = csbi.srWindow.Right - csbi.srWindow.Left + 1;
		csbi.dwSize.Y = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
		// Log that change
		if (gpLogSize)
		{
			char szLabel[80];
			sprintf_c(szLabel, "CONSOLE_FULLSCREEN_HARDWARE{x%08X}", gState.consoleDisplayMode_);
			LogSize(&csbi.dwSize, 0, szLabel);
		}
	}

	//
	_ASSERTE((csbi.srWindow.Bottom-csbi.srWindow.Top)<200);

	// ComSpec окно менять НЕ ДОЛЖЕН!
	// ??? Приложениям запрещено менять размер видимой области.
	// ??? Размер буфера - могут менять, но не менее чем текущая видимая область
	if ((gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer) && gpSrv)
	{
		// Если мы НЕ в ресайзе, проверить максимально допустимый размер консоли
		if (!gpSrv->nRequestChangeSize && (gpSrv->crReqSizeNewSize.X > 0) && (gpSrv->crReqSizeNewSize.Y > 0))
		{
			COORD crMax = MyGetLargestConsoleWindowSize(ghConOut);
			// Это может случиться, если пользователь резко уменьшил разрешение экрана
			// ??? Польза здесь сомнительная
			if (crMax.X > 0 && crMax.X < gpSrv->crReqSizeNewSize.X)
			{
				WorkerBase::PreConsoleSize(crMax.X, gpSrv->crReqSizeNewSize.Y);
				gpSrv->crReqSizeNewSize.X = crMax.X;
				TODO("Обновить gcrVisibleSize");
			}
			if (crMax.Y > 0 && crMax.Y < gpSrv->crReqSizeNewSize.Y)
			{
				WorkerBase::PreConsoleSize(gpSrv->crReqSizeNewSize.X, crMax.Y);
				gpSrv->crReqSizeNewSize.Y = crMax.Y;
				TODO("Обновить gcrVisibleSize");
			}
		}
	}

	// Issue 577: Chinese display error on Chinese
	// -- GetConsoleScreenBufferInfo возвращает координаты курсора в DBCS, а нам нужен wchar_t !!!
	if (csbi.dwSize.X && csbi.dwCursorPosition.X)
	{
		CorrectDBCSCursorPosition(ahConOut, csbi);
	}

	// Блокировка видимой области, TopLeft задается из GUI
	if ((gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer) && gpSrv)
	{
		// Коррекция srWindow
		if (gpSrv->TopLeft.isLocked())
		{
			SHORT nWidth = (csbi.srWindow.Right - csbi.srWindow.Left + 1);
			SHORT nHeight = (csbi.srWindow.Bottom - csbi.srWindow.Top + 1);

			if (gpSrv->TopLeft.x >= 0)
			{
				csbi.srWindow.Right = std::min<int>(csbi.dwSize.X, gpSrv->TopLeft.x + nWidth) - 1;
				csbi.srWindow.Left = std::max<int>(0, csbi.srWindow.Right - nWidth + 1);
			}
			if (gpSrv->TopLeft.y >= 0)
			{
				csbi.srWindow.Bottom = std::min<int>(csbi.dwSize.Y, gpSrv->TopLeft.y+nHeight) - 1;
				csbi.srWindow.Top = std::max<int>(0, csbi.srWindow.Bottom-nHeight+1);
			}
		}

		// Eсли
		// a) в прошлый раз курсор БЫЛ видим, а сейчас нет, И
		// b) TopLeft не был изменен с тех пор (GUI не прокручивали)
		// Это значит, что в консоли, например, нажали Enter в prompt
		// или приложение продолжает вывод в консоль, в общем,
		// содержимое консоли прокручивается,
		// и то же самое нужно сделать в GUI
		if (gpSrv->pConsole && gpSrv->TopLeft.isLocked())
		{
			// Был видим в той области, которая ушла в GUI
			if (CoordInSmallRect(gpSrv->pConsole->ConState.sbi.dwCursorPosition, gpSrv->pConsole->ConState.sbi.srWindow)
				// и видим сейчас в RealConsole
				&& CoordInSmallRect(csbi.dwCursorPosition, srRealWindow)
				// но стал НЕ видим в скорректированном (по TopLeft) прямоугольнике
				&& !CoordInSmallRect(csbi.dwCursorPosition, csbi.srWindow)
				// И TopLeft в GUI не менялся
				&& gpSrv->TopLeft.Equal(gpSrv->pConsole->ConState.TopLeft))
			{
				// Сбросить блокировку и вернуть реальное положение в консоли
				gpSrv->TopLeft.Reset();
				csbi.srWindow = srRealWindow;
			}
		}

		if (gpSrv->pConsole)
		{
			gpSrv->pConsole->ConState.TopLeft = gpSrv->TopLeft;
			gpSrv->pConsole->ConState.srRealWindow = srRealWindow;
		}
	}

wrap:
	if (apsc)
		*apsc = csbi;
	// Возвращаем (возможно) скорректированные данные
	UNREFERENCED_PARAMETER(nErrCode);
	return lbRc;
}

BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{
	// Log it first
	wchar_t szLog[80], szType[20];
	switch (dwCtrlType)
	{
	case CTRL_C_EVENT:
		wcscpy_c(szType, L"CTRL_C_EVENT"); break;
	case CTRL_BREAK_EVENT:
		wcscpy_c(szType, L"CTRL_BREAK_EVENT"); break;
	case CTRL_CLOSE_EVENT:
		wcscpy_c(szType, L"CTRL_CLOSE_EVENT"); break;
	case CTRL_LOGOFF_EVENT:
		wcscpy_c(szType, L"CTRL_LOGOFF_EVENT"); break;
	case CTRL_SHUTDOWN_EVENT:
		wcscpy_c(szType, L"CTRL_SHUTDOWN_EVENT"); break;
	default:
		msprintf(szType, countof(szType), L"ID=%u", dwCtrlType);
	}
	wcscpy_c(szLog, L"  ---  ConsoleCtrlHandler triggered: ");
	wcscat_c(szLog, szType);
	LogString(szLog);

	// Continue processing

	//PRINT_COMSPEC(L"HandlerRoutine triggered. Event type=%i\n", dwCtrlType);
	if ((dwCtrlType == CTRL_CLOSE_EVENT)
		|| (dwCtrlType == CTRL_LOGOFF_EVENT)
		|| (dwCtrlType == CTRL_SHUTDOWN_EVENT))
	{
		PRINT_COMSPEC(L"Console about to be closed\n", 0);
		WARNING("Тут бы подождать немного, пока другие консольные процессы завершатся... а то таб закрывается раньше времени");
		gbInShutdown = TRUE;

		if (gState.inExitWaitForKey_)
			gbStopExitWaitForKey = TRUE;

		// Our debugger is running?
		if (gpWorker->IsDebuggerActive())
		{
			// pfnDebugActiveProcessStop is useless, because
			// 1. pfnDebugSetProcessKillOnExit was called already
			// 2. we can debug more than a one process

			//gpSrv->DbgInfo.bDebuggerActive = FALSE;
		}
		else
		{
			#if 0
			wchar_t szTitle[128]; wsprintf(szTitle, L"ConEmuC, PID=%u", GetCurrentProcessId());
			//MessageBox(nullptr, L"CTRL_CLOSE_EVENT in ConEmuC", szTitle, MB_SYSTEMMODAL);
			DWORD nWait = WaitForSingleObject(ghExitQueryEvent, CLOSE_CONSOLE_TIMEOUT);
			if (nWait == WAIT_OBJECT_0)
			{
				#ifdef _DEBUG
				OutputDebugString(L"All console processes was terminated\n");
				#endif
			}
			else
			{
				// Поскольку мы (сервер) сейчас свалимся, то нужно показать
				// консольное окно, раз в нем остались какие-то процессы
				EmergencyShow();
			}
			#endif

			// trick to let ConsoleMain2() finish correctly
			ExitThread(1);
		}
	}
	else if ((dwCtrlType == CTRL_C_EVENT) || (dwCtrlType == CTRL_BREAK_EVENT))
	{
		if (gbTerminateOnCtrlBreak)
		{
			PRINT_COMSPEC(L"Ctrl+Break received, server will be terminated\n", 0);
			gbInShutdown = TRUE;
		}
		else if (gpWorker->IsDebugProcess())
		{
			const DWORD nWait = WaitForSingleObject(gpWorker->RootProcessHandle(), 0);
			#ifdef _DEBUG
			const DWORD nErr = GetLastError();
			#endif
			if (nWait == WAIT_OBJECT_0)
			{
				_ASSERTE(gbTerminateOnCtrlBreak==FALSE);
				PRINT_COMSPEC(L"Ctrl+Break received, debugger will be stopped\n", 0);
				//if (pfnDebugActiveProcessStop) pfnDebugActiveProcessStop(gpWorker->RootProcessId());
				//gpSrv->DbgInfo.bDebuggerActive = FALSE;
				//gbInShutdown = TRUE;
				SetTerminateEvent(ste_HandlerRoutine);
			}
			else
			{
				gpWorker->DbgInfo().GenerateMiniDumpFromCtrlBreak();
			}
		}
	}

	return TRUE;
}

void _printf(LPCSTR asFormat, DWORD dw1, DWORD dw2, LPCWSTR asAddLine)
{
	char szError[MAX_PATH];
	sprintf_c(szError, asFormat, dw1, dw2);
	_printf(szError);

	if (asAddLine)
	{
		_wprintf(asAddLine);
		_printf("\n");
	}
}

void _printf(LPCSTR asFormat, DWORD dwErr, LPCWSTR asAddLine)
{
	char szError[MAX_PATH];
	sprintf_c(szError, asFormat, dwErr);
	_printf(szError);

	if (asAddLine)
	{
		_wprintf(asAddLine);
		_printf("\n");
	}
}

void _printf(LPCSTR asFormat, DWORD dwErr)
{
	char szError[MAX_PATH];
	sprintf_c(szError, asFormat, dwErr);
	_printf(szError);
}

void _printf(LPCSTR asBuffer)
{
	if (!asBuffer) return;

	int nAllLen = lstrlenA(asBuffer);
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD dwWritten = 0;
	DEBUGTEST(BOOL bWriteRC =)
	WriteFile(hOut, asBuffer, nAllLen, &dwWritten, 0);
	UNREFERENCED_PARAMETER(dwWritten);
}

void print_error(DWORD dwErr/*= 0*/, LPCSTR asFormat/*= nullptr*/)
{
	if (!dwErr)
		dwErr = GetLastError();

	wchar_t* lpMsgBuf = nullptr;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, dwErr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&lpMsgBuf, 0, nullptr);

	_printf(asFormat ? asFormat : "\nErrCode=0x%08X, Description:\n", dwErr);
	_wprintf((!lpMsgBuf || !*lpMsgBuf) ? L"<Unknown error>" : lpMsgBuf);

	if (lpMsgBuf) LocalFree(lpMsgBuf);
	SetLastError(dwErr);
}

bool IsOutputRedirected()
{
	static int isRedirected = 0;
	if (isRedirected)
	{
		return (isRedirected == 2);
	}

	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

	CONSOLE_SCREEN_BUFFER_INFO sbi = {};
	BOOL bIsConsole = GetConsoleScreenBufferInfo(hOut, &sbi);
	if (bIsConsole)
	{
		isRedirected = 1;
		return false;
	}
	else
	{
		isRedirected = 2;
		return true;
	}
}

void _wprintf(LPCWSTR asBuffer)
{
	if (!asBuffer) return;
	WriteOutput(asBuffer);
}
