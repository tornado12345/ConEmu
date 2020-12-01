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

#define SHOWDEBUGSTR
//#define DEBUGSTRINPUTEVENT(s) //DEBUGSTR(s) // SetEvent(gpSrv->hInputEvent)
//#define DEBUGLOGINPUT(s) DEBUGSTR(s) // ConEmuC.MouseEvent(X=
//#define DEBUGSTRINPUTWRITE(s) DEBUGSTR(s) // *** ConEmuC.MouseEvent(X=
//#define DEBUGSTRINPUTWRITEALL(s) //DEBUGSTR(s) // *** WriteConsoleInput(Write=
//#define DEBUGSTRINPUTWRITEFAIL(s) DEBUGSTR(s) // ### WriteConsoleInput(Write=
//#define DEBUGSTRCHANGES(s) DEBUGSTR(s)
#define DEBUGSTRSIZE(x) DEBUGSTR(x)

#undef TEST_REFRESH_DELAYED

#include "ConsoleMain.h"
#include "ConEmuSrv.h"
#include "LogFunction.h"
#include "InjectRemote.h"
#include "InputLogger.h"
#include "../common/CmdLine.h"
#include "../common/ConsoleAnnotation.h"
#include "../common/ConsoleRead.h"
#include "../common/EmergencyShow.h"
#include "../common/EnvVar.h"
#include "../common/MPerfCounter.h"
#include "../common/MProcess.h"
#include "../common/MProcessBits.h"
#include "../common/MRect.h"
#include "../common/MSectionSimple.h"
#include "../common/MStrDup.h"
#include "../common/MStrSafe.h"
#include "../common/SetEnvVar.h"
#include "../common/StartupEnvDef.h"
#include "../common/WConsoleEx.h"
#include "../common/WConsoleInfo.h"
#include "../common/WFiles.h"
#include "../common/WThreads.h"
#include "../common/WObjects.h"
#include "../common/WUser.h"
//#include "TokenHelper.h"
#include "ConProcess.h"
#include "ConsoleArgs.h"
#include "ConsoleState.h"
#include "DumpOnException.h"
#include "SrvPipes.h"
#include "Queue.h"
#include "StartEnv.h"

#ifdef _DEBUG
	//#define DEBUG_SLEEP_NUMLCK
	#undef DEBUG_SLEEP_NUMLCK
#else
	#undef DEBUG_SLEEP_NUMLCK
#endif

#define MAX_EVENTS_PACK 20

#ifdef _DEBUG
//#define ASSERT_UNWANTED_SIZE
#else
#undef ASSERT_UNWANTED_SIZE
#endif

//Used to store and restore console screen buffers in cmd_AltBuffer
MConHandle gPrimaryBuffer(nullptr), gAltBuffer(nullptr);
USHORT gnPrimaryBufferLastRow = 0; // last detected written row in gPrimaryBuffer

BOOL    gbTerminateOnExit = FALSE;  // for debugging purposed


namespace
{
// Thread reloading console contents
DWORD WINAPI RefreshThreadProc(LPVOID lpParameter)
{
	return WorkerServer::Instance().RefreshThread(lpParameter);
}

DWORD WINAPI SetOemCpThreadProc(LPVOID lpParameter)
{
	return WorkerServer::Instance().SetOemCpThread(lpParameter);
}
}

void SrvInfo::InitFields()
{
	TopLeft.Reset();
}
void SrvInfo::FinalizeFields()
{
}

WorkerServer::~WorkerServer()
{
	_ASSERTE(gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer || gState.runMode_ == RunMode::AutoAttach);

	csColorerMappingCreate.Close();
	csRefreshControl.Close();
	AltServers.Release();
}

WorkerServer::WorkerServer()
	: WorkerBase()
{
	_ASSERTE(gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer || gState.runMode_ == RunMode::AutoAttach);

	AltServers.Init();

	if (gState.runMode_ == RunMode::Server)
	{
		SetupCreateDumpOnException();
	}

	if (gState.conemuWnd_)
	{
		GetWindowThreadProcessId(gState.conemuWnd_, &gState.conemuPid_);
	}
}

WorkerServer& WorkerServer::Instance()
{
	auto* server = dynamic_cast<WorkerServer*>(gpWorker);
	if (!server)
	{
		_ASSERTE(server != nullptr);
		LogString("!!! WorkerServer was not initialized !!!");
		_printf("\n!!! WorkerServer was not initialized !!!\n\n");
		ExitProcess(CERR_SERVER_WAS_NOT_INITIALIZED);
	}
	return *server;
}

// static callback for ServerInitFont
int WorkerServer::FontEnumProc(ENUMLOGFONTEX *lpelfe, NEWTEXTMETRICEX *, DWORD FontType, LPARAM lParam)
{
	if ((FontType & TRUETYPE_FONTTYPE) == TRUETYPE_FONTTYPE)
	{
		// OK, suitable
		*reinterpret_cast<bool*>(lParam) = true;
		Instance().consoleFontName_.Set(lpelfe->elfLogFont.lfFaceName);
		return FALSE;
	}

	return TRUE; // find next suitable
}

// Ensure we use proper font (name,size) to allow Unicode and GUI window increase
void WorkerServer::ServerInitFont()
{
	LogFunction(L"ServerInitFont");

	if (!gpConsoleArgs->consoleFontName_.IsEmpty())
		consoleFontName_.Set(gpConsoleArgs->consoleFontName_.GetStr(), LF_FACESIZE - 1);

	// Size and font name. It's required for server mode that conhost is configured
	// to use TrueType (aka Unicode) font. Otherwise it's impossible to read/write Unicode glyphs.
	// And we set small font size to allow increase of GUI window in advance.
	// No sense to do this check is it's already "Lucida Console"
	if (!consoleFontName_.IsEmpty() && consoleFontName_.Compare(DEFAULT_CONSOLE_FONT_NAME) != 0)
	{
		LOGFONT fnt = {0};
		// Is it installed in the system?
		wcscpy_s(fnt.lfFaceName, consoleFontName_.c_str());
		bool ttfFontExists = false;
		HDC hdc = GetDC(nullptr);
		EnumFontFamiliesEx(hdc, &fnt, reinterpret_cast<FONTENUMPROCW>(FontEnumProc), reinterpret_cast<LPARAM>(&ttfFontExists), 0);
		DeleteDC(hdc);
		// if ttfFontExists is true, consoleFontName_ could be updated
		if (!ttfFontExists)
			consoleFontName_.Clear(); // fill it with "Lucida Console" below
	}

	if (consoleFontName_.IsEmpty())
	{
		consoleFontName_.Set(DEFAULT_CONSOLE_FONT_NAME);
		consoleFontWidth_ = 3; consoleFontHeight_ = 5;
	}

	if (consoleFontHeight_ < 5)
	{
		consoleFontHeight_ = 5;
	}

	if (consoleFontWidth_ == 0 && consoleFontHeight_ == 0)
	{
		consoleFontWidth_ = 3; consoleFontHeight_ = 5;
	}
	else if (consoleFontWidth_ == 0)
	{
		consoleFontWidth_ = consoleFontHeight_ * 2 / 3;
	}
	else if (consoleFontHeight_ == 0)
	{
		consoleFontHeight_ = consoleFontWidth_ * 3 / 2;
	}

	if (consoleFontHeight_ < 5 || consoleFontWidth_ < 3)
	{
		consoleFontWidth_ = 3; consoleFontHeight_ = 5;
	}

	if (gState.attachMode_ && gState.noCreateProcess_ && this->RootProcessId() && gpConsoleArgs->attachFromFar_)
	{
		// It's expected to be attach from Far Manager. Let's set console font via plugin.
		wchar_t szPipeName[128];
		swprintf_c(szPipeName, CEPLUGINPIPENAME, L".", this->RootProcessId());
		CESERVER_REQ In;  // NOLINT(cppcoreguidelines-pro-type-member-init)
		ExecutePrepareCmd(&In, CMD_SET_CON_FONT, sizeof(CESERVER_REQ_HDR) + sizeof(CESERVER_REQ_SETFONT));
		In.Font.cbSize = sizeof(In.Font);
		In.Font.inSizeX = consoleFontWidth_;
		In.Font.inSizeY = consoleFontHeight_;
		wcscpy_s(In.Font.sFontName, consoleFontName_.c_str());
		CESERVER_REQ *pPlgOut = ExecuteCmd(szPipeName, &In, 500, gState.realConWnd_);

		if (pPlgOut) ExecuteFreeResult(pPlgOut);
	}
	else if ((!gState.alienMode_ || IsWin6()) && !gpStartEnv->bIsReactOS)
	{
		if (gpLogSize)
			LogSize(nullptr, 0, ":SetConsoleFontSizeTo.before");

		#ifdef _DEBUG
		if (consoleFontHeight_ >= 10)
			g_IgnoreSetLargeFont = true;
		#endif

		SetConsoleFontSizeTo(gState.realConWnd_, consoleFontHeight_, consoleFontWidth_, consoleFontName_, gnDefTextColors, gnDefPopupColors);

		if (gpLogSize)
		{
			int curSizeY = -1, curSizeX = -1;
			wchar_t sFontName[LF_FACESIZE] = L"";
			if (apiGetConsoleFontSize(ghConOut, curSizeY, curSizeX, sFontName) && curSizeY && curSizeX)
			{
				char szLogInfo[128];
				sprintf_c(szLogInfo, "Console font size H=%i W=%i N=", curSizeY, curSizeX);
				int nLen = lstrlenA(szLogInfo);
				WideCharToMultiByte(CP_UTF8, 0, sFontName, -1, szLogInfo+nLen, countof(szLogInfo)-nLen, nullptr, nullptr);
				LogFunction(szLogInfo);
			}
		}

		if (gpLogSize) LogSize(nullptr, 0, ":SetConsoleFontSizeTo.after");
	}
}

void WorkerServer::WaitForServerActivated(DWORD anServerPID, HANDLE ahServer, DWORD nTimeout /*= 30000*/)
{
	if (!gpSrv || !gpSrv->pConsoleMap || !gpSrv->pConsoleMap->IsValid())
	{
		_ASSERTE(FALSE && "ConsoleMap was not initialized!");
		Sleep(nTimeout);
		return;
	}

	HWND hDcWnd = nullptr;
	DWORD nStartTick = GetTickCount(), nDelta = 0, nWait = STILL_ACTIVE, nSrvPID = 0, nExitCode = STILL_ACTIVE;
	while (nDelta <= nTimeout)
	{
		nWait = WaitForSingleObject(ahServer, 100);
		if (nWait == WAIT_OBJECT_0)
		{
			// Server was terminated unexpectedly?
			if (!GetExitCodeProcess(ahServer, &nExitCode))
				nExitCode = E_UNEXPECTED;
			break;
		}

		nSrvPID = gpSrv->pConsoleMap->Ptr()->nServerPID;
		_ASSERTE(((nSrvPID == 0) || (nSrvPID == anServerPID)) && (nSrvPID != GetCurrentProcessId()));

		hDcWnd = gpSrv->pConsoleMap->Ptr()->hConEmuWndDc;

		// Well, server was started and attached to ConEmu
		if (nSrvPID && hDcWnd)
		{
			break;
		}

		nDelta = (GetTickCount() - nStartTick);
	}

	// Wait a little more, to be sure Server loads process tree
	Sleep(1000);
	UNREFERENCED_PARAMETER(nExitCode);
}

// Вызывается при запуске сервера: (gpStatus->noCreateProcess_ && (gpStatus->attachMode_ || gpWorker->IsDebuggerActive))
int WorkerServer::AttachRootProcess()
{
	LogFunction(L"AttachRootProcess");

	DWORD dwErr = 0;

	_ASSERTE((this->RootProcessHandle() == nullptr || this->RootProcessHandle() == GetCurrentProcess()) && "Must not be opened yet");

	if (!this->IsDebuggerActive() && !gpConsoleArgs->IsAutoAttachAllowed() && !(gState.conemuPid_ || gpConsoleArgs->attachFromFar_))
	{
		PRINT_COMSPEC(L"Console windows is not visible. Attach is unavailable. Exiting...\n", 0);
		gState.DisableAutoConfirmExit();
		//gpSrv->nProcessStartTick = GetTickCount() - 2*CHECK_ROOTSTART_TIMEOUT; // менять nProcessStartTick не нужно. проверка только по флажкам
		#ifdef _DEBUG
		xf_validate();
		xf_dump_chk();
		#endif
		return CERR_RUNNEWCONSOLE;
	}

	// "/AUTOATTACH" must be asynchronous
	if ((gState.attachMode_ & am_Async) || (this->RootProcessId() == 0 && !this->IsDebuggerActive()))
	{
		// Нужно попытаться определить PID корневого процесса.
		// Родительским может быть cmd (comspec, запущенный из FAR)
		DWORD dwParentPID = 0, dwFarPID = 0;
		DWORD dwServerPID = 0; // Вдруг в этой консоли уже есть сервер?
		_ASSERTE(!this->IsDebuggerActive());

		if (gpWorker->Processes().nProcessCount >= 2 && !this->IsDebuggerActive())
		{
			//TODO: Reuse MToolHelp.h
			HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);

			if (hSnap != INVALID_HANDLE_VALUE)
			{
				PROCESSENTRY32 prc = {sizeof(PROCESSENTRY32)};

				if (Process32First(hSnap, &prc))
				{
					do
					{
						for (UINT i = 0; i < gpWorker->Processes().nProcessCount; i++)
						{
							if (prc.th32ProcessID != gnSelfPID
							        && prc.th32ProcessID == gpWorker->Processes().pnProcesses[i])
							{
								if (lstrcmpiW(prc.szExeFile, L"conemuc.exe")==0
								        /*|| lstrcmpiW(prc.szExeFile, L"conemuc64.exe")==0*/)
								{
									CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_ATTACH2GUI, 0);
									CESERVER_REQ* pOut = ExecuteSrvCmd(prc.th32ProcessID, pIn, gState.realConWnd_);

									if (pOut) dwServerPID = prc.th32ProcessID;

									ExecuteFreeResult(pIn); ExecuteFreeResult(pOut);

									// Если команда успешно выполнена - выходим
									if (dwServerPID)
										break;
								}

								if (!dwFarPID && IsFarExe(prc.szExeFile))
								{
									dwFarPID = prc.th32ProcessID;
								}

								if (!dwParentPID)
									dwParentPID = prc.th32ProcessID;
							}
						}

						// Если уже выполнили команду в сервере - выходим, перебор больше не нужен
						if (dwServerPID)
							break;
					}
					while (Process32Next(hSnap, &prc));
				}

				CloseHandle(hSnap);

				if (dwFarPID) dwParentPID = dwFarPID;
			}
		}

		if (dwServerPID)
		{
			AllowSetForegroundWindow(dwServerPID);
			PRINT_COMSPEC(L"Server was already started. PID=%i. Exiting...\n", dwServerPID);
			gState.DisableAutoConfirmExit(); // server already exists?
			// no need to change nProcessStartTick. check is done by flags only
			//gpSrv->nProcessStartTick = GetTickCount() - 2*CHECK_ROOTSTART_TIMEOUT;
			#ifdef _DEBUG
			xf_validate();
			xf_dump_chk();
			#endif
			return CERR_RUNNEWCONSOLE;
		}

		if (!dwParentPID)
		{
			_printf("Attach to GUI was requested, but there is no console processes:\n", 0, GetCommandLineW()); //-V576
			_ASSERTE(FALSE);
			return CERR_CARGUMENT;
		}

		// Нужно открыть HANDLE корневого процесса
		this->SetRootProcessHandle(OpenProcess(PROCESS_QUERY_INFORMATION|SYNCHRONIZE, FALSE, dwParentPID));

		if (!this->RootProcessHandle())
		{
			dwErr = GetLastError();
			wchar_t* lpMsgBuf = nullptr;
			FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, dwErr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&lpMsgBuf, 0, nullptr);
			_printf("Can't open process (%i) handle, ErrCode=0x%08X, Description:\n", //-V576
			        dwParentPID, dwErr, (lpMsgBuf == nullptr) ? L"<Unknown error>" : lpMsgBuf);

			if (lpMsgBuf) LocalFree(lpMsgBuf);
			SetLastError(dwErr);

			return CERR_CREATEPROCESS;
		}

		this->SetRootProcessId(dwParentPID);

		const int nParentBitness = GetProcessBits(this->RootProcessId(), this->RootProcessHandle());

		// Запустить вторую копию ConEmuC НЕМОДАЛЬНО!
		wchar_t szSelf[MAX_PATH+1];
		wchar_t szCommand[MAX_PATH+100];

		if (!GetModuleFileName(nullptr, szSelf, countof(szSelf)))
		{
			dwErr = GetLastError();
			_printf("GetModuleFileName failed, ErrCode=0x%08X\n", dwErr);
			SetLastError(dwErr);
			return CERR_CREATEPROCESS;
		}

		if (nParentBitness && (nParentBitness != WIN3264TEST(32,64)))
		{
			wchar_t* pszName = (wchar_t*)PointToName(szSelf);
			*pszName = 0;
			wcscat_c(szSelf, (nParentBitness==32) ? L"ConEmuC.exe" : L"ConEmuC64.exe");
		}

		//if (wcschr(pszSelf, L' '))
		//{
		//	*(--pszSelf) = L'"';
		//	lstrcatW(pszSelf, L"\"");
		//}

		wchar_t szGuiWnd[32];
		if (gpConsoleArgs->requestNewGuiWnd_)
			wcscpy_c(szGuiWnd, L"/GHWND=NEW");
		else if (gState.hGuiWnd)
			swprintf_c(szGuiWnd, L"/GHWND=%08X", (DWORD)(DWORD_PTR)gState.hGuiWnd);
		else
			szGuiWnd[0] = 0;

		swprintf_c(szCommand, L"\"%s\" %s /ATTACH /PID=%u", szSelf, szGuiWnd, dwParentPID);

		PROCESS_INFORMATION pi; memset(&pi, 0, sizeof(pi));
		STARTUPINFOW si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
		PRINT_COMSPEC(L"Starting modeless:\n%s\n", pszSelf);
		// CREATE_NEW_PROCESS_GROUP - низя, перестает работать Ctrl-C
		// Это запуск нового сервера в этой консоли. В сервер хуки ставить не нужно
		BOOL lbRc = createProcess(TRUE, nullptr, szCommand, nullptr,nullptr, TRUE,
		                           NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
		dwErr = GetLastError();

		if (!lbRc)
		{
			PrintExecuteError(szCommand, dwErr);
			SetLastError(dwErr);
			return CERR_CREATEPROCESS;
		}

		//delete psNewCmd; psNewCmd = nullptr;
		AllowSetForegroundWindow(pi.dwProcessId);
		PRINT_COMSPEC(L"Modeless server was started. PID=%i. Exiting...\n", pi.dwProcessId);

		WaitForServerActivated(pi.dwProcessId, pi.hProcess, 30000);

		SafeCloseHandle(pi.hProcess); SafeCloseHandle(pi.hThread);
		gState.DisableAutoConfirmExit(); // server was started by other process to avoid bat file locking
		// no need to change nProcessStartTick. check is done by flags only
		//gpSrv->nProcessStartTick = GetTickCount() - 2*CHECK_ROOTSTART_TIMEOUT;
		#ifdef _DEBUG
		xf_validate();
		xf_dump_chk();
		#endif
		return CERR_RUNNEWCONSOLE;
	}
	else
	{
		const int iAttachRc = AttachRootProcessHandle();
		if (iAttachRc != 0)
			return iAttachRc;
	}

	return 0; // OK
}

int WorkerServer::ServerInitCheckExisting(bool abAlternative)
{
	LogFunction(L"ServerInitCheckExisting");

	int iRc = 0;
	CESERVER_CONSOLE_MAPPING_HDR test = {};

	BOOL lbExist = LoadSrvMapping(gState.realConWnd_, test);
	_ASSERTE(!lbExist || (test.ComSpec.ConEmuExeDir[0] && test.ComSpec.ConEmuBaseDir[0]));

	if (!abAlternative)
	{
		_ASSERTE(gState.runMode_==RunMode::Server);
		// Основной сервер! Мэппинг консоли по идее создан еще быть не должен!
		// Это должно быть ошибка - попытка запуска второго сервера в той же консоли!
		if (lbExist)
		{
			CESERVER_REQ_HDR In; ExecutePrepareCmd(&In, CECMD_ALIVE, sizeof(CESERVER_REQ_HDR));
			CESERVER_REQ* pOut = ExecuteSrvCmd(test.nServerPID, (CESERVER_REQ*)&In, nullptr);
			if (pOut)
			{
				_ASSERTE(test.nServerPID == 0);
				ExecuteFreeResult(pOut);
				wchar_t szErr[127];
				msprintf(szErr, countof(szErr), L"\nServer (PID=%u) already exist in console! Current PID=%u\n", test.nServerPID, GetCurrentProcessId());
				_wprintf(szErr);
				iRc = CERR_SERVER_ALREADY_EXISTS;
				goto wrap;
			}

			// Старый сервер умер, запустился новый? нужна какая-то дополнительная инициализация?
			_ASSERTE(test.nServerPID == 0 && "Server already exists");
		}
	}
	else
	{
		_ASSERTE(gState.runMode_==RunMode::AltServer);
		// По идее, в консоли должен быть _живой_ сервер.
		_ASSERTE(lbExist && test.nServerPID != 0);
		if (test.nServerPID == 0)
		{
			iRc = CERR_MAINSRV_NOT_FOUND;
			goto wrap;
		}
		else
		{
			this->dwMainServerPID = test.nServerPID;
			this->hMainServer = OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION, FALSE, test.nServerPID);
		}
	}

wrap:
	return iRc;
}

void WorkerServer::ServerInitConsoleSize(bool allowUseCurrent, CONSOLE_SCREEN_BUFFER_INFO* pSbiOut /*= nullptr*/)
{
	LogFunction(L"ServerInitConsoleSize");

	HANDLE hOut = static_cast<HANDLE>(ghConOut);

	if (allowUseCurrent && gcrVisibleSize.X && gcrVisibleSize.Y)
	{
		// could fail if the font is still too large
		SetConsoleSize(gnBufferHeight, gcrVisibleSize, SMALL_RECT{}, ":ServerInit.SetFromArg");

		if (pSbiOut)
		{
			if (!GetConsoleScreenBufferInfo(hOut, pSbiOut))
			{
				_ASSERTE(FALSE && "GetConsoleScreenBufferInfo failed");
			}
		}
	}
	else
	{
		CONSOLE_SCREEN_BUFFER_INFO lsbi = {}; // we need to know real values

		if (!GetConsoleScreenBufferInfo(hOut, &lsbi))
		{
			_ASSERTE(FALSE && "GetConsoleScreenBufferInfo failed");
		}
		else
		{
			gcrVisibleSize.X = lsbi.srWindow.Right - lsbi.srWindow.Left + 1;
			gcrVisibleSize.Y = lsbi.srWindow.Bottom - lsbi.srWindow.Top + 1;
			gnBufferHeight = (lsbi.dwSize.Y == gcrVisibleSize.Y) ? 0 : lsbi.dwSize.Y;
			gnBufferWidth = (lsbi.dwSize.X == gcrVisibleSize.X) ? 0 : lsbi.dwSize.X;

			PreConsoleSize(gcrVisibleSize);
			gpSrv->crReqSizeNewSize = gcrVisibleSize;
			_ASSERTE(gpSrv->crReqSizeNewSize.X!=0);

			if (pSbiOut)
			{
				*pSbiOut = lsbi;
			}
		}
	}
}

int WorkerServer::ServerInitAttach2Gui()
{
	LogFunction(L"ServerInitAttach2Gui");

	int iRc = 0;

	// Нить Refresh НЕ должна быть запущена, иначе в мэппинг могут попасть данные из консоли
	// ДО того, как отработает ресайз (тот размер, который указал установить GUI при аттаче)
	_ASSERTE(this->dwRefreshThread == 0);
	HWND hDcWnd = nullptr;

	while (true)
	{
		hDcWnd = Attach2Gui(ATTACH2GUI_TIMEOUT);

		if (hDcWnd)
			break; // OK

		wchar_t szTitle[128];
		swprintf_c(szTitle, WIN3264TEST(L"ConEmuC",L"ConEmuC64") L" PID=%u", GetCurrentProcessId());
		if (MessageBox(nullptr, L"Available ConEmu GUI window not found!", szTitle,
		              MB_RETRYCANCEL|MB_SYSTEMMODAL|MB_ICONQUESTION) != IDRETRY)
			break; // Reject
	}

	// 090719 попробуем в сервере это делать всегда. Нужно передать в GUI - TID нити ввода
	//// Если это НЕ новая консоль (-new_console) и не /ATTACH уже существующей консоли
	//if (!gpStatus->noCreateProcess_)
	//	SendStarted();

	if (!hDcWnd)
	{
		//_printf("Available ConEmu GUI window not found!\n"); -- don't put rubbish to console
		gbInShutdown = TRUE;
		gState.DisableAutoConfirmExit();
		iRc = CERR_ATTACHFAILED; goto wrap;
	}

wrap:
	return iRc;
}

// Дернуть ConEmu, чтобы он отдал HWND окна отрисовки
// (!gpStatus->attachMode_ && !gpWorker->IsDebuggerActive)
int WorkerServer::ServerInitGuiTab()
{
	LogFunction(L"ServerInitGuiTab");

	int iRc = CERR_ATTACH_NO_GUIWND;
	DWORD nWaitRc = 99;
	HWND hGuiWnd = FindConEmuByPID();

	if (hGuiWnd == nullptr)
	{
		if (gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer)
		{
			// Если запускается сервер - то он должен смочь найти окно ConEmu в которое его просят
			_ASSERTEX((hGuiWnd!=nullptr));
			_ASSERTE(iRc == CERR_ATTACH_NO_GUIWND);
			goto wrap;
		}
		else
		{
			_ASSERTEX(gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer);
		}
	}
	else
	{
		_ASSERTE(gState.realConWnd_!=nullptr);

		CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_SRVSTARTSTOP, sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_SRVSTARTSTOP));
		if (!pIn)
		{
			_ASSERTEX(pIn);
			iRc = CERR_NOTENOUGHMEM1;
			goto wrap;
		}
		pIn->SrvStartStop.Started = srv_Started; // сервер запущен
		pIn->SrvStartStop.hConWnd = gState.realConWnd_;
		// Сразу передать текущий KeyboardLayout
		IsKeyboardLayoutChanged(pIn->SrvStartStop.dwKeybLayout);

		if (TryConnect2Gui(hGuiWnd, 0, pIn))
		{
			iRc = 0;
		}
		else
		{
			ASSERTE(FALSE && "TryConnect2Gui failed");
		}

		ExecuteFreeResult(pIn);
	}

	if (gpSrv->hConEmuGuiAttached)
	{
		DEBUGTEST(DWORD t1 = GetTickCount());

		nWaitRc = WaitForSingleObject(gpSrv->hConEmuGuiAttached, 500);

		#ifdef _DEBUG
		DWORD t2 = GetTickCount(), tDur = t2-t1;
		if (tDur > GUIATTACHEVENT_TIMEOUT)
		{
			_ASSERTE((tDur <= GUIATTACHEVENT_TIMEOUT) && "GUI tab creation take more than 250ms");
		}
		#endif
	}

	CheckConEmuHwnd();
wrap:
	return iRc;
}

bool WorkerServer::AltServerWasStarted(DWORD nPID, HANDLE hAltServer, bool forceThaw)
{
	wchar_t szFnArg[200];
	swprintf_c(szFnArg, L"AltServerWasStarted PID=%u H=x%p ForceThaw=%s ",
		nPID, hAltServer, forceThaw ? L"true" : L"false");
	if (gpLogSize)
	{
		PROCESSENTRY32 AltSrv;
		if (GetProcessInfo(nPID, &AltSrv))
		{
			int iLen = lstrlen(szFnArg);
			lstrcpyn(szFnArg+iLen, PointToName(AltSrv.szExeFile), countof(szFnArg)-iLen);
		}
	}
	LogFunction(szFnArg);

	_ASSERTE(nPID!=0);

	if (hAltServer == nullptr)
	{
		hAltServer = OpenProcess(MY_PROCESS_ALL_ACCESS, FALSE, nPID);
		if (hAltServer == nullptr)
		{
			hAltServer = OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION, FALSE, nPID);
			if (hAltServer == nullptr)
			{
				return false;
			}
		}
	}

	if (this->dwAltServerPID && (this->dwAltServerPID != nPID))
	{
		// Остановить старый (текущий) сервер
		CESERVER_REQ* pFreezeIn = ExecuteNewCmd(CECMD_FREEZEALTSRV, sizeof(CESERVER_REQ_HDR)+2*sizeof(DWORD));
		if (pFreezeIn)
		{
			pFreezeIn->dwData[0] = 1;
			pFreezeIn->dwData[1] = nPID;
			CESERVER_REQ* pFreezeOut = ExecuteSrvCmd(this->dwAltServerPID, pFreezeIn, gState.realConWnd_);
			ExecuteFreeResult(pFreezeIn);
			ExecuteFreeResult(pFreezeOut);
		}

		// Если для nPID не было назначено "предыдущего" альт.сервера
		if (!this->AltServers.Get(nPID, nullptr))
		{
			// нужно сохранить параметры этого предыдущего (пусть даже и пустые)
			AltServerInfo info = {nPID};
			info.hPrev = this->hAltServer; // may be nullptr
			info.nPrevPID = this->dwAltServerPID; // may be 0
			this->AltServers.Set(nPID, info);
		}
	}


	// Перевести нить монитора в режим ожидания завершения AltServer, инициализировать this->dwAltServerPID, this->hAltServer

	//if (this->hAltServer && (this->hAltServer != hAltServer))
	//{
	//	this->dwAltServerPID = 0;
	//	SafeCloseHandle(this->hAltServer);
	//}

	this->hAltServer = hAltServer;
	this->dwAltServerPID = nPID;

	if (this->hAltServerChanged && (GetCurrentThreadId() != this->dwRefreshThread))
	{
		// В RefreshThread ожидание хоть и небольшое (100мс), но лучше передернуть
		SetEvent(this->hAltServerChanged);
	}


	if (forceThaw)
	{
		// Отпустить новый сервер (который раньше замораживался)
		CESERVER_REQ* pFreezeIn = ExecuteNewCmd(CECMD_FREEZEALTSRV, sizeof(CESERVER_REQ_HDR)+2*sizeof(DWORD));
		if (pFreezeIn)
		{
			pFreezeIn->dwData[0] = 0;
			pFreezeIn->dwData[1] = 0;
			CESERVER_REQ* pFreezeOut = ExecuteSrvCmd(this->dwAltServerPID, pFreezeIn, gState.realConWnd_);
			ExecuteFreeResult(pFreezeIn);
			ExecuteFreeResult(pFreezeOut);
		}
	}

	return (hAltServer != nullptr);
}

void WorkerServer::OnAltServerChanged(const int nStep, const StartStopType nStarted, const DWORD nAltServerPID, CESERVER_REQ_STARTSTOP* pStartStop, AltServerStartStop& AS)
{
	if (nStep == 1)
	{
		if (nStarted == sst_AltServerStart)
		{
			// Перевести нить монитора в режим ожидания завершения AltServer, инициализировать gpSrv->dwAltServerPID, gpSrv->hAltServer
			AS.nAltServerWasStarted = nAltServerPID;
			if (pStartStop)
				AS.hAltServerWasStarted = (HANDLE)(DWORD_PTR)pStartStop->hServerProcessHandle;
			AS.AltServerChanged = true;
		}
		else
		{
			AS.bPrevFound = AltServers.Get(nAltServerPID, &AS.info, true/*Remove*/);

			// Сначала проверяем, не текущий ли альт.сервер закрывается
			if (this->dwAltServerPID && (nAltServerPID == this->dwAltServerPID))
			{
				// Поскольку текущий сервер завершается - то сразу сбросим PID (его морозить уже не нужно)
				AS.nAltServerWasStopped = nAltServerPID;
				this->dwAltServerPID = 0;
				// Переключаемся на "старый" (если был)
				if (AS.bPrevFound && AS.info.nPrevPID)
				{
					// _ASSERTE могут приводить к ошибкам блокировки gpWorker->Processes().csProc в других потоках. Но ассертов быть не должно )
					_ASSERTE(AS.info.hPrev!=NULL);
					// Перевести нить монитора в обычный режим, закрыть gpSrv->hAltServer
					// Активировать альтернативный сервер (повторно), отпустить его нити чтения
					AS.AltServerChanged = true;
					AS.nAltServerWasStarted = AS.info.nPrevPID;
					AS.hAltServerWasStarted = AS.info.hPrev;
					AS.ForceThawAltServer = true;
				}
				else
				{
					// _ASSERTE могут приводить к ошибкам блокировки gpWorker->Processes().csProc в других потоках. Но ассертов быть не должно )
					_ASSERTE(AS.info.hPrev==NULL);
					AS.AltServerChanged = true;
				}
			}
			else
			{
				// _ASSERTE могут приводить к ошибкам блокировки gpWorker->Processes().csProc в других потоках. Но ассертов быть не должно )
				_ASSERTE(((nAltServerPID == this->dwAltServerPID) || !this->dwAltServerPID || ((nStarted != sst_AltServerStop) && (nAltServerPID != this->dwAltServerPID) && !AS.bPrevFound))
					&& "Expected active alt.server!");
			}
		}
	}
	else if (nStep == 2)
	{
		if (AS.AltServerChanged)
		{
			if (AS.nAltServerWasStarted)
			{
				WorkerServer::Instance().AltServerWasStarted(AS.nAltServerWasStarted, AS.hAltServerWasStarted, AS.ForceThawAltServer);
			}
			else if (AS.nCurAltServerPID && (nAltServerPID == AS.nCurAltServerPID))
			{
				if (this->hAltServerChanged)
				{
					// Чтобы не подраться между потоками - закрывать хэндл только в RefreshThread
					this->hCloseAltServer = this->hAltServer;
					this->dwAltServerPID = 0;
					this->hAltServer = nullptr;
					// В RefreshThread ожидание хоть и небольшое (100мс), но лучше передернуть
					SetEvent(this->hAltServerChanged);
				}
				else
				{
					this->dwAltServerPID = 0;
					SafeCloseHandle(this->hAltServer);
					_ASSERTE(this->hAltServerChanged!=NULL);
				}
			}

			if (!gState.conemuWnd_ || !IsWindow(gState.conemuWnd_))
			{
				_ASSERTE((gState.conemuWnd_==NULL) && "ConEmu GUI was terminated? Invalid gState.conemuWnd_");
			}
			else
			{
				CESERVER_REQ *pGuiIn = NULL, *pGuiOut = NULL;
				int nSize = sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_STARTSTOP);
				pGuiIn = ExecuteNewCmd(CECMD_CMDSTARTSTOP, nSize);

				if (!pGuiIn)
				{
					_ASSERTE(pGuiIn!=NULL && "Memory allocation failed");
				}
				else
				{
					if (pStartStop)
						pGuiIn->StartStop = *pStartStop;
					pGuiIn->StartStop.dwPID = AS.nAltServerWasStarted ? AS.nAltServerWasStarted : AS.nAltServerWasStopped;
					pGuiIn->StartStop.hServerProcessHandle = nullptr; // для GUI смысла не имеет
					pGuiIn->StartStop.nStarted = AS.nAltServerWasStarted ? sst_AltServerStart : sst_AltServerStop;
					if (pGuiIn->StartStop.nStarted == sst_AltServerStop)
					{
						// Если это был последний процесс в консоли, то главный сервер тоже закрывается
						// Переоткрывать пайпы в ConEmu нельзя
						pGuiIn->StartStop.bMainServerClosing = gbQuit || (WaitForSingleObject(ghExitQueryEvent,0) == WAIT_OBJECT_0);
					}

					pGuiOut = ExecuteGuiCmd(gState.realConWnd_, pGuiIn, gState.realConWnd_);

					_ASSERTE(pGuiOut!=NULL && "Can not switch GUI to alt server?"); // успешное выполнение?
					ExecuteFreeResult(pGuiOut);
					ExecuteFreeResult(pGuiIn);
				}
			}
		}
	}
}

void WorkerServer::OnFarDetached(const DWORD farPid)
{
	// После детача в фаре команда (например dir) схлопнется, чтобы
	// консоль неожиданно не закрылась...
	gState.autoDisableConfirmExit_ = FALSE;
	gState.alwaysConfirmExit_ = TRUE;

	MSectionLock CS; CS.Lock(Processes().csProc);
	const UINT nPrevCount = Processes().nProcessCount;
	_ASSERTE(farPid != 0);

	const BOOL lbChanged = Processes().ProcessRemove(farPid, nPrevCount, CS);

	MSectionLock CsAlt;
	CsAlt.Lock(gpSrv->csAltSrv, TRUE, 1000);

	AltServerStartStop AS = {};
	AS.nCurAltServerPID = this->dwAltServerPID;

	OnAltServerChanged(1, sst_AltServerStop, farPid, nullptr, AS);

	// ***
	if (lbChanged)
		Processes().ProcessCountChanged(TRUE, nPrevCount, CS);
	CS.Unlock();
	// ***

	// После Unlock-а, зовем функцию
	if (AS.AltServerChanged)
	{
		OnAltServerChanged(2, sst_AltServerStop, farPid, nullptr, AS);
	}

	// Обновить мэппинг
	UpdateConsoleMapHeader(L"CECMD_FARDETACHED");

	CsAlt.Unlock();
}

// ReSharper disable once CppMemberFunctionMayBeStatic
DWORD WorkerServer::SetOemCpThread(LPVOID lpParameter)
{
	const UINT cp = static_cast<UINT>(reinterpret_cast<DWORD_PTR>(lpParameter));
	SetConsoleCP(cp);
	SetConsoleOutputCP(cp);
	return 0;
}

const char* WorkerServer::GetCurrentThreadLabel() const
{
	const auto dwId = GetCurrentThreadId();
	if (dwId == gdwMainThreadId)
		return "MainThread";
	if (gpSrv->CmdServer.IsPipeThread(dwId))
		// ReSharper disable once StringLiteralTypo
		return "ServThread";
	if (dwId == this->dwRefreshThread)
		// ReSharper disable once StringLiteralTypo
		return "RefrThread";
	//#ifdef USE_WINEVENT_SRV
	//else if (dwId == gpSrv->dwWinEventThread)
	//	pszThread = " WinEventThread";
	//#endif
	if (gpSrv->InputServer.IsPipeThread(dwId))
		// ReSharper disable once StringLiteralTypo
		return "InptThread";
	if (gpSrv->DataServer.IsPipeThread(dwId))
		return "DataThread";

	return WorkerBase::GetCurrentThreadLabel();
}


void WorkerServer::ServerInitEnvVars()
{
	LogFunction(L"ServerInitEnvVars");

	wchar_t szValue[32];
	//DWORD nRc;
	//nRc = GetEnvironmentVariable(ENV_CONEMU_HOOKS, szValue, countof(szValue));
	//if ((nRc == 0) && (GetLastError() == ERROR_ENVVAR_NOT_FOUND)) ...

	SetEnvironmentVariable(ENV_CONEMU_HOOKS_W, ENV_CONEMU_HOOKS_ENABLED);

	if (gState.runMode_ == RunMode::Server)
	{
		swprintf_c(szValue, L"%u", GetCurrentProcessId());
		SetEnvironmentVariable(ENV_CONEMUSERVERPID_VAR_W, szValue);
	}

	if (gpSrv && (gpSrv->guiSettings.cbSize == sizeof(gpSrv->guiSettings)))
	{
		SetConEmuFolders(gpSrv->guiSettings.ComSpec.ConEmuExeDir, gpSrv->guiSettings.ComSpec.ConEmuBaseDir);

		// Не будем ставить сами, эту переменную заполняет Gui при своем запуске
		// соответственно, переменная наследуется серверами
		//SetEnvironmentVariableW(L"ConEmuArgs", pInfo->sConEmuArgs);

		//wchar_t szHWND[16]; swprintf_c(szHWND, L"0x%08X", gpSrv->guiSettings.hGuiWnd.u);
		//SetEnvironmentVariable(ENV_CONEMUHWND_VAR_W, szHWND);
		SetConEmuWindows(gpSrv->guiSettings.hGuiWnd, gState.conemuWndDC_, gState.conemuWndBack_);

		#ifdef _DEBUG
		bool bNewConArg = ((gpSrv->guiSettings.Flags & CECF_ProcessNewCon) != 0);
		//SetEnvironmentVariable(ENV_CONEMU_HOOKS, bNewConArg ? ENV_CONEMU_HOOKS_ENABLED : ENV_CONEMU_HOOKS_NOARGS);
		#endif

		bool bAnsi = ((gpSrv->guiSettings.Flags & CECF_ProcessAnsi) != 0);
		SetEnvironmentVariable(ENV_CONEMUANSI_VAR_W, bAnsi ? L"ON" : L"OFF");

		if (bAnsi)
		{
			wchar_t szInfo[40];
			HANDLE hOut = (HANDLE)ghConOut;
			CONSOLE_SCREEN_BUFFER_INFO lsbi = {{0,0}}; // интересует реальное положение дел
			GetConsoleScreenBufferInfo(hOut, &lsbi);

			msprintf(szInfo, countof(szInfo), L"%ux%u (%ux%u)", lsbi.dwSize.X, lsbi.dwSize.Y, lsbi.srWindow.Right-lsbi.srWindow.Left+1, lsbi.srWindow.Bottom-lsbi.srWindow.Top+1);
			SetEnvironmentVariable(ENV_ANSICON_VAR_W, szInfo);

			//static SHORT Con2Ansi[16] = {0,4,2,6,1,5,3,7,8|0,8|4,8|2,8|6,8|1,8|5,8|3,8|7};
			//DWORD clrDefault = Con2Ansi[CONFORECOLOR(lsbi.wAttributes)]
			//	| (Con2Ansi[CONBACKCOLOR(lsbi.wAttributes)] << 4);
			msprintf(szInfo, countof(szInfo), L"%X", LOBYTE(lsbi.wAttributes));
			SetEnvironmentVariable(ENV_ANSICON_DEF_VAR_W, szInfo);
		}
		else
		{
			SetEnvironmentVariable(ENV_ANSICON_VAR_W, nullptr);
			SetEnvironmentVariable(ENV_ANSICON_DEF_VAR_W, nullptr);
		}
		SetEnvironmentVariable(ENV_ANSICON_VER_VAR_W, nullptr);
	}
	else
	{
		//_ASSERTE(gpSrv && (gpSrv->guiSettings.cbSize == sizeof(gpSrv->guiSettings)));
	}
}


// Создать необходимые события и нити
int WorkerServer::Init()
{
	LogFunction(L"ServerInit");
	_ASSERTE(gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer || gState.runMode_ == RunMode::AutoAttach)

	int iRc = 0;
	DWORD dwErr = 0;
	wchar_t szName[64];
	DWORD nTick = GetTickCount();

	if (gbDumpServerInitStatus) { _printf("ServerInit: started"); }
	#define DumpInitStatus(fmt) if (gbDumpServerInitStatus) { DWORD nCurTick = GetTickCount(); _printf(" - %ums" fmt, (nCurTick-nTick)); nTick = nCurTick; }

	if (gState.runMode_ == RunMode::Server)
	{
		_ASSERTE(!(gState.attachMode_ & am_Async));

		this->dwMainServerPID = GetCurrentProcessId();
		this->hMainServer = GetCurrentProcess();

		_ASSERTE(gState.runMode_==RunMode::Server);

		if (gState.isDBCS_)
		{
			UINT nOemCP = GetOEMCP();
			UINT nConCP = GetConsoleOutputCP();
			if (nConCP != nOemCP)
			{
				DumpInitStatus("\nServerInit: CreateThread(SetOemCpProc)");
				DWORD nTID;
				HANDLE h = apiCreateThread(
					SetOemCpThreadProc, reinterpret_cast<LPVOID>(static_cast<DWORD_PTR>(nOemCP)),
					&nTID, "SetOemCpProc");
				if (h && (h != INVALID_HANDLE_VALUE))
				{
					DWORD nWait = WaitForSingleObject(h, 5000);
					if (nWait != WAIT_OBJECT_0)
					{
						_ASSERTE(nWait==WAIT_OBJECT_0 && "SetOemCpProc HUNGS!!!");
						apiTerminateThread(h, 100);
					}
					CloseHandle(h);
				}
			}
		}

		//// Set up some environment variables
		//DumpInitStatus("\nServerInit: ServerInitEnvVars");
		//ServerInitEnvVars();
	}


	gpSrv->osv.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetOsVersionInformational(&gpSrv->osv);

	// Смысла вроде не имеет, без ожидания "очистки" очереди винда "проглатывает мышиные события
	// Межпроцессный семафор не помогает, оставил пока только в качестве заглушки
	//InitializeConsoleInputSemaphore();

	if (gState.runMode_ == RunMode::Server)
	{
		if (!gpConsoleArgs->confirmExitParm_)
		{
			gState.alwaysConfirmExit_ = TRUE; gState.autoDisableConfirmExit_ = TRUE;
		}
	}
	else
	{
		_ASSERTE(gState.runMode_==RunMode::AltServer || gState.runMode_==RunMode::AutoAttach);
		// По идее, включены быть не должны, но убедимся
		_ASSERTE(!gState.alwaysConfirmExit_);
		gState.autoDisableConfirmExit_ = FALSE; gState.alwaysConfirmExit_ = FALSE;
	}

	// Remember RealConsole font at the startup moment
	if (gState.runMode_ == RunMode::AltServer)
	{
		apiInitConsoleFontSize(ghConOut);
	}

	// Шрифт в консоли нужно менять в самом начале, иначе могут быть проблемы с установкой размера консоли
	if ((gState.runMode_ == RunMode::Server) && !this->IsDebuggerActive() && !gState.noCreateProcess_)
		//&& (!gpStatus->noCreateProcess_ || (gpStatus->attachMode_ && gpStatus->noCreateProcess_ && gpWorker->RootProcessId()))
		//)
	{
		//DumpInitStatus("\nServerInit: ServerInitFont");
		ServerInitFont();

		//bool bMovedBottom = false;

		// Minimized окошко нужно развернуть!
		// Не помню уже зачем, возможно, что-то с мышкой связано...
		if (IsIconic(gState.realConWnd_))
		{
			//WINDOWPLACEMENT wplGui = {sizeof(wplGui)};
			//// По идее, HWND гуя нам уже должен быть известен (передан аргументом)
			//if (gState.hGuiWnd)
			//	GetWindowPlacement(gState.hGuiWnd, &wplGui);
			//SendMessage(gState.realConWnd, WM_SYSCOMMAND, SC_RESTORE, 0);
			WINDOWPLACEMENT wplCon = {sizeof(wplCon)};
			GetWindowPlacement(gState.realConWnd_, &wplCon);
			//wplCon.showCmd = SW_SHOWNA;
			////RECT rc = {wplGui.rcNormalPosition.left+3,wplGui.rcNormalPosition.top+3,wplCon.rcNormalPosition.right-wplCon.rcNormalPosition.left,wplCon.rcNormalPosition.bottom-wplCon.rcNormalPosition.top};
			//// т.к. ниже все равно делается "SetWindowPos(gState.realConWnd, nullptr, 0, 0, ..." - можем задвинуть подальше
			//RECT rc = {-30000,-30000,-30000+wplCon.rcNormalPosition.right-wplCon.rcNormalPosition.left,-30000+wplCon.rcNormalPosition.bottom-wplCon.rcNormalPosition.top};
			//wplCon.rcNormalPosition = rc;
			////SetWindowPos(gState.realConWnd, HWND_BOTTOM, 0, 0, 0,0, SWP_NOSIZE|SWP_NOMOVE);
			//SetWindowPlacement(gState.realConWnd, &wplCon);
			wplCon.showCmd = SW_RESTORE;
			SetWindowPlacement(gState.realConWnd_, &wplCon);
			//bMovedBottom = true;
		}

		if (!gbVisibleOnStartup && IsWindowVisible(gState.realConWnd_))
		{
			//DumpInitStatus("\nServerInit: Hiding console");
			apiShowWindow(gState.realConWnd_, SW_HIDE);
			//if (bMovedBottom)
			//{
			//	SetWindowPos(gState.realConWnd, HWND_TOP, 0, 0, 0,0, SWP_NOSIZE|SWP_NOMOVE);
			//}
		}

		//DumpInitStatus("\nServerInit: Set console window pos {0,0}");
		// -- чтобы на некоторых системах не возникала проблема с позиционированием -> {0,0}
		// Issue 274: Окно реальной консоли позиционируется в неудобном месте
		SetWindowPos(gState.realConWnd_, nullptr, 0, 0, 0,0, SWP_NOSIZE|SWP_NOZORDER);
	}

	// Не будем, наверное. OnTop попытается поставить сервер,
	// при показе консоли через Ctrl+Win+Alt+Space
	// Но вот если консоль уже видима, и это "/root", тогда
	// попытаемся поставить окну консоли флаг "OnTop"
	if (!gState.noCreateProcess_ && !gState.isWine_)
	{
		//if (!gbVisibleOnStartup)
		//	apiShowWindow(gState.realConWnd, SW_HIDE);
		//DumpInitStatus("\nServerInit: Set console window TOP_MOST");
		SetWindowPos(gState.realConWnd_, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
	}
	//if (!gbVisibleOnStartup && IsWindowVisible(gState.realConWnd))
	//{
	//	apiShowWindow(gState.realConWnd, SW_HIDE);
	//}

	// Подготовить буфер для длинного вывода
	// RunMode::RM_SERVER - создать и считать текущее содержимое консоли
	// RunMode::RM_ALTSERVER - только создать (по факту - выполняется открытие созданного в RunMode::RM_SERVER)
	if (gState.runMode_==RunMode::Server || gState.runMode_==RunMode::AltServer)
	{
		DumpInitStatus("\nServerInit: CmdOutputStore");
		CmdOutputStore(true/*abCreateOnly*/);
	}
	else
	{
		_ASSERTE(gState.runMode_==RunMode::AutoAttach);
	}

	//2009-08-27 Перенес снизу
	if (!gpSrv->hConEmuGuiAttached && (!this->IsDebugProcess() || gState.conemuPid_ || gState.hGuiWnd))
	{
		wchar_t szTempName[MAX_PATH];
		swprintf_c(szTempName, CEGUIRCONSTARTED, LODWORD(gState.realConWnd_)); //-V205

		gpSrv->hConEmuGuiAttached = CreateEvent(gpLocalSecurity, TRUE, FALSE, szTempName);

		_ASSERTE(gpSrv->hConEmuGuiAttached!=nullptr);
		//if (gpSrv->hConEmuGuiAttached) ResetEvent(gpSrv->hConEmuGuiAttached); -- низя. может уже быть создано/установлено в GUI
	}

	// Было 10, чтобы не перенапрягать консоль при ее быстром обновлении ("dir /s" и т.п.)
	gpSrv->nMaxFPS = 100;

	#ifdef _DEBUG
	if (ghFarInExecuteEvent)
		SetEvent(ghFarInExecuteEvent);
	#endif

	if (gState.conemuWndDC_ == nullptr)
	{
		// в AltServer режиме HWND уже должен быть известен
		_ASSERTE((gState.runMode_ == RunMode::Server) || (gState.runMode_ == RunMode::AutoAttach) || (gState.conemuWndDC_ != nullptr));
	}
	else
	{
		DumpInitStatus("\nServerInit: ServerInitCheckExisting");
		iRc = ServerInitCheckExisting((gState.runMode_ != RunMode::Server));
		if (iRc != 0)
			goto wrap;
	}

	// Создать MapFile для заголовка (СРАЗУ!!!) и буфера для чтения и сравнения
	//DumpInitStatus("\nServerInit: CreateMapHeader");
	iRc = CreateMapHeader();

	if (iRc != 0)
		goto wrap;

	_ASSERTE((gState.conemuWndDC_==nullptr) || (this->pColorerMapping!=nullptr));

	_ASSERTE(gpSrv->pConsole != nullptr);
	if ((gState.runMode_ == RunMode::AltServer) && IsCrashHandlerAllowed())
	{
		SetupCreateDumpOnException();
	}

	gpSrv->csAltSrv = new MSection();
	gpSrv->nMainTimerElapse = 10;
	gpSrv->TopLeft.Reset(); // блокировка прокрутки не включена
	// Инициализация имен пайпов
	swprintf_c(gpSrv->szPipename, CESERVERPIPENAME, L".", gnSelfPID);
	swprintf_c(gpSrv->szInputname, CESERVERINPUTNAME, L".", gnSelfPID);
	swprintf_c(gpSrv->szQueryname, CESERVERQUERYNAME, L".", gnSelfPID);
	swprintf_c(gpSrv->szGetDataPipe, CESERVERREADNAME, L".", gnSelfPID);
	swprintf_c(gpSrv->szDataReadyEvent, CEDATAREADYEVENT, gnSelfPID);
	MCHKHEAP;

	if (gpWorker->Processes().pnProcesses.empty() || gpWorker->Processes().pnProcessesGet.empty() || gpWorker->Processes().pnProcessesCopy.empty())
	{
		_printf("Can't allocate %i DWORDS!\n", gpWorker->Processes().nMaxProcesses);
		iRc = CERR_NOTENOUGHMEM1; goto wrap;
	}

	//DumpInitStatus("\nServerInit: CheckProcessCount");
	gpWorker->Processes().CheckProcessCount(TRUE); // Сначала добавит себя

	// в принципе, серверный режим может быть вызван из фара, чтобы подцепиться к GUI.
	// больше двух процессов в консоли вполне может быть, например, еще не отвалился
	// предыдущий conemuc.exe, из которого этот запущен немодально.
	_ASSERTE(this->IsDebuggerActive() || (gpWorker->Processes().nProcessCount<=2) || ((gpWorker->Processes().nProcessCount>2) && gState.attachMode_ && this->RootProcessId()));

	// Запустить нить обработки событий (клавиатура, мышь, и пр.)
	if (gState.runMode_ == RunMode::Server)
	{
		gpSrv->hInputEvent = CreateEvent(nullptr,FALSE,FALSE,nullptr);
		gpSrv->hInputWasRead = CreateEvent(nullptr,FALSE,FALSE,nullptr);

		if (gpSrv->hInputEvent && gpSrv->hInputWasRead)
		{
			DumpInitStatus("\nServerInit: CreateThread(InputThread)");
			gpSrv->hInputThread = apiCreateThread(InputThread, nullptr, &gpSrv->dwInputThread, "InputThread");

		}

		if (gpSrv->hInputEvent == nullptr || gpSrv->hInputWasRead == nullptr || gpSrv->hInputThread == nullptr)
		{
			dwErr = GetLastError();
			_printf("CreateThread(InputThread) failed, ErrCode=0x%08X\n", dwErr);
			iRc = CERR_CREATEINPUTTHREAD; goto wrap;
		}

		SetThreadPriority(gpSrv->hInputThread, THREAD_PRIORITY_ABOVE_NORMAL);

		DumpInitStatus("\nServerInit: InputQueue.Initialize");
		gpSrv->InputQueue.Initialize(CE_MAX_INPUT_QUEUE_BUFFER, gpSrv->hInputEvent);

		// Запустить пайп обработки событий (клавиатура, мышь, и пр.)
		DumpInitStatus("\nServerInit: InputServerStart");
		if (!InputServerStart())
		{
			dwErr = GetLastError();
			_printf("CreateThread(InputServerStart) failed, ErrCode=0x%08X\n", dwErr);
			iRc = CERR_CREATEINPUTTHREAD; goto wrap;
		}
	}

	// Пайп возврата содержимого консоли
	if ((gState.runMode_ == RunMode::Server) || (gState.runMode_ == RunMode::AltServer))
	{
		DumpInitStatus("\nServerInit: DataServerStart");
		if (!DataServerStart())
		{
			dwErr = GetLastError();
			_printf("CreateThread(DataServerStart) failed, ErrCode=0x%08X\n", dwErr);
			iRc = CERR_CREATEINPUTTHREAD; goto wrap;
		}
	}


	// Проверка. Для дебаггера должен быть RunMode::RM_UNDEFINED!
	// И он не должен быть "сервером" - работает как обычное приложение!
	_ASSERTE(!(this->IsDebuggerActive() || this->IsDebugProcess() || this->IsDebugProcessTree()));

	if (!gState.attachMode_ && !this->IsDebuggerActive())
	{
		DumpInitStatus("\nServerInit: ServerInitGuiTab");
		iRc = ServerInitGuiTab();
		if (iRc != 0)
			goto wrap;
	}

	if ((gState.runMode_ == RunMode::Server) && (gState.attachMode_ & ~am_Async) && !(gState.attachMode_ & am_Async))
	{
		DumpInitStatus("\nServerInit: ServerInitAttach2Gui");
		iRc = ServerInitAttach2Gui();
		if (iRc != 0)
			goto wrap;
	}

	// Ensure the console has proper size before further steps (echo for example)
	ServerInitConsoleSize(gbParmVisibleSize || gbParmBufSize);

	// Ensure that "set" commands in the command line will override ConEmu's default environment (settings page)
	// This function also process all other "configuration" and "output" commands like 'echo', 'type', 'chcp' etc.
	ApplyProcessSetEnvCmd();

	// Если "корневой" процесс консоли запущен не нами (аттач или дебаг)
	// то нужно к нему "подцепиться" (открыть HANDLE процесса)
	if (gState.noCreateProcess_ && (gState.attachMode_ || (this->IsDebuggerActive() && (this->RootProcessHandle() == nullptr))))
	{
		DumpInitStatus("\nServerInit: AttachRootProcess");
		iRc = AttachRootProcess();
		if (iRc != 0)
			goto wrap;

		if (gState.attachMode_ & am_Async)
		{
			_ASSERTE(FALSE && "Not expected to be here!");
			iRc = CERR_CARGUMENT;
			goto wrap;
		}
	}


	gpSrv->hAllowInputEvent = CreateEvent(nullptr, TRUE, TRUE, nullptr);

	if (!gpSrv->hAllowInputEvent) SetEvent(gpSrv->hAllowInputEvent);



	_ASSERTE(gpSrv->pConsole!=nullptr);
	//gpSrv->pConsole->hdr.bConsoleActive = TRUE;
	//gpSrv->pConsole->hdr.bThawRefreshThread = TRUE;

	//// Minimized окошко нужно развернуть!
	//if (IsIconic(gState.realConWnd))
	//{
	//	WINDOWPLACEMENT wplCon = {sizeof(wplCon)};
	//	GetWindowPlacement(gState.realConWnd, &wplCon);
	//	wplCon.showCmd = SW_RESTORE;
	//	SetWindowPlacement(gState.realConWnd, &wplCon);
	//}

	// Сразу получить текущее состояние консоли
	DumpInitStatus("\nServerInit: ReloadFullConsoleInfo");
	ReloadFullConsoleInfo(TRUE);

	//DumpInitStatus("\nServerInit: Creating events");

	//
	gpSrv->hRefreshEvent = CreateEvent(nullptr,FALSE,FALSE,nullptr);
	if (!gpSrv->hRefreshEvent)
	{
		dwErr = GetLastError();
		_printf("CreateEvent(hRefreshEvent) failed, ErrCode=0x%08X\n", dwErr);
		iRc = CERR_REFRESHEVENT; goto wrap;
	}

	_ASSERTE(gnSelfPID == GetCurrentProcessId());
	swprintf_c(szName, CEFARWRITECMTEVENT, gnSelfPID);
	gpSrv->hFarCommitEvent = CreateEvent(nullptr,FALSE,FALSE,szName);
	if (!gpSrv->hFarCommitEvent)
	{
		dwErr = GetLastError();
		_ASSERTE(gpSrv->hFarCommitEvent!=nullptr);
		_printf("CreateEvent(hFarCommitEvent) failed, ErrCode=0x%08X\n", dwErr);
		iRc = CERR_REFRESHEVENT; goto wrap;
	}

	swprintf_c(szName, CECURSORCHANGEEVENT, gnSelfPID);
	gpSrv->hCursorChangeEvent = CreateEvent(nullptr,FALSE,FALSE,szName);
	if (!gpSrv->hCursorChangeEvent)
	{
		dwErr = GetLastError();
		_ASSERTE(gpSrv->hCursorChangeEvent!=nullptr);
		_printf("CreateEvent(hCursorChangeEvent) failed, ErrCode=0x%08X\n", dwErr);
		iRc = CERR_REFRESHEVENT; goto wrap;
	}

	gpSrv->hRefreshDoneEvent = CreateEvent(nullptr,FALSE,FALSE,nullptr);
	if (!gpSrv->hRefreshDoneEvent)
	{
		dwErr = GetLastError();
		_printf("CreateEvent(hRefreshDoneEvent) failed, ErrCode=0x%08X\n", dwErr);
		iRc = CERR_REFRESHEVENT; goto wrap;
	}

	gpSrv->hDataReadyEvent = CreateEvent(gpLocalSecurity,FALSE,FALSE,gpSrv->szDataReadyEvent);
	if (!gpSrv->hDataReadyEvent)
	{
		dwErr = GetLastError();
		_printf("CreateEvent(hDataReadyEvent) failed, ErrCode=0x%08X\n", dwErr);
		iRc = CERR_REFRESHEVENT; goto wrap;
	}

	// !! Event может ожидаться в нескольких нитях !!
	gpSrv->hReqSizeChanged = CreateEvent(nullptr,TRUE,FALSE,nullptr);
	if (!gpSrv->hReqSizeChanged)
	{
		dwErr = GetLastError();
		_printf("CreateEvent(hReqSizeChanged) failed, ErrCode=0x%08X\n", dwErr);
		iRc = CERR_REFRESHEVENT; goto wrap;
	}
	gpSrv->pReqSizeSection = new MSection();

	if ((gState.runMode_ == RunMode::Server) && gState.attachMode_)
	{
		// External attach to running process, required ConEmuHk is not loaded yet
		if (!gpConsoleArgs->alternativeAttach_ && gState.noCreateProcess_ && gState.alienMode_ && !gpConsoleArgs->doNotInjectConEmuHk_)
		{
			if (this->RootProcessId())
			{
				DumpInitStatus("\nServerInit: InjectRemote (gpStatus->alienMode_)");
				CINFILTRATE_EXIT_CODES iRemote = InjectRemote(this->RootProcessId());
				if (iRemote != CIR_OK/*0*/ && iRemote != CIR_AlreadyInjected/*1*/)
				{
					_printf("ServerInit warning: InjectRemote PID=%u failed, Code=%i\n", this->RootProcessId(), iRemote);
				}
			}
			else
			{
				_printf("ServerInit warning: gpWorker->RootProcessId()==0\n", 0);
			}
		}
	}

	// Запустить нить наблюдения за консолью
	DumpInitStatus("\nServerInit: CreateThread(RefreshThread)");
	this->hRefreshThread = apiCreateThread(RefreshThreadProc, nullptr, &this->dwRefreshThread, "RefreshThread");
	if (this->hRefreshThread == nullptr)
	{
		dwErr = GetLastError();
		_printf("CreateThread(RefreshThread) failed, ErrCode=0x%08X\n", dwErr);
		iRc = CERR_CREATEREFRESHTHREAD; goto wrap;
	}

	//#ifdef USE_WINEVENT_SRV
	////gpSrv->nMsgHookEnableDisable = RegisterWindowMessage(L"ConEmuC::HookEnableDisable");
	//// The client thread that calls SetWinEventHook must have a message loop in order to receive events.");
	//gpSrv->hWinEventThread = apiCreateThread(nullptr, 0, WinEventThread, nullptr, 0, &gpSrv->dwWinEventThread);
	//if (gpSrv->hWinEventThread == nullptr)
	//{
	//	dwErr = GetLastError();
	//	_printf("CreateThread(WinEventThread) failed, ErrCode=0x%08X\n", dwErr);
	//	iRc = CERR_WINEVENTTHREAD; goto wrap;
	//}
	//#endif

	// Запустить пайп обработки команд
	DumpInitStatus("\nServerInit: CmdServerStart");
	if (!CmdServerStart())
	{
		dwErr = GetLastError();
		_printf("CreateThread(CmdServerStart) failed, ErrCode=0x%08X\n", dwErr);
		iRc = CERR_CREATESERVERTHREAD; goto wrap;
	}

	// Set up some environment variables
	DumpInitStatus("\nServerInit: ServerInitEnvVars");
	ServerInitEnvVars();

	// Пометить мэппинг, как готовый к отдаче данных
	gpSrv->pConsole->hdr.bDataReady = TRUE;

	UpdateConsoleMapHeader(L"ServerInit");

	// Set console title in server mode
	if (gState.runMode_ == RunMode::Server)
	{
		UpdateConsoleTitle();
	}

	DumpInitStatus("\nServerInit: SendStarted");
	SendStarted();

	CheckConEmuHwnd();

	// Обновить переменные окружения и мэппинг консоли (по ConEmuGuiMapping)
	// т.к. в момент CreateMapHeader ghConEmu еще был неизвестен
	ReloadGuiSettings(nullptr);

	// Если мы аттачим существующее GUI окошко
	if (gState.noCreateProcess_ && gState.attachMode_ && this->RootProcessGui())
	{
		// Его нужно дернуть, чтобы инициализировать цикл аттача во вкладку ConEmu
		CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_ATTACHGUIAPP, sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_ATTACHGUIAPP));
		_ASSERTE(LODWORD(this->RootProcessGui())!=0xCCCCCCCC);
		_ASSERTE(IsWindow(gState.conemuWnd_));
		_ASSERTE(IsWindow(gState.conemuWndDC_));
		_ASSERTE(IsWindow(gState.conemuWndBack_));
		_ASSERTE(IsWindow(this->RootProcessGui()));
		_ASSERTE(this->dwMainServerPID && (this->dwMainServerPID==GetCurrentProcessId()));
		pIn->AttachGuiApp.nServerPID = this->dwMainServerPID;
		pIn->AttachGuiApp.hConEmuWnd = gState.conemuWnd_;
		pIn->AttachGuiApp.hConEmuDc = gState.conemuWndDC_;
		pIn->AttachGuiApp.hConEmuBack = gState.conemuWndBack_;
		pIn->AttachGuiApp.hAppWindow = this->RootProcessGui();
		pIn->AttachGuiApp.hSrvConWnd = gState.realConWnd_;
		wchar_t szPipe[MAX_PATH];
		_ASSERTE(this->RootProcessId()!=0);
		swprintf_c(szPipe, CEHOOKSPIPENAME, L".", this->RootProcessId());
		DumpInitStatus("\nServerInit: CECMD_ATTACHGUIAPP");
		CESERVER_REQ* pOut = ExecuteCmd(szPipe, pIn, GUIATTACH_TIMEOUT, gState.realConWnd_);
		if (!pOut
			|| (pOut->hdr.cbSize < (sizeof(CESERVER_REQ_HDR)+sizeof(DWORD)))
			|| (pOut->dwData[0] != LODWORD(this->RootProcessGui())))
		{
			iRc = CERR_ATTACH_NO_GUIWND;
		}
		ExecuteFreeResult(pOut);
		ExecuteFreeResult(pIn);
		if (iRc != 0)
			goto wrap;
	}

	_ASSERTE(gnSelfPID == GetCurrentProcessId());
	swprintf_c(szName, CESRVSTARTEDEVENT, gnSelfPID);
	// Event мог быть создан и ранее (в Far-плагине, например)
	this->hServerStartedEvent = CreateEvent(LocalSecurity(), TRUE, FALSE, szName);
	if (!this->hServerStartedEvent)
	{
		_ASSERTE(this->hServerStartedEvent!=nullptr);
	}
	else
	{
		SetEvent(this->hServerStartedEvent);
	}
wrap:
	DumpInitStatus("\nServerInit: finished\n");
	#undef DumpInitStatus
	return iRc;
}

// Завершить все нити и закрыть дескрипторы
void WorkerServer::Done(const int exitCode, const bool reportShutdown /*= false*/)
{
	LogFunction(L"ServerDone");
	_ASSERTE(gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer || gState.runMode_ == RunMode::AutoAttach)

	gbQuit = true;
	gbTerminateOnExit = FALSE;

	#ifdef _DEBUG
	// Проверить, не вылезло ли Assert-ов в других потоках
	MyAssertShutdown();
	#endif

	// На всякий случай - выставим событие
	if (ghExitQueryEvent)
	{
		_ASSERTE(gbTerminateOnCtrlBreak==FALSE);
		if (!nExitQueryPlace) nExitQueryPlace = 10+(nExitPlaceStep);

		SetTerminateEvent(ste_ServerDone);
	}

	if (ghQuitEvent) SetEvent(ghQuitEvent);

	if (gState.conemuWnd_ && IsWindow(gState.conemuWnd_))
	{
		if (gpSrv->pConsole && gpSrv->pConsoleMap)
		{
			gpSrv->pConsole->hdr.nServerInShutdown = GetTickCount();

			UpdateConsoleMapHeader(L"ServerDone");
		}

		#ifdef _DEBUG
		UINT nCurProcCount = std::min(gpWorker->Processes().nProcessCount, (UINT)gpWorker->Processes().pnProcesses.size());
		DWORD nCurProcs[20];
		memmove(nCurProcs, &gpWorker->Processes().pnProcesses[0], std::min<DWORD>(nCurProcCount, 20) * sizeof(DWORD));
		_ASSERTE(nCurProcCount <= 1);
		#endif

		wchar_t szServerPipe[MAX_PATH];
		swprintf_c(szServerPipe, CEGUIPIPENAME, L".", LODWORD(gState.conemuWnd_)); //-V205

		CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_SRVSTARTSTOP, sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_SRVSTARTSTOP));
		if (pIn)
		{
			pIn->SrvStartStop.Started = srv_Stopped/*101*/;
			pIn->SrvStartStop.hConWnd = gState.realConWnd_;
			pIn->SrvStartStop.nShellExitCode = gnExitCode;
			// Здесь dwKeybLayout уже не интересен

			// Послать в GUI уведомление, что сервер закрывается
			/*CallNamedPipe(szServerPipe, &In, In.hdr.cbSize, &Out, sizeof(Out), &dwRead, 1000);*/
			// 131017 При закрытии не успевает отработать. Серверу нужно дождаться ответа как обычно
			CESERVER_REQ* pOut = ExecuteCmd(szServerPipe, pIn, 1000, gState.realConWnd_, FALSE/*bAsyncNoResult*/);

			ExecuteFreeResult(pIn);
			ExecuteFreeResult(pOut);
		}
	}

	// Our debugger is running?
	if (this->IsDebuggerActive())
	{
		// pfnDebugActiveProcessStop is useless, because
		// 1. pfnDebugSetProcessKillOnExit was called already
		// 2. we can debug more than a one process

		//this->IsDebuggerActive = FALSE;
	}



	if (gpSrv->hInputThread)
	{
		// Подождем немножко, пока нить сама завершится
		WARNING("Не завершается?");

		if (WaitForSingleObject(gpSrv->hInputThread, 500) != WAIT_OBJECT_0)
		{
			gbTerminateOnExit = gpSrv->bInputTermination = TRUE;
			#ifdef _DEBUG
				// Проверить, не вылезло ли Assert-ов в других потоках
				MyAssertShutdown();
			#endif

			#ifndef __GNUC__
			#pragma warning( push )
			#pragma warning( disable : 6258 )
			#endif
			apiTerminateThread(gpSrv->hInputThread, 100);    // раз корректно не хочет...
			#ifndef __GNUC__
			#pragma warning( pop )
			#endif
		}

		SafeCloseHandle(gpSrv->hInputThread);
		//gpSrv->dwInputThread = 0; -- не будем чистить ИД, Для истории
	}

	// Пайп консольного ввода
	if (gpSrv)
		gpSrv->InputServer.StopPipeServer(false, gpSrv->bServerForcedTermination);

	//SafeCloseHandle(gpSrv->hInputPipe);
	SafeCloseHandle(gpSrv->hInputEvent);
	SafeCloseHandle(gpSrv->hInputWasRead);

	if (gpSrv)
		gpSrv->DataServer.StopPipeServer(false, gpSrv->bServerForcedTermination);

	if (gpSrv)
		gpSrv->CmdServer.StopPipeServer(false, gpSrv->bServerForcedTermination);

	if (this->hRefreshThread)
	{
		if (WaitForSingleObject(this->hRefreshThread, 250)!=WAIT_OBJECT_0)
		{
			_ASSERT(FALSE);
			gbTerminateOnExit = true;
			this->bRefreshTermination = true;
			#ifdef _DEBUG
				// Проверить, не вылезло ли Assert-ов в других потоках
				MyAssertShutdown();
			#endif

			#ifndef __GNUC__
			#pragma warning( push )
			#pragma warning( disable : 6258 )
			#endif
			apiTerminateThread(this->hRefreshThread, 100);
			#ifndef __GNUC__
			#pragma warning( pop )
			#endif
		}

		SafeCloseHandle(this->hRefreshThread);
	}

	SafeCloseHandle(this->hAltServerChanged);

	SafeCloseHandle(gpSrv->hRefreshEvent);

	SafeCloseHandle(gpSrv->hFarCommitEvent);

	SafeCloseHandle(gpSrv->hCursorChangeEvent);

	SafeCloseHandle(gpSrv->hRefreshDoneEvent);

	SafeCloseHandle(gpSrv->hDataReadyEvent);

	SafeCloseHandle(this->hServerStartedEvent);

	//if (gpSrv->hChangingSize) {
	//    SafeCloseHandle(gpSrv->hChangingSize);
	//}
	// Отключить все хуки
	//gpSrv->bWinHookAllow = FALSE; gpSrv->nWinHookMode = 0;
	//HookWinEvents ( -1 );

	SafeDelete(gpSrv->pStoredOutputItem);
	SafeDelete(gpSrv->pStoredOutputHdr);

	SafeFree(gpSrv->pszAliases);

	//if (gpSrv->psChars) { free(gpSrv->psChars); gpSrv->psChars = nullptr; }
	//if (gpSrv->pnAttrs) { free(gpSrv->pnAttrs); gpSrv->pnAttrs = nullptr; }
	//if (gpSrv->ptrLineCmp) { free(gpSrv->ptrLineCmp); gpSrv->ptrLineCmp = nullptr; }
	//Delete Critical Section(&gpSrv->csConBuf);
	//Delete Critical Section(&gpSrv->csChar);
	//Delete Critical Section(&gpSrv->csChangeSize);
	SafeCloseHandle(gpSrv->hAllowInputEvent);
	this->CloseRootProcessHandles();

	SafeDelete(gpSrv->csAltSrv);

	//SafeFree(gpSrv->pInputQueue);
	gpSrv->InputQueue.Release();

	CloseMapHeader();

	//SafeCloseHandle(gpSrv->hColorerMapping);
	SafeDelete(this->pColorerMapping);

	SafeCloseHandle(gpSrv->hReqSizeChanged);
	SafeDelete(gpSrv->pReqSizeSection);

	// Final steps
	WorkerBase::Done(exitCode, reportShutdown);
}

int WorkerServer::ProcessCommandLineArgs()
{
	const int baseRc = WorkerBase::ProcessCommandLineArgs();
	if (baseRc != 0)
		return baseRc;

	LogFunction(L"ParseCommandLine{in-progress-server}");

	// ReSharper disable once CppInitializedValueIsAlwaysRewritten
	int iRc = 0;

	if (gState.runMode_ == RunMode::Undefined || gState.runMode_ == RunMode::AltServer)
	{
		_ASSERTE(gpConsoleArgs->command_.IsEmpty());
		gState.runMode_ = RunMode::AltServer;
		_ASSERTE(!IsCreateDumpOnExceptionInstalled());
		_ASSERTE(gState.attachMode_==am_None);
		if (!(gState.attachMode_ & am_Modes))
			gState.attachMode_ |= am_Simple;
		gState.DisableAutoConfirmExit();
		gState.noCreateProcess_ = TRUE;
		gState.alienMode_ = TRUE;
		gpWorker->SetRootProcessId(GetCurrentProcessId());
		gpWorker->SetRootProcessHandle(GetCurrentProcess());
		//gState.conemuPid_ = ...;

		SafeFree(gpszRunCmd);
		gpszRunCmd = lstrdup(L"");

		CreateColorerHeader();
	}

	if (gState.runMode_ == RunMode::Server)
	{
		_ASSERTE(!gState.noCreateProcess_);
		SetConEmuWorkEnvVar(ghOurModule);
	}

	if (gpConsoleArgs->conemuPid_.exists)
	{
		if ((iRc = ParamConEmuGuiPid()) != 0)
			return iRc;
	}


	return 0;
}

void WorkerServer::EnableProcessMonitor(const bool enable)
{
	if (enable)
	{
		// could be already initialized from cmd_CmdStartStop
		if (!gpWorker->Processes().nProcessStartTick)
			gpWorker->Processes().nProcessStartTick = GetTickCount();
	}
	else
	{
		gpWorker->Processes().nProcessStartTick = 0;
	}
}

// Консоль любит глючить, при попытках запроса более определенного количества ячеек.
// MAX_CONREAD_SIZE подобрано экспериментально
BOOL WorkerServer::MyReadConsoleOutput(HANDLE hOut, CHAR_INFO *pData, COORD& bufSize, SMALL_RECT& rgn)
{
	MSectionLock RCS;
	if (gpSrv->pReqSizeSection && !RCS.Lock(gpSrv->pReqSizeSection, TRUE, 30000))
	{
		_ASSERTE(FALSE);
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;

	}

	return ReadConsoleOutputEx(hOut, pData, bufSize, rgn);
}

// Консоль любит глючить, при попытках запроса более определенного количества ячеек.
// MAX_CONREAD_SIZE подобрано экспериментально
BOOL WorkerServer::MyWriteConsoleOutput(HANDLE hOut, CHAR_INFO *pData, COORD& bufSize, COORD& crBufPos, SMALL_RECT& rgn)
{
	LogFunction(L"MyWriteConsoleOutput");

	BOOL lbRc = FALSE;

	size_t nBufWidth = bufSize.X;
	int nWidth = (rgn.Right - rgn.Left + 1);
	int nHeight = (rgn.Bottom - rgn.Top + 1);
	int nCurSize = nWidth * nHeight;

	_ASSERTE(bufSize.X >= nWidth);
	_ASSERTE(bufSize.Y >= nHeight);
	_ASSERTE(rgn.Top>=0 && rgn.Bottom>=rgn.Top);
	_ASSERTE(rgn.Left>=0 && rgn.Right>=rgn.Left);

	COORD bufCoord = crBufPos;

	if (nCurSize <= MAX_CONREAD_SIZE)
	{
		if (WriteConsoleOutputW(hOut, pData, bufSize, bufCoord, &rgn))
			lbRc = TRUE;
	}

	if (!lbRc)
	{
		// Придется читать построчно

		// Теоретически - можно и блоками, но оверхед очень маленький, а велик
		// шанс обломаться, если консоль "глючит". Поэтому построчно...

		//bufSize.X = TextWidth;
		bufSize.Y = 1;
		bufCoord.X = 0; bufCoord.Y = 0;
		//rgn = this->consoleInfo.sbi.srWindow;

		int Y1 = rgn.Top;
		int Y2 = rgn.Bottom;

		CHAR_INFO* pLine = pData;
		for (int y = Y1; y <= Y2; y++, rgn.Top++, pLine+=nBufWidth)
		{
			rgn.Bottom = rgn.Top;
			lbRc = WriteConsoleOutputW(hOut, pLine, bufSize, bufCoord, &rgn);
		}
	}

	return lbRc;
}

bool WorkerServer::LoadScreenBufferInfo(ScreenBufferInfo& sbi)
{
	// Need to block all requests to output buffer in other threads
	MSectionLockSimple csRead = LockConsoleReaders(LOCK_READOUTPUT_TIMEOUT);

	// ReSharper disable once CppLocalVariableMayBeConst
	HANDLE hOut = ghConOut.GetHandle();

	const auto result = GetConsoleScreenBufferInfo(hOut, &sbi.csbi);
	sbi.crMaxSize = MyGetLargestConsoleWindowSize(hOut);
	return result;
}

MSectionLockSimple WorkerServer::LockConsoleReaders(DWORD anWaitTimeout)
{
	MSectionLockSimple csLock;
	csLock.Lock(&csReadConsoleInfo, anWaitTimeout);
	return csLock;
}

void WorkerServer::ConOutCloseHandle()
{
	if (IsReopenHandleAllowed())
	{
		// Need to block all requests to output buffer in other threads
		MSectionLockSimple csRead = LockConsoleReaders(LOCK_REOPENCONOUT_TIMEOUT);
		if (csRead.IsLocked())
		{
			ghConOut.Close();
		}
	}
}

// В Win7 закрытие дескриптора в ДРУГОМ процессе - закрывает консольный буфер ПОЛНОСТЬЮ!!!
// В итоге, буфер вывода telnet'а схлопывается!
bool WorkerServer::IsReopenHandleAllowed()
{
	// Windows 7 has a bug which makes impossible to utilize ScreenBuffers
	// https://conemu.github.io/en/MicrosoftBugs.html#CorruptedScreenBuffer
	if (IsWin7Eql())
		return false;
	return true;
}

bool WorkerServer::IsCrashHandlerAllowed()
{
	if (gState.runMode_ == RunMode::AltServer)
	{
		// By default, handler is not installed in AltServer
		// gpSet->isConsoleExceptionHandler --> CECF_ConExcHandler
		const bool allowHandler = gpSrv && gpSrv->pConsole && (gpSrv->pConsole->hdr.Flags & CECF_ConExcHandler);
		if (!allowHandler)
			return false; // disabled in ConEmu settings
	}

	return true;
}

bool WorkerServer::CmdOutputOpenMap(CONSOLE_SCREEN_BUFFER_INFO& lsbi, CESERVER_CONSAVE_MAPHDR*& pHdr, CESERVER_CONSAVE_MAP*& pData)
{
	LogFunction(L"CmdOutputOpenMap");

	pHdr = nullptr;
	pData = nullptr;

	// Need to block all requests to output buffer in other threads
	MSectionLockSimple csRead = LockConsoleReaders(LOCK_READOUTPUT_TIMEOUT);

	if (!gpSrv->pStoredOutputHdr)
	{
		gpSrv->pStoredOutputHdr = new MFileMapping<CESERVER_CONSAVE_MAPHDR>;
		gpSrv->pStoredOutputHdr->InitName(CECONOUTPUTNAME, LODWORD(gState.realConWnd_)); //-V205
		if (!(pHdr = gpSrv->pStoredOutputHdr->Create()))
		{
			_ASSERTE(FALSE && "Failed to create mapping: CESERVER_CONSAVE_MAPHDR");
			gpSrv->pStoredOutputHdr->CloseMap();
			return false;
		}

		ExecutePrepareCmd(&pHdr->hdr, 0, sizeof(*pHdr));
	}
	else
	{
		if (!(pHdr = gpSrv->pStoredOutputHdr->Ptr()))
		{
			_ASSERTE(FALSE && "Failed to get mapping Ptr: CESERVER_CONSAVE_MAPHDR");
			gpSrv->pStoredOutputHdr->CloseMap();
			return false;
		}
	}

	if (!lsbi.dwSize.Y)
		lsbi = pHdr->info;

	WARNING("А вот это нужно бы делать в RefreshThread!!!");
	DEBUGSTR(L"--- CmdOutputStore begin\n");

	DWORD cchOneBufferSize = lsbi.dwSize.X * lsbi.dwSize.Y; // Читаем всю консоль целиком!
	DWORD cchMaxBufferSize = std::max<DWORD>(pHdr->MaxCellCount, (lsbi.dwSize.Y * lsbi.dwSize.X));


	bool lbNeedRecreate = false; // требуется новый или больший, или сменился индекс (создан в другом сервере)
	bool lbNeedReopen = (gpSrv->pStoredOutputItem == nullptr);
	// Warning! Мэппинг уже мог быть создан другим сервером.
	if (!pHdr->CurrentIndex || (pHdr->MaxCellCount < cchOneBufferSize))
	{
		pHdr->CurrentIndex++;
		lbNeedRecreate = true;
	}
	int nNewIndex = pHdr->CurrentIndex;

	// Проверить, если мэппинг уже открывался ранее, может его нужно переоткрыть - сменился индекс (создан в другом сервере)
	if (!lbNeedRecreate && gpSrv->pStoredOutputItem)
	{
		if (!gpSrv->pStoredOutputItem->IsValid()
			|| (nNewIndex != gpSrv->pStoredOutputItem->Ptr()->CurrentIndex))
		{
			lbNeedReopen = lbNeedRecreate = true;
		}
	}

	if (lbNeedRecreate
		|| (!gpSrv->pStoredOutputItem)
		|| (pHdr->MaxCellCount < cchOneBufferSize))
	{
		if (!gpSrv->pStoredOutputItem)
		{
			gpSrv->pStoredOutputItem = new MFileMapping<CESERVER_CONSAVE_MAP>;
			_ASSERTE(lbNeedReopen);
			lbNeedReopen = true;
		}

		if (!lbNeedRecreate && pHdr->MaxCellCount)
		{
			_ASSERTE(pHdr->MaxCellCount >= cchOneBufferSize);
			cchMaxBufferSize = pHdr->MaxCellCount;
		}

		if (lbNeedReopen || lbNeedRecreate || !gpSrv->pStoredOutputItem->IsValid())
		{
			LPCWSTR pszName = gpSrv->pStoredOutputItem->InitName(CECONOUTPUTITEMNAME, LODWORD(gState.realConWnd_), nNewIndex); //-V205
			DWORD nMaxSize = sizeof(*pData) + cchMaxBufferSize * sizeof(pData->Data[0]);

			if (!(pData = gpSrv->pStoredOutputItem->Create(nMaxSize)))
			{
				_ASSERTE(FALSE && "Failed to create item mapping: CESERVER_CONSAVE_MAP");
				gpSrv->pStoredOutputItem->CloseMap();
				pHdr->sCurrentMap[0] = 0; // сброс, если был ранее назначен
				return false;
			}

			ExecutePrepareCmd(&pData->hdr, 0, nMaxSize);
			// Save current mapping
			pData->CurrentIndex = nNewIndex;
			pData->MaxCellCount = cchMaxBufferSize;
			pHdr->MaxCellCount = cchMaxBufferSize;
			wcscpy_c(pHdr->sCurrentMap, pszName);

			goto wrap;
		}
	}

	if (!(pData = gpSrv->pStoredOutputItem->Ptr()))
	{
		_ASSERTE(FALSE && "Failed to get item mapping Ptr: CESERVER_CONSAVE_MAP");
		gpSrv->pStoredOutputItem->CloseMap();
		return false;
	}

wrap:
	if (!pData || (pData->hdr.nVersion != CESERVER_REQ_VER) || (pData->hdr.cbSize <= sizeof(CESERVER_CONSAVE_MAP)))
	{
		_ASSERTE(pData && (pData->hdr.nVersion == CESERVER_REQ_VER) && (pData->hdr.cbSize > sizeof(CESERVER_CONSAVE_MAP)));
		gpSrv->pStoredOutputItem->CloseMap();
		return false;
	}

	return (pData != nullptr);
}

// Сохранить данные ВСЕЙ консоли в mapping
void WorkerServer::CmdOutputStore(bool abCreateOnly /*= false*/)
{
	LogFunction(L"CmdOutputStore");

	const bool reopen_allowed = IsReopenHandleAllowed();

	CONSOLE_SCREEN_BUFFER_INFO lsbi = {};
	CESERVER_CONSAVE_MAPHDR* pHdr = nullptr;
	CESERVER_CONSAVE_MAP* pData = nullptr;

	// Need to block all requests to output buffer in other threads
	MSectionLockSimple csRead = LockConsoleReaders(LOCK_READOUTPUT_TIMEOUT);
	LogString(L"csReadConsoleInfo locked");

	if (reopen_allowed)
		ConOutCloseHandle();

	// !!! Нас интересует реальное положение дел в консоли,
	//     а не скорректированное функцией MyGetConsoleScreenBufferInfo
	if (!GetConsoleScreenBufferInfo(ghConOut, &lsbi) || !lsbi.dwSize.Y)
	{
		LogString("--- Skipped, GetConsoleScreenBufferInfo failed");
		return; // Не смогли получить информацию о консоли...
	}
	// just for information
	COORD crMaxSize = MyGetLargestConsoleWindowSize(ghConOut);

	if (!CmdOutputOpenMap(lsbi, pHdr, pData))
	{
		LogString("--- Skipped, CmdOutputOpenMap failed");
		return;
	}

	if (!pHdr || !pData)
	{
		_ASSERTE(pHdr && pData);
		LogString("--- Skipped, invalid map data");
		return;
	}

	if (!pHdr->info.dwSize.Y || !abCreateOnly)
		pHdr->info = lsbi;
	if (!pData->info.dwSize.Y || !abCreateOnly)
		pData->info = lsbi;

	if (!abCreateOnly)
	{
		// now we may read the console data
		COORD BufSize = {lsbi.dwSize.X, lsbi.dwSize.Y};
		SMALL_RECT ReadRect = {0, 0, lsbi.dwSize.X-1, lsbi.dwSize.Y-1};

		// store/update sbi
		pData->info = lsbi;

		LogString("MyReadConsoleOutput");
		pData->Succeeded = MyReadConsoleOutput(ghConOut, pData->Data, BufSize, ReadRect);
	}

	LogString("CmdOutputStore finished");
	DEBUGSTR(L"--- CmdOutputStore end\n");
	UNREFERENCED_PARAMETER(crMaxSize);
}

// abSimpleMode==true  - просто восстановить экран на момент вызова CmdOutputStore
//             ==false - пытаться подгонять строки вывода под текущее состояние
//                       задел на будущее для выполнения команд из Far (без /w), mc, или еще кого.
void WorkerServer::CmdOutputRestore(bool abSimpleMode)
{
	LogFunction(L"CmdOutputRestore");

	const bool reopen_allowed = IsReopenHandleAllowed();

	if (!abSimpleMode)
	{
		//_ASSERTE(FALSE && "Non Simple mode is not supported!");
		WARNING("Переделать/доделать CmdOutputRestore для Far");
		LogString("--- Skipped, only simple mode supported yet");
		return;
	}

	// Need to block all requests to output buffer in other threads
	MSectionLockSimple csRead = LockConsoleReaders(LOCK_READOUTPUT_TIMEOUT);
	LogString(L"csReadConsoleInfo locked");

	// Just in case we change the logic somehow
	if (reopen_allowed)
		ConOutCloseHandle();

	CONSOLE_SCREEN_BUFFER_INFO lsbi = {};
	CESERVER_CONSAVE_MAPHDR* pHdr = nullptr;
	CESERVER_CONSAVE_MAP* pData = nullptr;
	if (!CmdOutputOpenMap(lsbi, pHdr, pData))
	{
		LogString(L"--- Skipped, CmdOutputOpenMap failed");
		return;
	}

	if (lsbi.srWindow.Top > 0)
	{
		_ASSERTE(lsbi.srWindow.Top == 0 && "Upper left corner of window expected");
		wchar_t err_msg[80];
		msprintf(err_msg, std::size(err_msg),
			L"Invalid upper-left corner; sr={%i,%i}-{%i,%i}",
			LogSRectCoords(lsbi.srWindow));
		LogString(err_msg);
		return;
	}

	CHAR_INFO chrFill = {};
	chrFill.Attributes = lsbi.wAttributes;
	chrFill.Char.UnicodeChar = L' ';

	SMALL_RECT rcTop = {0,0, lsbi.dwSize.X-1, lsbi.srWindow.Bottom};
	COORD crMoveTo = {0, lsbi.dwSize.Y - 1 - lsbi.srWindow.Bottom};
	if (!ScrollConsoleScreenBuffer(ghConOut, &rcTop, nullptr, crMoveTo, &chrFill))
	{
		LogString(L"--- Skipped, ScrollConsoleScreenBuffer failed");
		return;
	}

	short h = lsbi.srWindow.Bottom - lsbi.srWindow.Top + 1;
	short w = lsbi.srWindow.Right - lsbi.srWindow.Left + 1;

	if (abSimpleMode)
	{
		crMoveTo.Y = std::min<int>(pData->info.srWindow.Top, std::max<int>(0,lsbi.dwSize.Y-h));
	}

	SMALL_RECT rcBottom = {0, crMoveTo.Y, w - 1, crMoveTo.Y + h - 1};
	SetConsoleWindowInfo(ghConOut, TRUE, &rcBottom);

	COORD crNewPos = {lsbi.dwCursorPosition.X, lsbi.dwCursorPosition.Y + crMoveTo.Y};
	SetConsoleCursorPosition(ghConOut, crNewPos);


	// Восстановить текст скрытой (прокрученной вверх) части консоли
	// Учесть, что ширина консоли могла измениться со времени выполнения предыдущей команды.
	// Сейчас у нас в верхней части консоли может оставаться кусочек предыдущего вывода (восстановил FAR).
	// 1) Этот кусочек нужно считать
	// 2) Скопировать в нижнюю часть консоли (до которой докрутилась предыдущая команда)
	// 3) прокрутить консоль до предыдущей команды (куда мы только что скопировали данные сверху)
	// 4) восстановить оставшуюся часть консоли. Учесть, что фар может
	//    выполнять некоторые команды сам и курсор вообще-то мог несколько уехать...


	WARNING("Попытаться подобрать те строки, которые НЕ нужно выводить в консоль");
	// из-за прокрутки консоли самим фаром, некоторые строки могли уехать вверх.


	CONSOLE_SCREEN_BUFFER_INFO storedSbi = pData->info;
	COORD crOldBufSize = pData->info.dwSize; // Может быть шире или уже чем текущая консоль!
	SMALL_RECT rcWrite = MakeSmallRect(0, 0, std::min<int>(crOldBufSize.X,lsbi.dwSize.X)-1, std::min<int>(crOldBufSize.Y,lsbi.dwSize.Y)-1);
	COORD crBufPos = {0,0};

	if (!abSimpleMode)
	{
		// Что восстанавливать - при выполнении команд из фара - нужно
		// восстановить только область выше первой видимой строки,
		// т.к. видимую область фар восстанавливает сам
		SHORT nStoredHeight = std::min<SHORT>(storedSbi.srWindow.Top, rcBottom.Top);
		if (nStoredHeight < 1)
		{
			// Nothing to restore?
			return;
		}

		rcWrite.Top = rcBottom.Top-nStoredHeight;
		rcWrite.Right = std::min<int>(crOldBufSize.X,lsbi.dwSize.X)-1;
		rcWrite.Bottom = rcBottom.Top-1;

		crBufPos.Y = storedSbi.srWindow.Top-nStoredHeight;
	}
	else
	{
		// А в "простом" режиме - тупо восстановить консоль как она была на момент сохранения!
	}

	MyWriteConsoleOutput(ghConOut, pData->Data, crOldBufSize, crBufPos, rcWrite);

	if (abSimpleMode)
	{
		LogString("SetConsoleTextAttribute");
		SetConsoleTextAttribute(ghConOut, pData->info.wAttributes);
	}

	LogString("CmdOutputRestore finished");
}

void WorkerServer::SetConEmuFolders(LPCWSTR asExeDir, LPCWSTR asBaseDir)
{
	_ASSERTE(asExeDir && *asExeDir!=0 && asBaseDir && *asBaseDir);
	SetEnvironmentVariable(ENV_CONEMUDIR_VAR_W, asExeDir);
	SetEnvironmentVariable(ENV_CONEMUBASEDIR_VAR_W, asBaseDir);
	CEStr BaseShort(GetShortFileNameEx(asBaseDir, false));
	SetEnvironmentVariable(ENV_CONEMUBASEDIRSHORT_VAR_W, BaseShort.IsEmpty() ? asBaseDir : BaseShort.ms_Val);
}

void WorkerServer::CheckConEmuHwnd()
{
	LogFunction(L"CheckConEmuHwnd");

	// #WARNING too many calls during server start?

	DWORD dwGuiThreadId = 0;

	if (this->IsDebuggerActive())
	{
		HWND  hDcWnd = nullptr;
		gState.conemuWnd_ = FindConEmuByPID();

		if (gState.conemuWnd_)
		{
			GetWindowThreadProcessId(gState.conemuWnd_, &gState.conemuPid_);
			// Просто для информации
			hDcWnd = FindWindowEx(gState.conemuWnd_, nullptr, VirtualConsoleClass, nullptr);
		}
		else
		{
			hDcWnd = nullptr;
		}

		UNREFERENCED_PARAMETER(hDcWnd);

		return;
	}

	if (gState.conemuWnd_ == nullptr)
	{
		SendStarted(); // Он и окно проверит, и параметры перешлет и размер консоли скорректирует
	}

	// GUI может еще "висеть" в ожидании или в отладчике, так что пробуем и через Snapshot
	if (gState.conemuWnd_ == nullptr)
	{
		gState.conemuWnd_ = FindConEmuByPID();
	}

	if (gState.conemuWnd_ == nullptr)
	{
		// Если уж ничего не помогло...
		LogFunction(L"GetConEmuHWND");
		gState.conemuWnd_ = GetConEmuHWND(1/*Gui Main window*/);
	}

	if (gState.conemuWnd_)
	{
		if (!gState.conemuWndDC_)
		{
			// gState.conemuWndDC_ по идее уже должен быть получен из GUI через пайпы
			LogFunction(L"Warning, gState.conemuWndDC_ still not initialized");
			_ASSERTE(gState.conemuWndDC_!=nullptr);
			wchar_t szClass[128];
			HWND hBack = nullptr;
			while (!gState.conemuWndDC_)
			{
				hBack = FindWindowEx(gState.conemuWnd_, hBack, VirtualConsoleClassBack, nullptr);
				if (!hBack)
					break;
				if (GetWindowLong(hBack, WindowLongBack_ConWnd) == LOLONG(gState.realConWnd_))
				{
					const HWND2 hDc{(DWORD)GetWindowLong(hBack, WindowLongBack_DCWnd)};
					if (IsWindow(hDc) && GetClassName(hDc, szClass, countof(szClass)
						&& (0 == lstrcmp(szClass, VirtualConsoleClass))))
					{
						SetConEmuWindows(gState.conemuWnd_, hDc, hBack);
						break;
					}
				}
			}
			_ASSERTE(gState.conemuWndDC_!=nullptr);
		}

		// Установить переменную среды с дескриптором окна
		SetConEmuWindows(gState.conemuWnd_, gState.conemuWndDC_, gState.conemuWndBack_);

		//if (hWndFore == gState.realConWnd || hWndFocus == gState.realConWnd)
		//if (hWndFore != gState.conemuWnd_)

		if (GetForegroundWindow() == gState.realConWnd_)
			apiSetForegroundWindow(gState.conemuWnd_); // 2009-09-14 почему-то было было gState.realConWnd ?
	}
	else
	{
		// да и фиг с ним. нас могли просто так, без gui запустить
		//_ASSERTE(gState.conemuWnd_!=nullptr);
	}
}

void WorkerServer::FixConsoleMappingHdr(CESERVER_CONSOLE_MAPPING_HDR *pMap)
{
	pMap->nGuiPID = gState.conemuPid_;
	pMap->hConEmuRoot = gState.conemuWnd_;
	pMap->hConEmuWndDc = gState.conemuWndDC_;
	pMap->hConEmuWndBack = gState.conemuWndBack_;
}

LgsResult WorkerServer::LoadGuiSettingsPtr(ConEmuGuiMapping& guiMapping, const ConEmuGuiMapping* pInfo, const bool needReload, const bool forceCopy, DWORD& rnWrongValue) const
{
	LgsResult liRc = LgsResult::Failed;
	DWORD cbSize = 0;
	bool lbNeedCopy = false;
	bool lbCopied = false;
	wchar_t szLog[80];

	if (!pInfo)
	{
		liRc = LgsResult::MapPtr;
		wcscpy_c(szLog, L"LoadGuiSettings(Failed, MapPtr is null)");
		LogFunction(szLog);
		goto wrap;
	}

	if (forceCopy)
	{
		cbSize = std::min<DWORD>(sizeof(guiMapping), pInfo->cbSize);
		memmove(&guiMapping, pInfo, cbSize);
		gpSrv->guiSettings.cbSize = cbSize;
		lbCopied = true;
	}

	if (pInfo->cbSize >= (size_t)(sizeof(pInfo->nProtocolVersion) + ((LPBYTE)&pInfo->nProtocolVersion) - (LPBYTE)pInfo))
	{
		if (pInfo->nProtocolVersion != CESERVER_REQ_VER)
		{
			liRc = LgsResult::WrongVersion;
			rnWrongValue = pInfo->nProtocolVersion;
			wcscpy_c(szLog, L"LoadGuiSettings(Failed, MapPtr is null)");
			swprintf_c(szLog, L"LoadGuiSettings(Failed, Version=%u, Required=%u)", rnWrongValue, (DWORD)CESERVER_REQ_VER);
			LogFunction(szLog);
			goto wrap;
		}
	}

	if (pInfo->cbSize != sizeof(ConEmuGuiMapping))
	{
		liRc = LgsResult::WrongSize;
		rnWrongValue = pInfo->cbSize;
		swprintf_c(szLog, L"LoadGuiSettings(Failed, cbSize=%u, Required=%u)", pInfo->cbSize, (DWORD)sizeof(ConEmuGuiMapping));
		LogFunction(szLog);
		goto wrap;
	}

	lbNeedCopy = needReload
		|| (gpSrv->guiSettingsChangeNum != pInfo->nChangeNum)
		|| (guiMapping.bGuiActive != pInfo->bGuiActive)
		;

	if (lbNeedCopy)
	{
		wcscpy_c(szLog, L"LoadGuiSettings(Changed)");
		LogFunction(szLog);
		if (!lbCopied)
			memmove(&guiMapping, pInfo, pInfo->cbSize);
		_ASSERTE(guiMapping.ComSpec.ConEmuExeDir[0]!=0 && guiMapping.ComSpec.ConEmuBaseDir[0]!=0);
		liRc = LgsResult::Updated;
	}
	else if (guiMapping.dwActiveTick != pInfo->dwActiveTick)
	{
		// But active consoles list may be changed
		if (!lbCopied)
			memmove(guiMapping.Consoles, pInfo->Consoles, sizeof(guiMapping.Consoles));
		liRc = LgsResult::ActiveChanged;
	}
	else
	{
		liRc = LgsResult::Succeeded;
	}

wrap:
	return liRc;
}

LgsResult WorkerServer::LoadGuiSettings(ConEmuGuiMapping& guiMapping, DWORD& rnWrongValue) const
{
	LgsResult liRc = LgsResult::Failed;
	bool lbNeedReload = false;
	// ReSharper disable once CppJoinDeclarationAndAssignment
	DWORD dwGuiThreadId = 0, dwGuiProcessId = 0;
	// ReSharper disable once CppLocalVariableMayBeConst
	HWND hGuiWnd = gState.conemuWnd_ ? gState.conemuWnd_ : gState.hGuiWnd;
	const ConEmuGuiMapping* pInfo = nullptr;

	if (!hGuiWnd || !IsWindow(hGuiWnd))
	{
		LogFunction(L"LoadGuiSettings(Invalid window)");
		goto wrap;
	}

	if (!gpSrv->pGuiInfoMap || (gpSrv->hGuiInfoMapWnd != hGuiWnd))
	{
		lbNeedReload = true;
	}

	if (lbNeedReload)
	{
		LogFunction(L"LoadGuiSettings(Opening)");

		dwGuiThreadId = GetWindowThreadProcessId(hGuiWnd, &dwGuiProcessId);
		if (!dwGuiThreadId)
		{
			_ASSERTE(dwGuiProcessId);
			LogFunction(L"LoadGuiSettings(Failed, dwGuiThreadId==0)");
			goto wrap;
		}

		if (!gpSrv->pGuiInfoMap)
			gpSrv->pGuiInfoMap = new MFileMapping<ConEmuGuiMapping>;
		else
			gpSrv->pGuiInfoMap->CloseMap();

		gpSrv->pGuiInfoMap->InitName(CEGUIINFOMAPNAME, dwGuiProcessId);
		pInfo = gpSrv->pGuiInfoMap->Open();

		if (pInfo)
		{
			gpSrv->hGuiInfoMapWnd = hGuiWnd;
		}
	}
	else
	{
		pInfo = gpSrv->pGuiInfoMap->Ptr();
	}

	liRc = LoadGuiSettingsPtr(guiMapping, pInfo, lbNeedReload, false, rnWrongValue);
wrap:
	return liRc;
}

// ReSharper disable once CppParameterMayBeConst
LgsResult WorkerServer::ReloadGuiSettings(ConEmuGuiMapping* apFromCmd, LPDWORD pnWrongValue /*= NULL*/)
{
	bool lbChanged = false;
	LgsResult lgsResult = LgsResult::Failed;
	DWORD nWrongValue = 0;

	if (apFromCmd)
	{
		LogFunction(L"ReloadGuiSettings(apFromCmd)");
		lgsResult = LoadGuiSettingsPtr(gpSrv->guiSettings, apFromCmd, false, true, nWrongValue);
		lbChanged = (lgsResult >= LgsResult::Succeeded);
	}
	else
	{
		gpSrv->guiSettings.cbSize = sizeof(ConEmuGuiMapping);
		lgsResult = LoadGuiSettings(gpSrv->guiSettings, nWrongValue);
		lbChanged = (lgsResult >= LgsResult::Succeeded)
			&& ((gpSrv->guiSettingsChangeNum != gpSrv->guiSettings.nChangeNum)
				|| (gpSrv->pConsole && gpSrv->pConsole->hdr.ComSpec.ConEmuExeDir[0] == 0));
	}

	if (pnWrongValue)
		*pnWrongValue = nWrongValue;

	if (lbChanged)
	{
		LogFunction(L"ReloadGuiSettings(Apply)");

		gpSrv->guiSettingsChangeNum = gpSrv->guiSettings.nChangeNum;

		gbLogProcess = (gpSrv->guiSettings.nLoggingType == glt_Processes);

		UpdateComspec(&gpSrv->guiSettings.ComSpec); // isAddConEmu2Path, ...

		SetConEmuFolders(gpSrv->guiSettings.ComSpec.ConEmuExeDir, gpSrv->guiSettings.ComSpec.ConEmuBaseDir);

		// Won't set variable by us, the variable fill the Gui and it should be inherited by servers
		// #TODO is it correct for elevated/restricted/other_user accounts?
		//SetEnvironmentVariableW(L"ConEmuArgs", pInfo->sConEmuArgs);

		//wchar_t szHWND[16]; swprintf_c(szHWND, L"0x%08X", gpSrv->guiSettings.hGuiWnd.u);
		//SetEnvironmentVariable(ENV_CONEMUHWND_VAR_W, szHWND);
		SetConEmuWindows(gpSrv->guiSettings.hGuiWnd, gState.conemuWndDC_, gState.conemuWndBack_);

		if (gpSrv->pConsole)
		{
			CopySrvMapFromGuiMap();

			UpdateConsoleMapHeader(L"guiSettings were changed");
		}
	}

	return lgsResult;
}

bool WorkerServer::TryConnect2Gui(HWND hGui, DWORD anGuiPid, CESERVER_REQ* pIn)
{
	LogFunction(L"TryConnect2Gui");

	bool bConnected = false;
	CESERVER_REQ *pOut = nullptr;
	CESERVER_REQ_SRVSTARTSTOPRET* pStartStopRet = nullptr;

	_ASSERTE(pIn && ((pIn->hdr.nCmd==CECMD_ATTACH2GUI && gState.attachMode_) || (pIn->hdr.nCmd==CECMD_SRVSTARTSTOP && !gState.attachMode_)));

	ZeroStruct(gpSrv->ConnectInfo);
	gpSrv->ConnectInfo.hGuiWnd = hGui;

	//if (lbNeedSetFont) {
	//	lbNeedSetFont = FALSE;
	//
	//    if (gpLogSize) LogSize(nullptr, ":SetConsoleFontSizeTo.before");
	//    SetConsoleFontSizeTo(gState.realConWnd, consoleFontHeight_, consoleFontWidth_, gpSrv->szConsoleFont);
	//    if (gpLogSize) LogSize(nullptr, ":SetConsoleFontSizeTo.after");
	//}

	// Если GUI запущен не от имени админа - то он обломается при попытке
	// открыть дескриптор процесса сервера. Нужно будет ему помочь.
	if (pIn->hdr.nCmd == CECMD_ATTACH2GUI)
	{
		// CESERVER_REQ_STARTSTOP::sCmdLine[1] is variable length!
		_ASSERTE(pIn->DataSize() >= sizeof(pIn->StartStop));
		pIn->StartStop.hServerProcessHandle = nullptr;

		if (pIn->StartStop.bUserIsAdmin || gState.attachMode_)
		{
			DWORD  nGuiPid = 0;

			if ((hGui && GetWindowThreadProcessId(hGui, &nGuiPid) && nGuiPid)
				|| ((nGuiPid = anGuiPid) != 0))
			{
				// Issue 791: Fails, when GUI started under different credentials (login) as server
				HANDLE hGuiHandle = OpenProcess(PROCESS_DUP_HANDLE, FALSE, nGuiPid);

				if (!hGuiHandle)
				{
					gpSrv->ConnectInfo.nDupErrCode = GetLastError();
					_ASSERTE((hGuiHandle!=nullptr) && "Failed to transfer server process handle to GUI");
				}
				else
				{
					HANDLE hDupHandle = nullptr;

					if (DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
					                   hGuiHandle, &hDupHandle, MY_PROCESS_ALL_ACCESS/*PROCESS_QUERY_INFORMATION|SYNCHRONIZE*/,
					                   FALSE, 0)
					        && hDupHandle)
					{
						pIn->StartStop.hServerProcessHandle = hDupHandle;
					}
					else
					{
						gpSrv->ConnectInfo.nDupErrCode = GetLastError();
						_ASSERTE((hGuiHandle!=nullptr) && "Failed to transfer server process handle to GUI");
					}

					CloseHandle(hGuiHandle);
				}
			}
		}

		// Palette may revive after detach from ConEmu
		MyLoadConsolePalette((HANDLE)ghConOut, pIn->StartStop.Palette);

	}
	else
	{
		_ASSERTE(pIn->hdr.nCmd == CECMD_SRVSTARTSTOP);
		_ASSERTE(pIn->DataSize() == sizeof(pIn->SrvStartStop));
	}

	// Execute CECMD_ATTACH2GUI
	wchar_t szServerPipe[64];
	if (hGui)
	{
		swprintf_c(szServerPipe, CEGUIPIPENAME, L".", LODWORD(hGui)); //-V205
	}
	else if (anGuiPid)
	{
		swprintf_c(szServerPipe, CESERVERPIPENAME, L".", anGuiPid);
	}
	else
	{
		_ASSERTEX((hGui!=nullptr) || (anGuiPid!=0));
		goto wrap;
	}

	gpSrv->ConnectInfo.nInitTick = GetTickCount();

	while (true)
	{
		gpSrv->ConnectInfo.nStartTick = GetTickCount();

		ExecuteFreeResult(pOut);
		pOut = ExecuteCmd(szServerPipe, pIn, EXECUTE_CONNECT_GUI_CALL_TIMEOUT, gState.realConWnd_);
		gpSrv->ConnectInfo.bCallRc = (pOut->DataSize() >= sizeof(CESERVER_REQ_STARTSTOPRET));

		gpSrv->ConnectInfo.nErr = GetLastError();
		gpSrv->ConnectInfo.nEndTick = GetTickCount();
		gpSrv->ConnectInfo.nConnectDelta = gpSrv->ConnectInfo.nEndTick - gpSrv->ConnectInfo.nInitTick;
		gpSrv->ConnectInfo.nDelta = gpSrv->ConnectInfo.nEndTick - gpSrv->ConnectInfo.nStartTick;

		#ifdef _DEBUG
		if (gpSrv->ConnectInfo.bCallRc && (gpSrv->ConnectInfo.nDelta >= EXECUTE_CMD_WARN_TIMEOUT))
		{
			if (!IsDebuggerPresent())
			{
				//_ASSERTE(gpSrv->ConnectInfo.nDelta <= EXECUTE_CMD_WARN_TIMEOUT || (pIn->hdr.nCmd == CECMD_CMDSTARTSTOP && nDelta <= EXECUTE_CMD_WARN_TIMEOUT2));
				_ASSERTEX(gpSrv->ConnectInfo.nDelta <= EXECUTE_CMD_WARN_TIMEOUT);
			}
		}
		#endif

		if (gpSrv->ConnectInfo.bCallRc || (gpSrv->ConnectInfo.nConnectDelta > EXECUTE_CONNECT_GUI_TIMEOUT))
			break;

		if (!gpSrv->ConnectInfo.bCallRc)
		{
			_ASSERTE(gpSrv->ConnectInfo.bCallRc && (gpSrv->ConnectInfo.nErr==ERROR_FILE_NOT_FOUND) && "GUI was not initialized yet?");
			Sleep(250);
		}
	}

	// Этот блок if-else нужно вынести в отдельную функцию инициализации сервера (для аттача и обычный)
	pStartStopRet = (pOut->DataSize() >= sizeof(CESERVER_REQ_SRVSTARTSTOPRET)) ? &pOut->SrvStartStopRet : nullptr;

	if (!pStartStopRet || !pStartStopRet->Info.hWnd || !pStartStopRet->Info.hWndDc || !pStartStopRet->Info.hWndBack)
	{
		gpSrv->ConnectInfo.nErr = GetLastError();

		#ifdef _DEBUG
		wchar_t szDbgMsg[512], szTitle[128];
		GetModuleFileName(nullptr, szDbgMsg, countof(szDbgMsg));
		msprintf(szTitle, countof(szTitle), L"%s: PID=%u", PointToName(szDbgMsg), gnSelfPID);
		msprintf(szDbgMsg, countof(szDbgMsg),
			L"ExecuteCmd('%s',%u)\nFailed, code=%u, pOut=%s, Size=%u, "
			L"hWnd=x%08X, hWndDC=x%08X, hWndBack=x%08X",
			szServerPipe, (pOut ? pOut->hdr.nCmd : 0),
			gpSrv->ConnectInfo.nErr, (pOut ? L"OK" : L"nullptr"), pOut->DataSize(),
			pStartStopRet ? (DWORD)pStartStopRet->Info.hWnd : 0,
			pStartStopRet ? (DWORD)pStartStopRet->Info.hWndDc : 0,
			pStartStopRet ? (DWORD)pStartStopRet->Info.hWndBack : 0);
		MessageBox(nullptr, szDbgMsg, szTitle, MB_SYSTEMMODAL);
		SetLastError(gpSrv->ConnectInfo.nErr);
		#endif

		goto wrap;
	}

	_ASSERTE(pStartStopRet->GuiMapping.cbSize == sizeof(pStartStopRet->GuiMapping));

	// Environment initialization
	if (pStartStopRet->EnvCommands.cchCount)
	{
		ApplyEnvironmentCommands(pStartStopRet->EnvCommands.Demangle());
	}

	SetEnvironmentVariable(ENV_CONEMU_TASKNAME_W, pStartStopRet->TaskName.Demangle());
	SetEnvironmentVariable(ENV_CONEMU_PALETTENAME_W, pStartStopRet->PaletteName.Demangle());

	if (pStartStopRet->Palette.bPalletteLoaded)
	{
		gpSrv->ConsolePalette = pStartStopRet->Palette;
	}

	// Also calls SetConEmuEnvVar
	SetConEmuWindows(pStartStopRet->Info.hWnd, pStartStopRet->Info.hWndDc, pStartStopRet->Info.hWndBack);
	_ASSERTE(gState.conemuPid_ == pStartStopRet->Info.dwPID);
	gState.conemuPid_ = pStartStopRet->Info.dwPID;
	_ASSERTE(gState.conemuWnd_!=nullptr && "Must be set!");

	// Refresh settings
	ReloadGuiSettings(&pStartStopRet->GuiMapping);

	// Limited logging of console contents (same output as processed by CECF_ProcessAnsi)
	InitAnsiLog(pStartStopRet->AnsiLog);

	if (!gState.attachMode_) // Часть с "обычным" запуском сервера
	{
		#ifdef _DEBUG
		DWORD nGuiPID; GetWindowThreadProcessId(gState.conemuWnd_, &nGuiPID);
		_ASSERTEX(pOut->hdr.nSrcPID==nGuiPID);
		#endif

		gState.bWasDetached_ = FALSE;
		UpdateConsoleMapHeader(L"TryConnect2Gui, !gpStatus->attachMode_");
	}
	else // Запуск сервера "с аттачем" (это может быть RunAsAdmin и т.п.)
	{
		_ASSERTE(pOut->hdr.nCmd==CECMD_ATTACH2GUI);
		_ASSERTE(gpSrv->pConsoleMap != nullptr); // мэппинг уже должен быть создан,
		_ASSERTE(gpSrv->pConsole != nullptr); // и локальная копия тоже

		//gpSrv->pConsole->info.nGuiPID = pStartStopRet->dwPID;
		CESERVER_CONSOLE_MAPPING_HDR *pMap = gpSrv->pConsoleMap->Ptr();
		if (pMap)
		{
			_ASSERTE(gState.conemuPid_ == pStartStopRet->Info.dwPID);
			FixConsoleMappingHdr(pMap);
			_ASSERTE(pMap->hConEmuRoot==nullptr || pMap->nGuiPID!=0);
		}

		// Только если подцепились успешно
		if (gState.conemuWnd_)
		{
			//DisableAutoConfirmExit();

			if (gpConsoleArgs->IsForceHideConWnd())
			{
				if (!(gpSrv->guiSettings.Flags & CECF_RealConVisible))
					apiShowWindow(gState.realConWnd_, SW_HIDE);
			}

			// Установить шрифт в консоли
			if (pStartStopRet->Font.cbSize == sizeof(CESERVER_REQ_SETFONT))
			{
				consoleFontName_.Set(pStartStopRet->Font.sFontName, LF_FACESIZE - 1);
				consoleFontHeight_ = pStartStopRet->Font.inSizeY;
				consoleFontWidth_ = pStartStopRet->Font.inSizeX;
				ServerInitFont();
			}

			const auto crNewSize = MakeCoord(pStartStopRet->Info.nWidth, pStartStopRet->Info.nHeight);
			//SMALL_RECT rcWnd = {0,pIn->StartStop.sbi.srWindow.Top};
			const SMALL_RECT rcWnd = {};

			CESERVER_REQ *pSizeIn = nullptr, *pSizeOut = nullptr;
			if (this->dwAltServerPID && ((pSizeIn = ExecuteNewCmd(CECMD_SETSIZESYNC, sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_SETSIZE))) != nullptr))
			{
				// conhost uses only SHORT, SetSize.nBufferHeight is defines as USHORT
				_ASSERTE(!HIWORD(pStartStopRet->Info.nBufferHeight));
				pSizeIn->SetSize.nBufferHeight = LOWORD(pStartStopRet->Info.nBufferHeight);
				pSizeIn->SetSize.size = crNewSize;
				//pSizeIn->SetSize.nSendTopLine = -1;
				//pSizeIn->SetSize.rcWindow = rcWnd;

				pSizeOut = ExecuteSrvCmd(this->dwAltServerPID, pSizeIn, gState.realConWnd_);
			}
			else
			{
				SetConsoleSize((USHORT)pStartStopRet->Info.nBufferHeight, crNewSize, rcWnd, "Attach2Gui:Ret");
			}
			ExecuteFreeResult(pSizeIn);
			ExecuteFreeResult(pSizeOut);
		}
	}

	// Только если подцепились успешно
	if (gState.conemuWnd_)
	{
		CheckConEmuHwnd();
		gpWorker->Processes().OnAttached();
		gpSrv->ConnectInfo.bConnected = TRUE;
		bConnected = true;
	}
	else
	{
		//-- не надо, это сделает вызывающая функция
		//SetTerminateEvent(ste_Attach2GuiFailed);
		//DisableAutoConfirmExit();
		_ASSERTE(bConnected == false);
	}

wrap:
	ExecuteFreeResult(pOut);
	return bConnected;
}

HWND WorkerServer::Attach2Gui(DWORD nTimeout)
{
	LogFunction(L"Attach2Gui");

	if (!gState.bWasDetached_ && isTerminalMode())
	{
		_ASSERTE(FALSE && "Attach is not allowed in telnet");
		return nullptr;
	}

	// Нить Refresh НЕ должна быть запущена, иначе в мэппинг могут попасть данные из консоли
	// ДО того, как отработает ресайз (тот размер, который указал установить GUI при аттаче)
	_ASSERTE(this->dwRefreshThread==0 || gState.bWasDetached_);
	HWND hGui = nullptr;
	DWORD nToolhelpFoundGuiPID = 0;
	//UINT nMsg = RegisterWindowMessage(CONEMUMSG_ATTACH);
	BOOL bNeedStartGui = FALSE;
	DWORD nStartedGuiPID = 0;
	DWORD dwErr = 0;
	DWORD dwStartWaitIdleResult = -1;
	// Будем подцепляться заново
	if (gState.bWasDetached_)
	{
		gState.bWasDetached_ = FALSE;
		_ASSERTE(gState.attachMode_==am_None);
		if (!(gState.attachMode_ & am_Modes))
			gState.attachMode_ |= am_Simple;
		if (gpSrv->pConsole)
			gpSrv->pConsole->bDataChanged = TRUE;
	}

	if (!gpSrv->pConsoleMap)
	{
		_ASSERTE(gpSrv->pConsoleMap!=nullptr);
	}
	else
	{
		// Чтобы GUI не пытался считать информацию из консоли до завершения аттача (до изменения размеров)
		gpSrv->pConsoleMap->Ptr()->bDataReady = FALSE;
	}

	if (gpConsoleArgs->requestNewGuiWnd_ && !gState.conemuPid_ && !gState.hGuiWnd)
	{
		bNeedStartGui = TRUE;
		hGui = (HWND)-1;
	}
	else if (gState.hGuiWnd)
	{
		// Only HWND may be (was) specified, especially when running from batches
		if (!gState.conemuPid_)
			GetWindowThreadProcessId(gState.hGuiWnd, &gState.conemuPid_);

		wchar_t szClass[128] = L"";
		GetClassName(gState.hGuiWnd, szClass, countof(szClass));
		if (gState.conemuPid_ && lstrcmp(szClass, VirtualConsoleClassMain) == 0)
			hGui = gState.hGuiWnd;
		else
			gState.hGuiWnd = nullptr;
	}

	// That may fail if processes are running under different credentials or permissions
	if (!hGui)
		hGui = FindWindowEx(nullptr, hGui, VirtualConsoleClassMain, nullptr);

	if (!hGui)
	{
		//TODO: Reuse MToolHelp.h
		HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);

		if (hSnap != INVALID_HANDLE_VALUE)
		{
			PROCESSENTRY32 prc = {sizeof(PROCESSENTRY32)};

			if (Process32First(hSnap, &prc))
			{
				do
				{
					for (UINT i = 0; i < gpWorker->Processes().nProcessCount; i++)
					{
						if (lstrcmpiW(prc.szExeFile, L"conemu.exe")==0
							|| lstrcmpiW(prc.szExeFile, L"conemu64.exe")==0)
						{
							nToolhelpFoundGuiPID = prc.th32ProcessID;
							break;
						}
					}

					if (nToolhelpFoundGuiPID) break;
				}
				while (Process32Next(hSnap, &prc));
			}

			CloseHandle(hSnap);

			if (!nToolhelpFoundGuiPID) bNeedStartGui = TRUE;
		}
	}

	if (bNeedStartGui)
	{
		wchar_t szGuiExe[MAX_PATH];
		wchar_t *pszSlash = nullptr;

		if (!GetModuleFileName(nullptr, szGuiExe, MAX_PATH))
		{
			dwErr = GetLastError();
			_printf("GetModuleFileName failed, ErrCode=0x%08X\n", dwErr);
			return nullptr;
		}

		pszSlash = wcsrchr(szGuiExe, L'\\');

		if (!pszSlash)
		{
			_printf("Invalid GetModuleFileName, backslash not found!\n", 0, szGuiExe); //-V576
			return nullptr;
		}

		bool bExeFound = false;

		for (int s = 0; s <= 1; s++)
		{
			if (s)
			{
				// Он может быть на уровень выше
				*pszSlash = 0;
				pszSlash = wcsrchr(szGuiExe, L'\\');
				if (!pszSlash)
					break;
			}

			if (IsWindows64())
			{
				lstrcpyW(pszSlash+1, L"ConEmu64.exe");
				if (FileExists(szGuiExe))
				{
					bExeFound = true;
					break;
				}
			}

			lstrcpyW(pszSlash+1, L"ConEmu.exe");
			if (FileExists(szGuiExe))
			{
				bExeFound = true;
				break;
			}
		}

		if (!bExeFound)
		{
			_printf("ConEmu.exe not found!\n");
			return nullptr;
		}

		lstrcpyn(gpSrv->guiSettings.sConEmuExe, szGuiExe, countof(gpSrv->guiSettings.sConEmuExe));
		lstrcpyn(gpSrv->guiSettings.ComSpec.ConEmuExeDir, szGuiExe, countof(gpSrv->guiSettings.ComSpec.ConEmuExeDir));
		wchar_t* pszCut = wcsrchr(gpSrv->guiSettings.ComSpec.ConEmuExeDir, L'\\');
		if (pszCut)
			*pszCut = 0;
		if (gpSrv->pConsole)
		{
			lstrcpyn(gpSrv->pConsole->hdr.sConEmuExe, szGuiExe, countof(gpSrv->pConsole->hdr.sConEmuExe));
			lstrcpyn(gpSrv->pConsole->hdr.ComSpec.ConEmuExeDir, gpSrv->guiSettings.ComSpec.ConEmuExeDir, countof(gpSrv->pConsole->hdr.ComSpec.ConEmuExeDir));
		}

		bool bNeedQuot = IsQuotationNeeded(szGuiExe);
		CEStr lsGuiCmd(bNeedQuot ? L"\"" : nullptr, szGuiExe, bNeedQuot ? L"\"" : nullptr);

		// "/config" and others!
		CEStr cfgSwitches(GetEnvVar(L"ConEmuArgs"));
		if (!cfgSwitches.IsEmpty())
		{
			// `-cmd`, `-cmdlist`, `-run` or `-runlist` must be in the "ConEmuArgs2" only!
			#ifdef _DEBUG
			CmdArg lsFirst; LPCWSTR pszCfgSwitches = cfgSwitches.c_str();
			_ASSERTE(NextArg(pszCfgSwitches,lsFirst) && !lsFirst.OneOfSwitches(L"-cmd",L"-cmdlist",L"-run",L"-runlist"));
			#endif

			lstrmerge(&lsGuiCmd.ms_Val, L" ", cfgSwitches);
			lstrcpyn(gpSrv->guiSettings.sConEmuArgs, cfgSwitches, countof(gpSrv->guiSettings.sConEmuArgs));
		}

		// The server called from am_Async (RunMode::RM_AUTOATTACH) mode
		lstrmerge(&lsGuiCmd.ms_Val, L" -Detached");
		#ifdef _DEBUG
		lstrmerge(&lsGuiCmd.ms_Val, L" -NoKeyHooks");
		#endif

		PROCESS_INFORMATION pi; memset(&pi, 0, sizeof(pi));
		STARTUPINFOW si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
		PRINT_COMSPEC(L"Starting GUI:\n%s\n", pszSelf);
		// CREATE_NEW_PROCESS_GROUP - низя, перестает работать Ctrl-C
		// Запуск GUI (conemu.exe), хуки ест-но не нужны
		BOOL lbRc = createProcess(TRUE, nullptr, lsGuiCmd.ms_Val, nullptr,nullptr, TRUE,
		                           NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
		dwErr = GetLastError();

		if (!lbRc)
		{
			PrintExecuteError(lsGuiCmd, dwErr);
			return nullptr;
		}

		//delete psNewCmd; psNewCmd = nullptr;
		nStartedGuiPID = pi.dwProcessId;
		AllowSetForegroundWindow(pi.dwProcessId);
		PRINT_COMSPEC(L"Detached GUI was started. PID=%i, Attaching...\n", pi.dwProcessId);
		dwStartWaitIdleResult = WaitForInputIdle(pi.hProcess, INFINITE); // был nTimeout, видимо часто обламывался
		SafeCloseHandle(pi.hProcess); SafeCloseHandle(pi.hThread);
		//if (nTimeout > 1000) nTimeout = 1000;
	}

	DWORD dwStart = GetTickCount(), dwDelta = 0, dwCur = 0;
	CESERVER_REQ *pIn = nullptr;
	_ASSERTE(sizeof(CESERVER_REQ_STARTSTOP) >= sizeof(CESERVER_REQ_STARTSTOPRET));
	DWORD cchCmdMax = std::max<int>((gpszRunCmd ? lstrlen(gpszRunCmd) : 0), (MAX_PATH + 2)) + 1;
	DWORD nInSize =
		sizeof(CESERVER_REQ_HDR)
		+sizeof(CESERVER_REQ_STARTSTOP)
		+cchCmdMax*sizeof(wchar_t);
	pIn = ExecuteNewCmd(CECMD_ATTACH2GUI, nInSize);
	pIn->StartStop.nStarted = sst_ServerStart;
	wcscpy_c(pIn->StartStop.sIcon, gpSrv->pConsole->hdr.sIcon);
	pIn->StartStop.hWnd = gState.realConWnd_;
	pIn->StartStop.dwPID = gnSelfPID;
	pIn->StartStop.dwAID = gState.dwGuiAID;
	//pIn->StartStop.dwInputTID = gpSrv->dwInputPipeThreadId;
	pIn->StartStop.nSubSystem = gnImageSubsystem;
	// Сразу передать текущий KeyboardLayout
	IsKeyboardLayoutChanged(pIn->StartStop.dwKeybLayout);
	// После детача/аттача
	DWORD nAltWait;
	if (this->dwAltServerPID && this->hAltServer
		&& (WAIT_OBJECT_0 != (nAltWait = WaitForSingleObject(this->hAltServer, 0))))
	{
		pIn->StartStop.nAltServerPID = this->dwAltServerPID;
	}

	if (gpConsoleArgs->attachFromFar_)
		pIn->StartStop.bRootIsCmdExe = FALSE;
	else
		pIn->StartStop.bRootIsCmdExe = gState.rootIsCmdExe_ || (gState.attachMode_ && !gState.noCreateProcess_);

	pIn->StartStop.bRunInBackgroundTab = gbRunInBackgroundTab;

	bool bCmdSet = false;

	if (!bCmdSet && (gpszRunCmd && *gpszRunCmd))
	{
		_wcscpy_c(pIn->StartStop.sCmdLine, cchCmdMax, gpszRunCmd);
		bCmdSet = true;
	}

	if (!bCmdSet && !gpConsoleArgs->rootExe_.IsEmpty())
	{
		_wcscpy_c(pIn->StartStop.sCmdLine, cchCmdMax, gpConsoleArgs->rootExe_.GetStr());
		bCmdSet = true;
	}

	//TODO: In the (gpStatus->attachMode_ & am_Async) mode dwRootProcess is expected to be determined already
	_ASSERTE(bCmdSet || ((gState.attachMode_ & (am_Async|am_Simple)) && this->RootProcessId()));
	if (!bCmdSet && this->RootProcessId())
	{
		PROCESSENTRY32 pi;
		if (GetProcessInfo(this->RootProcessId(), &pi))
		{
			msprintf(pIn->StartStop.sCmdLine, cchCmdMax, L"\"%s\"", pi.szExeFile);
		}
	}

	if (pIn->StartStop.sCmdLine[0])
	{
		CEStr lsExe;
		IsNeedCmd(true, pIn->StartStop.sCmdLine, lsExe);
		lstrcpyn(pIn->StartStop.sModuleName, lsExe, countof(pIn->StartStop.sModuleName));
		_ASSERTE(pIn->StartStop.sModuleName[0]!=0);
	}
	else
	{
		// Must be set, otherwise ConEmu will not be able to determine proper AddDistinct options
		_ASSERTE(pIn->StartStop.sCmdLine[0]!=0);
	}

	// Если GUI запущен не от имени админа - то он обломается при попытке
	// открыть дескриптор процесса сервера. Нужно будет ему помочь.
	pIn->StartStop.bUserIsAdmin = IsUserAdmin();
	HANDLE hOut = (HANDLE)ghConOut;

	ServerInitConsoleSize(false, &pIn->StartStop.sbi);

	pIn->StartStop.crMaxSize = MyGetLargestConsoleWindowSize(hOut);

//LoopFind:
	// В обычном "серверном" режиме шрифт уже установлен, а при аттаче
	// другого процесса - шрифт все-равно поменять не получится
	//BOOL lbNeedSetFont = TRUE;

	_ASSERTE(gState.conemuWndDC_==nullptr);
	gState.conemuWndDC_ = nullptr;

	// Если с первого раза не получится (GUI мог еще не загрузиться) пробуем еще
	_ASSERTE(dwDelta < nTimeout); // Must runs at least once!
	while (dwDelta <= nTimeout)
	{
		if (gState.hGuiWnd)
		{
			// On success, it will set gState.conemuWndDC_ and others...
			TryConnect2Gui(gState.hGuiWnd, 0, pIn);
		}
		else
		{
			HWND hFindGui = FindWindowEx(nullptr, nullptr, VirtualConsoleClassMain, nullptr);
			DWORD nFindPID;

			if ((hFindGui == nullptr) && nToolhelpFoundGuiPID)
			{
				TryConnect2Gui(nullptr, nToolhelpFoundGuiPID, pIn);
			}
			else do
			{
				// Если ConEmu.exe мы запустили сами
				if (nStartedGuiPID)
				{
					// то цепляемся ТОЛЬКО к этому PID!
					GetWindowThreadProcessId(hFindGui, &nFindPID);
					if (nFindPID != nStartedGuiPID)
						continue;
				}

				// On success, it will set gState.conemuWndDC_ and others...
				if (TryConnect2Gui(hFindGui, 0, pIn))
					break; // OK
			} while ((hFindGui = FindWindowEx(nullptr, hFindGui, VirtualConsoleClassMain, nullptr)) != nullptr);
		}

		if (gState.conemuWndDC_)
			break;

		dwCur = GetTickCount(); dwDelta = dwCur - dwStart;

		if (dwDelta > nTimeout)
			break;

		Sleep(500);
		dwCur = GetTickCount(); dwDelta = dwCur - dwStart;
	}

	return gState.conemuWndDC_;
}



void WorkerServer::CopySrvMapFromGuiMap()
{
	LogFunction(L"CopySrvMapFromGuiMap");

	if (!gpSrv || !gpSrv->pConsole)
	{
		// Должно быть уже создано!
		_ASSERTE(gpSrv && gpSrv->pConsole);
		return;
	}

	if (!gpSrv->guiSettings.cbSize)
	{
		_ASSERTE(gpSrv->guiSettings.cbSize==sizeof(ConEmuGuiMapping));
		return;
	}

	// настройки командного процессора
	_ASSERTE(gpSrv->guiSettings.ComSpec.ConEmuExeDir[0]!=0 && gpSrv->guiSettings.ComSpec.ConEmuBaseDir[0]!=0);
	gpSrv->pConsole->hdr.ComSpec = gpSrv->guiSettings.ComSpec;

	// Путь к GUI
	if (gpSrv->guiSettings.sConEmuExe[0] != 0)
	{
		wcscpy_c(gpSrv->pConsole->hdr.sConEmuExe, gpSrv->guiSettings.sConEmuExe);
	}
	else
	{
		_ASSERTE(gpSrv->guiSettings.sConEmuExe[0]!=0);
	}

	gpSrv->pConsole->hdr.nLoggingType = gpSrv->guiSettings.nLoggingType;
	gpSrv->pConsole->hdr.useInjects = gpSrv->guiSettings.useInjects;
	//gpSrv->pConsole->hdr.bDosBox = gpSrv->guiSettings.bDosBox;
	//gpSrv->pConsole->hdr.bUseTrueColor = gpSrv->guiSettings.bUseTrueColor;
	//gpSrv->pConsole->hdr.bProcessAnsi = gpSrv->guiSettings.bProcessAnsi;
	//gpSrv->pConsole->hdr.bUseClink = gpSrv->guiSettings.bUseClink;
	gpSrv->pConsole->hdr.Flags = gpSrv->guiSettings.Flags;

	// Обновить пути к ConEmu
	_ASSERTE(gpSrv->guiSettings.sConEmuExe[0]!=0);
	wcscpy_c(gpSrv->pConsole->hdr.sConEmuExe, gpSrv->guiSettings.sConEmuExe);
	//wcscpy_c(gpSrv->pConsole->hdr.sConEmuBaseDir, gpSrv->guiSettings.sConEmuBaseDir);
}


/*
!! Во избежание растранжиривания памяти фиксированно создавать MAP только для шапки (Info), сами же данные
   загружать в дополнительный MAP, "ConEmuFileMapping.%08X.N", где N == nCurDataMapIdx
   Собственно это для того, чтобы при увеличении размера консоли можно было безболезненно увеличить объем данных
   и легко переоткрыть память во всех модулях

!! Для начала, в плагине после реальной записи в консоль просто дергать событие. Т.к. фар и сервер крутятся
   под одним аккаунтом - проблем с открытием события быть не должно.

1. ReadConsoleData должна сразу прикидывать, Размер данных больше 30K? Если больше - то и не пытаться читать блоком.
2. ReadConsoleInfo & ReadConsoleData должны возвращать TRUE при наличии изменений (последняя должна кэшировать последнее чтение для сравнения)
3. ReloadFullConsoleInfo пытаться звать ReadConsoleInfo & ReadConsoleData только ОДИН раз. Счетчик крутить только при наличии изменений. Но можно обновить Tick

4. В RefreshThread ждать события hRefresh 10мс (строго) или hExit.
-- Если есть запрос на изменение размера -
   ставить в TRUE локальную переменную bChangingSize
   менять размер и только после этого
-- звать ReloadFullConsoleInfo
-- После нее, если bChangingSize - установить Event hSizeChanged.
5. Убить все критические секции. Они похоже уже не нужны, т.к. все чтение консоли будет в RefreshThread.
6. Команда смена размера НЕ должна сама менять размер, а передавать запрос в RefreshThread и ждать события hSizeChanged
*/


int WorkerServer::CreateMapHeader()
{
	LogFunction(L"CreateMapHeader");

	int iRc = 0;
	//wchar_t szMapName[64];
	//int nConInfoSize = sizeof(CESERVER_CONSOLE_MAPPING_HDR);
	_ASSERTE(gpSrv->pConsole == nullptr);
	_ASSERTE(gpSrv->pConsoleMap == nullptr);
	_ASSERTE(gpSrv->pConsoleDataCopy == nullptr);
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	COORD crMax = MyGetLargestConsoleWindowSize(h);

	if (crMax.X < 80 || crMax.Y < 25)
	{
#ifdef _DEBUG
		DWORD dwErr = GetLastError();
		if (gState.isWine_)
		{
			wchar_t szDbgMsg[512], szTitle[128];
			szDbgMsg[0] = 0;
			GetModuleFileName(nullptr, szDbgMsg, countof(szDbgMsg));
			msprintf(szTitle, countof(szTitle), L"%s: PID=%u", PointToName(szDbgMsg), GetCurrentProcessId());
			msprintf(szDbgMsg, countof(szDbgMsg), L"GetLargestConsoleWindowSize failed -> {%ix%i}, Code=%u", crMax.X, crMax.Y, dwErr);
			MessageBox(nullptr, szDbgMsg, szTitle, MB_SYSTEMMODAL);
		}
		else
		{
			_ASSERTE(crMax.X >= 80 && crMax.Y >= 25);
		}
#endif

		if (crMax.X < 80) crMax.X = 80;

		if (crMax.Y < 25) crMax.Y = 25;
	}

	// Размер шрифта может быть еще не уменьшен? Прикинем размер по максимуму?
	HMONITOR hMon = MonitorFromWindow(gState.realConWnd_, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFO mi = {sizeof(MONITORINFO)};

	if (GetMonitorInfo(hMon, &mi))
	{
		int x = (mi.rcWork.right - mi.rcWork.left) / 3;
		int y = (mi.rcWork.bottom - mi.rcWork.top) / 5;

		if (crMax.X < x || crMax.Y < y)
		{
			//_ASSERTE((crMax.X + 16) >= x && (crMax.Y + 32) >= y);
			if (crMax.X < x)
				crMax.X = x;

			if (crMax.Y < y)
				crMax.Y = y;
		}
	}

	//TODO("Добавить к nConDataSize размер необходимый для хранения crMax ячеек");
	int nTotalSize = 0;
	DWORD nMaxCells = (crMax.X * crMax.Y);
	//DWORD nHdrSize = ((LPBYTE)gpSrv->pConsoleDataCopy->Buf) - ((LPBYTE)gpSrv->pConsoleDataCopy);
	//_ASSERTE(sizeof(CESERVER_REQ_CONINFO_DATA) == (sizeof(COORD)+sizeof(CHAR_INFO)));
	int nMaxDataSize = nMaxCells * sizeof(CHAR_INFO); // + nHdrSize;
	bool lbCreated, lbUseExisting = false;

	gpSrv->pConsoleDataCopy = (CHAR_INFO*)calloc(nMaxDataSize,1);

	if (!gpSrv->pConsoleDataCopy)
	{
		_printf("ConEmuC: calloc(%i) failed, pConsoleDataCopy is null", nMaxDataSize);
		goto wrap;
	}

	//gpSrv->pConsoleDataCopy->crMaxSize = crMax;
	nTotalSize = sizeof(CESERVER_REQ_CONINFO_FULL) + (nMaxCells * sizeof(CHAR_INFO));
	gpSrv->pConsole = (CESERVER_REQ_CONINFO_FULL*)calloc(nTotalSize,1);

	if (!gpSrv->pConsole)
	{
		_printf("ConEmuC: calloc(%i) failed, pConsole is null", nTotalSize);
		goto wrap;
	}

	if (!gpSrv->pGuiInfoMap)
		gpSrv->pGuiInfoMap = new MFileMapping<ConEmuGuiMapping>;

	if (!gpSrv->pConsoleMap)
		gpSrv->pConsoleMap = new MFileMapping<CESERVER_CONSOLE_MAPPING_HDR>;
	if (!gpSrv->pConsoleMap)
	{
		_printf("ConEmuC: calloc(MFileMapping<CESERVER_CONSOLE_MAPPING_HDR>) failed, pConsoleMap is null", 0); //-V576
		goto wrap;
	}

	if (!gpSrv->pAppMap)
		gpSrv->pAppMap = new MFileMapping<CESERVER_CONSOLE_APP_MAPPING>;
	if (!gpSrv->pAppMap)
	{
		_printf("ConEmuC: calloc(MFileMapping<CESERVER_CONSOLE_APP_MAPPING>) failed, pAppMap is null", 0); //-V576
		goto wrap;
	}

	gpSrv->pConsoleMap->InitName(CECONMAPNAME, LODWORD(gState.realConWnd_)); //-V205
	gpSrv->pAppMap->InitName(CECONAPPMAPNAME, LODWORD(gState.realConWnd_)); //-V205

	if (gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AutoAttach)
	{
		lbCreated = (gpSrv->pConsoleMap->Create() != nullptr)
			&& (gpSrv->pAppMap->Create() != nullptr);
	}
	else
	{
		lbCreated = (gpSrv->pConsoleMap->Open() != nullptr)
			&& (gpSrv->pAppMap->Open(TRUE) != nullptr);
	}

	if (!lbCreated)
	{
		_ASSERTE(FALSE && "Failed to create/open mapping!");
		_wprintf(gpSrv->pConsoleMap->GetErrorText());
		SafeDelete(gpSrv->pConsoleMap);
		iRc = CERR_CREATEMAPPINGERR; goto wrap;
	}
	else if (gState.runMode_ == RunMode::AltServer)
	{
		// На всякий случай, перекинем параметры
		if (gpSrv->pConsoleMap->GetTo(&gpSrv->pConsole->hdr))
		{
			lbUseExisting = true;

			if (gpSrv->pConsole->hdr.ComSpec.ConEmuExeDir[0] && gpSrv->pConsole->hdr.ComSpec.ConEmuBaseDir[0])
			{
				gpSrv->guiSettings.ComSpec = gpSrv->pConsole->hdr.ComSpec;
			}
		}
	}
	else if (gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AutoAttach)
	{
		CESERVER_CONSOLE_APP_MAPPING init = {sizeof(CESERVER_CONSOLE_APP_MAPPING), CESERVER_REQ_VER};
		init.HookedPids.Init();
		gpSrv->pAppMap->SetFrom(&init);
	}

	// !!! Warning !!! Изменил здесь, поменяй и ReloadGuiSettings/CopySrvMapFromGuiMap !!!
	gpSrv->pConsole->cbMaxSize = nTotalSize;
	gpSrv->pConsole->hdr.cbSize = sizeof(gpSrv->pConsole->hdr);
	if (!lbUseExisting)
		gpSrv->pConsole->hdr.nLogLevel = (gpLogSize!=nullptr) ? 1 : 0;
	gpSrv->pConsole->hdr.crMaxConSize = crMax;
	gpSrv->pConsole->hdr.bDataReady = FALSE;
	gpSrv->pConsole->hdr.hConWnd = gState.realConWnd_; _ASSERTE(gState.realConWnd_!=nullptr);
	_ASSERTE((this->dwMainServerPID!=0) || (gState.attachMode_ & am_Async));
	if (gState.attachMode_ & am_Async)
	{
		_ASSERTE(this->dwMainServerPID == 0);
		gpSrv->pConsole->hdr.nServerPID = 0;
	}
	else
	{
		gpSrv->pConsole->hdr.nServerPID = this->dwMainServerPID;
	}
	gpSrv->pConsole->hdr.nAltServerPID = (gState.runMode_==RunMode::AltServer) ? GetCurrentProcessId() : this->dwAltServerPID;
	gpSrv->pConsole->hdr.nGuiPID = gState.conemuPid_;
	gpSrv->pConsole->hdr.hConEmuRoot = gState.conemuWnd_;
	gpSrv->pConsole->hdr.hConEmuWndDc = gState.conemuWndDC_;
	gpSrv->pConsole->hdr.hConEmuWndBack = gState.conemuWndBack_;
	_ASSERTE(gpSrv->pConsole->hdr.hConEmuRoot==nullptr || gpSrv->pConsole->hdr.nGuiPID!=0);
	gpSrv->pConsole->hdr.nServerInShutdown = 0;
	gpSrv->pConsole->hdr.nProtocolVersion = CESERVER_REQ_VER;
	gpSrv->pConsole->hdr.nActiveFarPID = this->nActiveFarPID_;

	// Обновить переменные окружения (через ConEmuGuiMapping)
	if (gState.conemuWnd_) // если уже известен - тогда можно
		ReloadGuiSettings(nullptr);

	// По идее, уже должно быть настроено
	if (gpSrv->guiSettings.cbSize == sizeof(ConEmuGuiMapping))
	{
		CopySrvMapFromGuiMap();
	}
	else
	{
		_ASSERTE(gpSrv->guiSettings.cbSize==sizeof(ConEmuGuiMapping) || (gState.attachMode_ && !gState.conemuWnd_));
	}


	gpSrv->pConsole->ConState.hConWnd = gState.realConWnd_; _ASSERTE(gState.realConWnd_!=nullptr);
	gpSrv->pConsole->ConState.crMaxSize = crMax;

	// Проверять, нужно ли реестр хукать, будем в конце ServerInit

	//WARNING! Сразу ставим флаг измененности чтобы данные сразу пошли в GUI
	gpSrv->pConsole->bDataChanged = TRUE;


	UpdateConsoleMapHeader(L"CreateMapHeader");
wrap:
	return iRc;
}

int WorkerServer::Compare(const CESERVER_CONSOLE_MAPPING_HDR* p1, const CESERVER_CONSOLE_MAPPING_HDR* p2)
{
	if (!p1 || !p2)
	{
		_ASSERTE(FALSE && "Invalid arguments");
		return 0;
	}
	#ifdef _DEBUG
	_ASSERTE(p1->cbSize==sizeof(CESERVER_CONSOLE_MAPPING_HDR) && (p1->cbSize==p2->cbSize || p2->cbSize==0));
	if (p1->cbSize != p2->cbSize)
		return 1;
	if (p1->nAltServerPID != p2->nAltServerPID)
		return 2;
	if (p1->nActiveFarPID != p2->nActiveFarPID)
		return 3;
	if (memcmp(&p1->crLockedVisible, &p2->crLockedVisible, sizeof(p1->crLockedVisible))!=0)
		return 4;
	#endif
	int nCmp = memcmp(p1, p2, p1->cbSize);
	return nCmp;
};

void WorkerServer::UpdateConsoleMapHeader(LPCWSTR asReason /*= nullptr*/)
{
	CEStr lsLog(L"UpdateConsoleMapHeader{", asReason, L"}");
	LogFunction(lsLog);

	WARNING("***ALT*** не нужно обновлять мэппинг одновременно и в сервере и в альт.сервере");

	if (gpSrv && gpSrv->pConsole)
	{
		if (gState.runMode_ == RunMode::Server) // !!! RunMode::RM_ALTSERVER - ниже !!!
		{
			if (gState.conemuWndDC_ && (!this->pColorerMapping || (gpSrv->pConsole->hdr.hConEmuWndDc != gState.conemuWndDC_)))
			{
				bool bRecreate = (this->pColorerMapping && (gpSrv->pConsole->hdr.hConEmuWndDc != gState.conemuWndDC_));
				CreateColorerHeader(bRecreate);
			}
			gpSrv->pConsole->hdr.nServerPID = GetCurrentProcessId();
			gpSrv->pConsole->hdr.nAltServerPID = this->dwAltServerPID;
		}
		else if (gState.runMode_ == RunMode::AltServer)
		{
			DWORD nCurServerInMap = 0;
			if (gpSrv->pConsoleMap && gpSrv->pConsoleMap->IsValid())
				nCurServerInMap = gpSrv->pConsoleMap->Ptr()->nServerPID;

			_ASSERTE(gpSrv->pConsole->hdr.nServerPID && (gpSrv->pConsole->hdr.nServerPID == this->dwMainServerPID));
			if (nCurServerInMap && (nCurServerInMap != this->dwMainServerPID))
			{
				if (IsMainServerPID(nCurServerInMap))
				{
					// Странно, основной сервер сменился?
					_ASSERTE((nCurServerInMap == this->dwMainServerPID) && "Main server was changed?");
					CloseHandle(this->hMainServer);
					this->dwMainServerPID = nCurServerInMap;
					this->hMainServer = OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION, FALSE, nCurServerInMap);
				}
			}

			gpSrv->pConsole->hdr.nServerPID = this->dwMainServerPID;
			gpSrv->pConsole->hdr.nAltServerPID = GetCurrentProcessId();
		}

		if (gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer)
		{
			// Размер _видимой_ области. Консольным приложениям запрещено менять его "изнутри".
			// Размер может менять только пользователь ресайзом окна ConEmu
			_ASSERTE(gcrVisibleSize.X>0 && gcrVisibleSize.X<=400 && gcrVisibleSize.Y>0 && gcrVisibleSize.Y<=300);
			gpSrv->pConsole->hdr.bLockVisibleArea = TRUE;
			gpSrv->pConsole->hdr.crLockedVisible = gcrVisibleSize;
			// Какая прокрутка допустима. Пока - любая.
			gpSrv->pConsole->hdr.rbsAllowed = rbs_Any;
		}
		gpSrv->pConsole->hdr.nGuiPID = gState.conemuPid_;
		gpSrv->pConsole->hdr.hConEmuRoot = gState.conemuWnd_;
		gpSrv->pConsole->hdr.hConEmuWndDc = gState.conemuWndDC_;
		gpSrv->pConsole->hdr.hConEmuWndBack = gState.conemuWndBack_;
		_ASSERTE(gpSrv->pConsole->hdr.hConEmuRoot==nullptr || gpSrv->pConsole->hdr.nGuiPID!=0);
		gpSrv->pConsole->hdr.nActiveFarPID = this->nActiveFarPID_;

		if (gState.runMode_ == RunMode::Server)
		{
			// Limited logging of console contents (same output as processed by CECF_ProcessAnsi)
			gpSrv->pConsole->hdr.AnsiLog = gpSrv->AnsiLog;
		}

		#ifdef _DEBUG
		int nMapCmp = -100;
		if (gpSrv->pConsoleMap)
			nMapCmp = Compare(&gpSrv->pConsole->hdr, gpSrv->pConsoleMap->Ptr());
		#endif

		// Нельзя альт.серверу мэппинг менять - подерутся
		if ((gState.runMode_ != RunMode::Server) && (gState.runMode_ != RunMode::AutoAttach))
		{
			_ASSERTE(gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer || gState.runMode_ == RunMode::AutoAttach);
			// Могли измениться: gcrVisibleSize, nActiveFarPID
			if (this->dwMainServerPID && this->dwMainServerPID != GetCurrentProcessId())
			{
				size_t nReqSize = sizeof(CESERVER_REQ_HDR) + sizeof(CESERVER_CONSOLE_MAPPING_HDR);
				CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_UPDCONMAPHDR, nReqSize);
				if (pIn)
				{
					pIn->ConInfo = gpSrv->pConsole->hdr;
					CESERVER_REQ* pOut = ExecuteSrvCmd(this->dwMainServerPID, pIn, gState.realConWnd_);
					ExecuteFreeResult(pIn);
					ExecuteFreeResult(pOut);
				}
			}
			return;
		}

		if (gpSrv->pConsoleMap)
		{
			if (gpSrv->pConsole->hdr.ComSpec.ConEmuExeDir[0]==0 || gpSrv->pConsole->hdr.ComSpec.ConEmuBaseDir[0]==0)
			{
				_ASSERTE((gpSrv->pConsole->hdr.ComSpec.ConEmuExeDir[0]!=0 && gpSrv->pConsole->hdr.ComSpec.ConEmuBaseDir[0]!=0) || (gState.attachMode_ && !gState.conemuWnd_));
				wchar_t szSelfPath[MAX_PATH+1];
				if (GetModuleFileName(nullptr, szSelfPath, countof(szSelfPath)))
				{
					wchar_t* pszSlash = wcsrchr(szSelfPath, L'\\');
					if (pszSlash)
					{
						*pszSlash = 0;
						lstrcpy(gpSrv->pConsole->hdr.ComSpec.ConEmuBaseDir, szSelfPath);
					}
				}
			}

			if (gpSrv->pConsole->hdr.sConEmuExe[0] == 0)
			{
				_ASSERTE((gpSrv->pConsole->hdr.sConEmuExe[0]!=0) || (gState.attachMode_ && !gState.conemuWnd_));
			}

			gpSrv->pConsoleMap->SetFrom(&(gpSrv->pConsole->hdr));
		}
	}
}

int WorkerServer::CreateColorerHeader(bool bForceRecreate /*= false*/)
{
	LogFunction(L"CreateColorerHeader");

	if (!gpSrv)
	{
		_ASSERTE(gpSrv!=nullptr);
		return -1;
	}

	int iRc = -1;
	DWORD dwErr = 0;
	HWND lhConWnd = nullptr;
	const AnnotationHeader* pHdr = nullptr;
	int nHdrSize;

	MSectionLockSimple CS;
	CS.Lock(&this->csColorerMappingCreate);

	// По идее, не должно быть пересоздания TrueColor мэппинга, разве что при Detach/Attach
	_ASSERTE((this->pColorerMapping == nullptr) || (gState.attachMode_ == am_Simple));

	if (bForceRecreate)
	{
		if (this->pColorerMapping)
		{
			SafeDelete(this->pColorerMapping);
		}
		else
		{
			// Если уж был запрос на пересоздание - должно быть уже создано
			_ASSERTE(this->pColorerMapping!=nullptr);
		}
	}
	else if (this->pColorerMapping != nullptr)
	{
		_ASSERTE(FALSE && "pColorerMapping was already created");
		iRc = 0;
		goto wrap;
	}

	// 111101 - было "GetConEmuHWND(2)", но GetConsoleWindow теперь перехватывается.
	lhConWnd = gState.conemuWndDC_; // GetConEmuHWND(2);

	if (!lhConWnd)
	{
		_ASSERTE(lhConWnd != nullptr);
		dwErr = GetLastError();
		_printf("Can't create console data file mapping. ConEmu DC window is nullptr.\n");
		//iRc = CERR_COLORERMAPPINGERR; -- ошибка не критическая и не обрабатывается
		iRc = 0;
		goto wrap;
	}

	//COORD crMaxSize = MyGetLargestConsoleWindowSize(GetStdHandle(STD_OUTPUT_HANDLE));
	//nMapCells = std::max(crMaxSize.X,200) * std::max(crMaxSize.Y,200) * 2;
	//nMapSize = nMapCells * sizeof(AnnotationInfo) + sizeof(AnnotationHeader);
	_ASSERTE(sizeof(AnnotationInfo) == 8*sizeof(int)/*sizeof(AnnotationInfo::raw)*/);

	if (this->pColorerMapping == nullptr)
	{
		this->pColorerMapping = new MFileMapping<const AnnotationHeader>;
	}
	// Задать имя для mapping, если надо - сам сделает CloseMap();
	this->pColorerMapping->InitName(AnnotationShareName, (DWORD)sizeof(AnnotationInfo), LODWORD(lhConWnd)); //-V205

	//swprintf_c(szMapName, AnnotationShareName, sizeof(AnnotationInfo), (DWORD)lhConWnd);
	//gpSrv->hColorerMapping = CreateFileMapping(INVALID_HANDLE_VALUE,
	//                                        gpLocalSecurity, PAGE_READWRITE, 0, nMapSize, szMapName);

	//if (!gpSrv->hColorerMapping)
	//{
	//	dwErr = GetLastError();
	//	_printf("Can't create colorer data mapping. ErrCode=0x%08X\n", dwErr, szMapName);
	//	iRc = CERR_COLORERMAPPINGERR;
	//}
	//else
	//{
	// Заголовок мэппинга содержит информацию о размере, нужно заполнить!
	//AnnotationHeader* pHdr = (AnnotationHeader*)MapViewOfFile(gpSrv->hColorerMapping, FILE_MAP_ALL_ACCESS,0,0,0);
	// 111101 - было "Create(nMapSize);"

	// AnnotationShareName is CREATED in ConEmu.exe
	// May be it would be better, to avoid hooking and cycling (minhook),
	// call CreateFileMapping instead of OpenFileMapping...
	pHdr = this->pColorerMapping->Open();

	if (!pHdr)
	{
		dwErr = GetLastError();
		// The TrueColor may be disabled in ConEmu settings, don't warn user about it
		SafeDelete(this->pColorerMapping);
		goto wrap;
	}
	else if ((nHdrSize = pHdr->struct_size) != sizeof(AnnotationHeader))
	{
		Sleep(500);
		int nDbgSize = pHdr->struct_size;
		_ASSERTE(nHdrSize == sizeof(AnnotationHeader));
		UNREFERENCED_PARAMETER(nDbgSize);

		if (pHdr->struct_size != sizeof(AnnotationHeader))
		{
			SafeDelete(this->pColorerMapping);
			goto wrap;
		}
	}

	_ASSERTE((gState.runMode_ == RunMode::AltServer) || (pHdr->locked == 0 && pHdr->flushCounter == 0));

	this->ColorerHdr = *pHdr;

	// OK
	iRc = 0;

wrap:
	CS.Unlock();
	return iRc;
}

void WorkerServer::CloseMapHeader()
{
	LogFunction(L"CloseMapHeader");

	if (gpSrv->pConsoleMap)
	{
		//gpSrv->pConsoleMap->CloseMap(); -- не требуется, сделает деструктор
		SafeDelete(gpSrv->pConsoleMap);
	}

	if (gpSrv->pConsole)
	{
		free(gpSrv->pConsole);
		gpSrv->pConsole = nullptr;
	}

	if (gpSrv->pConsoleDataCopy)
	{
		free(gpSrv->pConsoleDataCopy);
		gpSrv->pConsoleDataCopy = nullptr;
	}

	if (gpSrv->pConsoleMap)
	{
		free(gpSrv->pConsoleMap);
		gpSrv->pConsoleMap = nullptr;
	}
}


// Limited logging of console contents (same output as processed by CECF_ProcessAnsi)
void WorkerServer::InitAnsiLog(const ConEmuAnsiLog& AnsiLog)
{
	LogFunction(L"InitAnsiLog");
	// Reset first
	SetEnvironmentVariable(ENV_CONEMUANSILOG_VAR_W, L"");
	gpSrv->AnsiLog = {};
	// Enabled?
	if (!AnsiLog.Enabled || !*AnsiLog.Path)
	{
		_ASSERTE(!AnsiLog.Enabled || AnsiLog.Path[0]!=0);
		return;
	}
	// May contains variables
	CEStr log_file(ExpandEnvStr(AnsiLog.Path));
	if (log_file.IsEmpty())
		log_file.Set(AnsiLog.Path);
	const wchar_t* ptr_name = PointToName(log_file.c_str());
	if (!ptr_name)
	{
		_ASSERTE(ptr_name != nullptr);
		return;
	}
	const ssize_t idx_name = ptr_name - log_file.c_str();
	const wchar_t name_chr = log_file.SetAt(idx_name, 0);
	if (!DirectoryExists(log_file))
	{
		if (!MyCreateDirectory(log_file.ms_Val))
		{
			DWORD dwErr = GetLastError();
			_printf("Failed to create AnsiLog-files directory:\n");
			_wprintf(log_file);
			print_error(dwErr);
			return;
		}
	}
	log_file.SetAt(idx_name, name_chr);
	// Try to create
	HANDLE hLog = CreateFile(log_file, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!hLog || hLog == INVALID_HANDLE_VALUE)
	{
		DWORD dwErr = GetLastError();
		_printf("Failed to create new AnsiLog-file:\n");
		_wprintf(log_file);
		print_error(dwErr);
		return;
	}
	CloseHandle(hLog);
	// OK!
	gpSrv->AnsiLog = AnsiLog;
	wcscpy_c(gpSrv->AnsiLog.Path, log_file);
	SetEnvironmentVariable(ENV_CONEMUANSILOG_VAR_W, log_file);
}

int WorkerServer::ReadConsoleInfo()
{
	// Need to block all requests to output buffer in other threads
	MSectionLockSimple csRead = LockConsoleReaders(LOCK_READOUTPUT_TIMEOUT);

	if (CheckHwFullScreen())
	{
		LogString("!!! ReadConsoleInfo was skipped due to CONSOLE_FULLSCREEN_HARDWARE !!!");
		return -1;
	}

	//int liRc = 1;
	BOOL lbChanged = gpSrv->pConsole->bDataChanged; // Если что-то еще не отослали - сразу TRUE
	CONSOLE_SELECTION_INFO lsel = {0}; // apiGetConsoleSelectionInfo
	CONSOLE_CURSOR_INFO lci = {0}; // GetConsoleCursorInfo
	DWORD ldwConsoleCP=0, ldwConsoleOutputCP=0, ldwConsoleMode;
	CONSOLE_SCREEN_BUFFER_INFO lsbi = {{0,0}}; // MyGetConsoleScreenBufferInfo
	HANDLE hOut = (HANDLE)ghConOut;
	HANDLE hStdOut = nullptr;
	HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
	//DWORD nConInMode = 0;

	if (hOut == INVALID_HANDLE_VALUE)
		hOut = hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

	// Могут возникать проблемы при закрытии ComSpec и уменьшении высоты буфера
	MCHKHEAP;

	if (!apiGetConsoleSelectionInfo(&lsel))
	{
		SetConEmuFlags(gpSrv->pConsole->ConState.Flags,CECI_Paused,(CECI_None));
	}
	else
	{
		SetConEmuFlags(gpSrv->pConsole->ConState.Flags,CECI_Paused,((lsel.dwFlags & CONSOLE_SELECTION_IN_PROGRESS) ? CECI_Paused : CECI_None));
	}

	if (!GetConsoleCursorInfo(hOut, &lci))
	{
		gpSrv->dwCiRc = GetLastError(); if (!gpSrv->dwCiRc) gpSrv->dwCiRc = -1;
	}
	else
	{
		TODO("Нифига не реагирует на режим вставки в cmd.exe, видимо, GetConsoleMode можно получить только в cmd.exe");
		//if (gpSrv->bTelnetActive) lci.dwSize = 15;  // telnet "глючит" при нажатии Ins - меняет курсор даже когда нажат Ctrl например
		//GetConsoleMode(hIn, &nConInMode);
		//GetConsoleMode(hOut, &nConInMode);
		//if (GetConsoleMode(hIn, &nConInMode) && !(nConInMode & ENABLE_INSERT_MODE) && (lci.dwSize < 50))
		//	lci.dwSize = 50;

		gpSrv->dwCiRc = 0;

		if (memcmp(&gpSrv->ci, &lci, sizeof(gpSrv->ci)))
		{
			gpSrv->ci = lci;
			lbChanged = TRUE;
		}
	}

	ldwConsoleCP = GetConsoleCP();

	if (gpSrv->dwConsoleCP!=ldwConsoleCP)
	{
		LogModeChange(L"ConCP", gpSrv->dwConsoleCP, ldwConsoleCP);
		gpSrv->dwConsoleCP = ldwConsoleCP; lbChanged = TRUE;
	}

	ldwConsoleOutputCP = GetConsoleOutputCP();

	if (gpSrv->dwConsoleOutputCP!=ldwConsoleOutputCP)
	{
		LogModeChange(L"ConOutCP", gpSrv->dwConsoleOutputCP, ldwConsoleOutputCP);
		gpSrv->dwConsoleOutputCP = ldwConsoleOutputCP; lbChanged = TRUE;
	}

	// ConsoleInMode
	ldwConsoleMode = 0;
	DEBUGTEST(BOOL lbConModRc =)
	GetConsoleMode(hStdIn, &ldwConsoleMode);
	if (gpSrv->dwConsoleInMode != LOWORD(ldwConsoleMode))
	{
		_ASSERTE(LOWORD(ldwConsoleMode) == ldwConsoleMode);
		LogModeChange(L"ConInMode", gpSrv->dwConsoleInMode, ldwConsoleMode);

		if ((ldwConsoleMode & ENABLE_VIRTUAL_TERMINAL_INPUT) != (gpSrv->dwConsoleInMode & ENABLE_VIRTUAL_TERMINAL_INPUT))
		{
			CESERVER_REQ* pIn = ExecuteNewCmd(CECMD_STARTXTERM, sizeof(CESERVER_REQ_HDR)+3*sizeof(DWORD));
			if (pIn)
			{
				pIn->dwData[0] = tmc_ConInMode;
				pIn->dwData[1] = ldwConsoleMode;
				pIn->dwData[2] = this->RootProcessId();
				CESERVER_REQ* pOut = ExecuteGuiCmd(gState.realConWnd_, pIn, gState.realConWnd_);
				ExecuteFreeResult(pIn);
				ExecuteFreeResult(pOut);
			}
		}

		gpSrv->dwConsoleInMode = LOWORD(ldwConsoleMode); lbChanged = TRUE;
	}

	// ConsoleOutMode
	ldwConsoleMode = 0;
	DEBUGTEST(lbConModRc =)
	GetConsoleMode(hOut, &ldwConsoleMode);
	if (gpSrv->dwConsoleOutMode != LOWORD(ldwConsoleMode))
	{
		_ASSERTE(LOWORD(ldwConsoleMode) == ldwConsoleMode);
		LogModeChange(L"ConOutMode", gpSrv->dwConsoleOutMode, ldwConsoleMode);
		gpSrv->dwConsoleOutMode = LOWORD(ldwConsoleMode); lbChanged = TRUE;
	}

	MCHKHEAP;

	if (!MyGetConsoleScreenBufferInfo(hOut, &lsbi))
	{
		DWORD dwErr = GetLastError();
		_ASSERTE(FALSE && "!!! ReadConsole::MyGetConsoleScreenBufferInfo failed !!!");

		this->consoleInfo.dwSbiRc = dwErr ? dwErr : -1;

		//liRc = -1;
		return -1;
	}
	else
	{
		DWORD nCurScroll = (gnBufferHeight ? rbs_Vert : 0) | (gnBufferWidth ? rbs_Horz : 0);
		DWORD nNewScroll = 0;
		int TextWidth = 0, TextHeight = 0;
		short nMaxWidth = -1, nMaxHeight = -1;
		BOOL bSuccess = ::GetConWindowSize(lsbi, gcrVisibleSize.X, gcrVisibleSize.Y, nCurScroll, &TextWidth, &TextHeight, &nNewScroll);

		// Use "visible" buffer positions, what user sees at the moment in the ConEmu window
		// These values are stored in RConBuffer::con.TopLeft
		if (bSuccess)
		{
			//rgn = this->consoleInfo.sbi.srWindow;
			if (!(nNewScroll & rbs_Horz))
			{
				lsbi.srWindow.Left = 0;
				nMaxWidth = std::max<int>(gpSrv->pConsole->hdr.crMaxConSize.X, (lsbi.srWindow.Right-lsbi.srWindow.Left+1));
				lsbi.srWindow.Right = std::min<int>(nMaxWidth, lsbi.dwSize.X-1);
			}

			if (!(nNewScroll & rbs_Vert))
			{
				lsbi.srWindow.Top = 0;
				nMaxHeight = std::max<int>(gpSrv->pConsole->hdr.crMaxConSize.Y, (lsbi.srWindow.Bottom-lsbi.srWindow.Top+1));
				lsbi.srWindow.Bottom = std::min<int>(nMaxHeight, lsbi.dwSize.Y-1);
			}
		}

		if (memcmp(&this->consoleInfo.sbi, &lsbi, sizeof(this->consoleInfo.sbi)))
		{
			InputLogger::Log(InputLogger::Event::evt_ConSbiChanged);

			_ASSERTE(lsbi.srWindow.Left == 0);
			/*
			//Issue 373: при запуске wmic устанавливается ШИРИНА буфера в 1500 символов
			//           пока ConEmu не поддерживает горизонтальную прокрутку - игнорим,
			//           будем показывать только видимую область окна
			if (lsbi.srWindow.Right != (lsbi.dwSize.X - 1))
			{
				//_ASSERTE(lsbi.srWindow.Right == (lsbi.dwSize.X - 1)); -- ругаться пока не будем
				lsbi.dwSize.X = (lsbi.srWindow.Right - lsbi.srWindow.Left + 1);
			}
			*/

			// Консольное приложение могло изменить размер буфера
			if (!NTVDMACTIVE)  // НЕ при запущенном 16битном приложении - там мы все жестко фиксируем, иначе съезжает размер при закрытии 16бит
			{
				// ширина
				if ((lsbi.srWindow.Left == 0  // или окно соответствует полному буферу
				        && lsbi.dwSize.X == (lsbi.srWindow.Right - lsbi.srWindow.Left + 1)))
				{
					// Это значит, что прокрутки нет, и консольное приложение изменило размер буфера
					gnBufferWidth = 0;
					gcrVisibleSize.X = lsbi.dwSize.X;
				}
				// высота
				if ((lsbi.srWindow.Top == 0  // или окно соответствует полному буферу
				        && lsbi.dwSize.Y == (lsbi.srWindow.Bottom - lsbi.srWindow.Top + 1)))
				{
					// Это значит, что прокрутки нет, и консольное приложение изменило размер буфера
					gnBufferHeight = 0;
					gcrVisibleSize.Y = lsbi.dwSize.Y;
				}

				if (lsbi.dwSize.X != this->consoleInfo.sbi.dwSize.X
				        || (lsbi.srWindow.Bottom - lsbi.srWindow.Top) != (this->consoleInfo.sbi.srWindow.Bottom - this->consoleInfo.sbi.srWindow.Top))
				{
					// При изменении размера видимой области - обязательно передернуть данные
					gpSrv->pConsole->bDataChanged = TRUE;
				}
			}

			#ifdef ASSERT_UNWANTED_SIZE
			COORD crReq = gpSrv->crReqSizeNewSize;
			COORD crSize = lsbi.dwSize;

			if (crReq.X != crSize.X && !gpSrv->dwDisplayMode && !IsZoomed(gState.realConWnd_))
			{
				// Только если не было запрошено изменение размера консоли!
				if (!gpSrv->nRequestChangeSize)
				{
					LogSize(nullptr, ":ReadConsoleInfo(AssertWidth)");
					wchar_t /*szTitle[64],*/ szInfo[128];
					//swprintf_c(szTitle, L"ConEmuC, PID=%i", GetCurrentProcessId());
					swprintf_c(szInfo, L"Size req by server: {%ix%i},  Current size: {%ix%i}",
					          crReq.X, crReq.Y, crSize.X, crSize.Y);
					//MessageBox(nullptr, szInfo, szTitle, MB_OK|MB_SETFOREGROUND|MB_SYSTEMMODAL);
					MY_ASSERT_EXPR(FALSE, szInfo, false);
				}
			}
			#endif

			if (gpLogSize) LogSize(nullptr, 0, ":ReadConsoleInfo");

			this->consoleInfo.sbi = lsbi;
			lbChanged = TRUE;
		}
	}

	if (!gnBufferHeight)
	{
		int nWndHeight = (this->consoleInfo.sbi.srWindow.Bottom - this->consoleInfo.sbi.srWindow.Top + 1);

		if (this->consoleInfo.sbi.dwSize.Y > (std::max<int>(gcrVisibleSize.Y, nWndHeight)+200)
		        || ((gpSrv->nRequestChangeSize > 0) && gpSrv->nReqSizeBufferHeight))
		{
			// Приложение изменило размер буфера!

			if (!gpSrv->nReqSizeBufferHeight)
			{
				//#ifdef _DEBUG
				//EmergencyShow(gState.realConWnd);
				//#endif
				WARNING("###: Приложение изменило вертикальный размер буфера");
				if (this->consoleInfo.sbi.dwSize.Y > 200)
				{
					//_ASSERTE(this->consoleInfo.sbi.dwSize.Y <= 200);
					DEBUGLOGSIZE(L"!!! this->consoleInfo.sbi.dwSize.Y > 200 !!! in ConEmuC.ReloadConsoleInfo\n");
				}
				gpSrv->nReqSizeBufferHeight = this->consoleInfo.sbi.dwSize.Y;
			}

			gnBufferHeight = gpSrv->nReqSizeBufferHeight;
		}

		//	Sleep(10);
		//} else {
		//	break; // OK
	}

	// Лучше всегда делать, чтобы данные были гарантированно актуальные
	gpSrv->pConsole->hdr.hConWnd = gpSrv->pConsole->ConState.hConWnd = gState.realConWnd_;
	_ASSERTE(this->dwMainServerPID!=0);
	gpSrv->pConsole->hdr.nServerPID = this->dwMainServerPID;
	gpSrv->pConsole->hdr.nAltServerPID = (gState.runMode_==RunMode::AltServer) ? GetCurrentProcessId() : this->dwAltServerPID;
	//gpSrv->pConsole->info.nInputTID = gpSrv->dwInputThreadId;
	gpSrv->pConsole->ConState.nReserved0 = 0;
	gpSrv->pConsole->ConState.dwCiSize = sizeof(gpSrv->ci);
	gpSrv->pConsole->ConState.ci = gpSrv->ci;
	gpSrv->pConsole->ConState.dwConsoleCP = gpSrv->dwConsoleCP;
	gpSrv->pConsole->ConState.dwConsoleOutputCP = gpSrv->dwConsoleOutputCP;
	gpSrv->pConsole->ConState.dwConsoleInMode = gpSrv->dwConsoleInMode;
	gpSrv->pConsole->ConState.dwConsoleOutMode = gpSrv->dwConsoleOutMode;
	gpSrv->pConsole->ConState.dwSbiSize = sizeof(this->consoleInfo.sbi);
	gpSrv->pConsole->ConState.sbi = this->consoleInfo.sbi;
	gpSrv->pConsole->ConState.ConsolePalette = gpSrv->ConsolePalette;


	// Если есть возможность (WinXP+) - получим реальный список процессов из консоли
	//CheckProcessCount(); -- уже должно быть вызвано !!!
	//2010-05-26 Изменения в списке процессов не приходили в GUI до любого чиха в консоль.
	#ifdef _DEBUG
	_ASSERTE(gpWorker->Processes().pnProcesses.size() > 0);  // NOLINT(readability-container-size-empty)
	if (!gpWorker->Processes().nProcessCount)
	{
		_ASSERTE(gpWorker->Processes().nProcessCount); //CheckProcessCount(); -- must be already initialized !!!
	}
	#endif

	auto& conState = gpSrv->pConsole->ConState;
	if (gpWorker->Processes().GetProcesses(conState.nProcesses, countof(conState.nProcesses), dwMainServerPID))
	{
		// Process list was changed
		lbChanged = TRUE;
	}

	return lbChanged ? 1 : 0;
}

// !! test test !!

// !!! Засечка времени чтения данных консоли показала, что само чтение занимает мизерное время
// !!! Повтор 1000 раз чтения буфера размером 140x76 занимает 100мс.
// !!! Чтение 1000 раз по строке (140x1) занимает 30мс.
// !!! Резюме. Во избежание усложнения кода и глюков отрисовки читаем всегда все полностью.
// !!! А выигрыш за счет частичного чтения - незначителен и создает риск некорректного чтения.


bool WorkerServer::ReadConsoleData()
{
	// Need to block all requests to output buffer in other threads
	MSectionLockSimple csRead = LockConsoleReaders(LOCK_READOUTPUT_TIMEOUT);

	BOOL lbRc = FALSE, lbChanged = FALSE;
	bool lbDataChanged = false;
#ifdef _DEBUG
	CONSOLE_SCREEN_BUFFER_INFO dbgSbi = this->consoleInfo.sbi;
#endif
	HANDLE hOut = nullptr;
	//USHORT TextWidth=0, TextHeight=0;
	DWORD TextLen=0;
	COORD bufSize; //, bufCoord;
	SMALL_RECT rgn;
	DWORD nCurSize, nHdrSize;
	// -- начинаем потихоньку горизонтальную прокрутку
	_ASSERTE(this->consoleInfo.sbi.srWindow.Left == 0); // этот пока оставим
	//_ASSERTE(this->consoleInfo.sbi.srWindow.Right == (this->consoleInfo.sbi.dwSize.X - 1));
	DWORD nCurScroll = (gnBufferHeight ? rbs_Vert : 0) | (gnBufferWidth ? rbs_Horz : 0);
	DWORD nNewScroll = 0;
	int TextWidth = 0, TextHeight = 0;
	short nMaxWidth = -1, nMaxHeight = -1;
	char sFailedInfo[128];

	// sbi считывается в ReadConsoleInfo
	BOOL bSuccess = ::GetConWindowSize(this->consoleInfo.sbi, gcrVisibleSize.X, gcrVisibleSize.Y, nCurScroll, &TextWidth, &TextHeight, &nNewScroll);

	UNREFERENCED_PARAMETER(bSuccess);
	//TextWidth  = this->consoleInfo.sbi.dwSize.X;
	//TextHeight = (this->consoleInfo.sbi.srWindow.Bottom - this->consoleInfo.sbi.srWindow.Top + 1);
	TextLen = TextWidth * TextHeight;

	if (!gpSrv->pConsole)
	{
		_ASSERTE(gpSrv->pConsole!=nullptr);
		LogString("gpSrv->pConsole == nullptr");
		goto wrap;
	}

	//rgn = this->consoleInfo.sbi.srWindow;
	if (nNewScroll & rbs_Horz)
	{
		rgn.Left = this->consoleInfo.sbi.srWindow.Left;
		rgn.Right = std::min<int>(this->consoleInfo.sbi.srWindow.Left+TextWidth,this->consoleInfo.sbi.dwSize.X)-1;
	}
	else
	{
		rgn.Left = 0;
		nMaxWidth = std::max<int>(gpSrv->pConsole->hdr.crMaxConSize.X,(this->consoleInfo.sbi.srWindow.Right-this->consoleInfo.sbi.srWindow.Left+1));
		rgn.Right = std::min<int>(nMaxWidth,(this->consoleInfo.sbi.dwSize.X-1));
	}

	if (nNewScroll & rbs_Vert)
	{
		rgn.Top = this->consoleInfo.sbi.srWindow.Top;
		rgn.Bottom = std::min<int>(this->consoleInfo.sbi.srWindow.Top+TextHeight,this->consoleInfo.sbi.dwSize.Y)-1;
	}
	else
	{
		rgn.Top = 0;
		nMaxHeight = std::max<int>(gpSrv->pConsole->hdr.crMaxConSize.Y,(this->consoleInfo.sbi.srWindow.Bottom-this->consoleInfo.sbi.srWindow.Top+1));
		rgn.Bottom = std::min<int>(nMaxHeight,(this->consoleInfo.sbi.dwSize.Y-1));
	}


	if (!TextWidth || !TextHeight)
	{
		_ASSERTE(TextWidth && TextHeight);
		goto wrap;
	}

	nCurSize = TextLen * sizeof(CHAR_INFO);
	nHdrSize = sizeof(CESERVER_REQ_CONINFO_FULL)-sizeof(CHAR_INFO);

	if (gpSrv->pConsole->cbMaxSize < (nCurSize+nHdrSize))
	{
		_ASSERTE(gpSrv->pConsole && gpSrv->pConsole->cbMaxSize >= (nCurSize+nHdrSize));

		sprintf_c(sFailedInfo, "ReadConsoleData FAIL: MaxSize(%u) < CurSize(%u), TextSize(%ux%u)", gpSrv->pConsole->cbMaxSize, (nCurSize+nHdrSize), TextWidth, TextHeight);
		LogString(sFailedInfo);

		TextHeight = (gpSrv->pConsole->ConState.crMaxSize.X * gpSrv->pConsole->ConState.crMaxSize.Y - (TextWidth-1)) / TextWidth;

		if (TextHeight <= 0)
		{
			_ASSERTE(TextHeight > 0);
			goto wrap;
		}

		rgn.Bottom = std::min<int>(rgn.Bottom,(rgn.Top+TextHeight-1));
		TextLen = TextWidth * TextHeight;
		nCurSize = TextLen * sizeof(CHAR_INFO);
		// Если MapFile еще не создавался, или был увеличен размер консоли
		//if (!RecreateMapData())
		//{
		//	// Раз не удалось пересоздать MapFile - то и дергаться не нужно...
		//	goto wrap;
		//}
		//_ASSERTE(gpSrv->nConsoleDataSize >= (nCurSize+nHdrSize));
	}

	if (gpSrv->pConsole->ConState.crWindow.X != TextWidth || gpSrv->pConsole->ConState.crWindow.Y != TextHeight)
		lbDataChanged = true;
	gpSrv->pConsole->ConState.crWindow = MakeCoord(TextWidth, TextHeight);

	gpSrv->pConsole->ConState.sbi.srWindow = rgn;

	hOut = (HANDLE)ghConOut;

	if (hOut == INVALID_HANDLE_VALUE)
		hOut = GetStdHandle(STD_OUTPUT_HANDLE);

	lbRc = FALSE;

	//if (nCurSize <= MAX_CONREAD_SIZE)
	{
		bufSize.X = TextWidth; bufSize.Y = TextHeight;
		//bufCoord.X = 0; bufCoord.Y = 0;
		//rgn = this->consoleInfo.sbi.srWindow;

		//if (ReadConsoleOutput(hOut, gpSrv->pConsoleDataCopy, bufSize, bufCoord, &rgn))
		if (MyReadConsoleOutput(hOut, gpSrv->pConsoleDataCopy, bufSize, rgn))
		{
			lbRc = TRUE;

			//gh-1164, gh-1216, gh-1219: workaround for Window 10 conhost bug
			if (IsWin10())
			{
				WORD defAttr = this->consoleInfo.sbi.wAttributes;
				//if (CONFORECOLOR(defAttr) == CONBACKCOLOR(defAttr))
				//	defAttr = 7; // Really? There would be nothing visible at all...
				int max_cells = bufSize.X * bufSize.Y;
				// After shrinking the width of the console, due to the bug in conhost
				// all contents receive attribute = 0, so even the prompt on the current
				// line is displayed as "black on black".
				// However any new input on the line receives proper color,
				// so we can't decide attr=0 is or is not expected on that line
				for (int i = 0; i < max_cells && !gpSrv->pConsoleDataCopy[i].Attributes; ++i)
				{
					gpSrv->pConsoleDataCopy[i].Attributes = defAttr;
				}
			}
		}
	}

	//if (!lbRc)
	//{
	//	// Придется читать построчно
	//	bufSize.X = TextWidth; bufSize.Y = 1;
	//	bufCoord.X = 0; bufCoord.Y = 0;
	//	//rgn = this->consoleInfo.sbi.srWindow;
	//	CHAR_INFO* pLine = gpSrv->pConsoleDataCopy;

	//	for(int y = 0; y < (int)TextHeight; y++, rgn.Top++, pLine+=TextWidth)
	//	{
	//		rgn.Bottom = rgn.Top;
	//		ReadConsoleOutput(hOut, pLine, bufSize, bufCoord, &rgn);
	//	}
	//}

	// низя - он уже установлен в максимальное значение
	//gpSrv->pConsoleDataCopy->crBufSize.X = TextWidth;
	//gpSrv->pConsoleDataCopy->crBufSize.Y = TextHeight;

	// Not only the contents may be changed, but window height too
	// In result, on height decrease ConEmu content was erased on update
	if (!lbDataChanged)
		lbDataChanged = (memcmp(gpSrv->pConsole->data, gpSrv->pConsoleDataCopy, nCurSize) != 0);
	if (lbDataChanged)
	{
		InputLogger::Log(InputLogger::Event::evt_ConDataChanged);
		memmove(gpSrv->pConsole->data, gpSrv->pConsoleDataCopy, nCurSize);
		gpSrv->pConsole->bDataChanged = TRUE; // TRUE уже может быть с прошлого раза, не сбрасывать в FALSE
		lbChanged = TRUE;
		LogString("ReadConsoleData: content was changed");
	}
	else if (gState.bWasReattached_)
	{
		gpSrv->pConsole->bDataChanged = TRUE;
	}


	if (!lbChanged && this->pColorerMapping)
	{
		AnnotationHeader ahdr;
		if (this->pColorerMapping->GetTo(&ahdr, sizeof(ahdr)))
		{
			if (this->ColorerHdr.flushCounter != ahdr.flushCounter && !ahdr.locked)
			{
				this->ColorerHdr = ahdr;
				gpSrv->pConsole->bDataChanged = TRUE; // TRUE уже может быть с прошлого раза, не сбрасывать в FALSE
				lbChanged = TRUE;
			}
		}
	}


	// низя - он уже установлен в максимальное значение
	//gpSrv->pConsoleData->crBufSize = gpSrv->pConsoleDataCopy->crBufSize;
wrap:
	//if (lbChanged)
	//	gpSrv->pConsole->bDataChanged = TRUE;
	UNREFERENCED_PARAMETER(nMaxWidth); UNREFERENCED_PARAMETER(nMaxHeight);
	return lbChanged;
}




// abForceSend выставляется в TRUE, чтобы гарантированно
// передернуть GUI по таймауту (не реже 1 сек).
BOOL WorkerServer::ReloadFullConsoleInfo(BOOL abForceSend)
{
	if (CheckHwFullScreen())
	{
		LogString("ReloadFullConsoleInfo was skipped due to CONSOLE_FULLSCREEN_HARDWARE");
		return FALSE;
	}

	BOOL lbChanged = abForceSend;
	BOOL lbDataChanged = abForceSend;
	DWORD dwCurThId = GetCurrentThreadId();

	// Должен вызываться ТОЛЬКО в нити (RefreshThread)
	// Иначе возможны блокировки
	if (abForceSend && this->dwRefreshThread && dwCurThId != this->dwRefreshThread)
	{
		//ResetEvent(gpSrv->hDataReadyEvent);
		gpSrv->bForceConsoleRead = TRUE;

		ResetEvent(gpSrv->hRefreshDoneEvent);
		SetEvent(gpSrv->hRefreshEvent);
		// Ожидание, пока сработает RefreshThread
		HANDLE hEvents[2] = {ghQuitEvent, gpSrv->hRefreshDoneEvent};
		DWORD nWait = WaitForMultipleObjects(2, hEvents, FALSE, RELOAD_INFO_TIMEOUT);
		lbChanged = (nWait == (WAIT_OBJECT_0+1));

		gpSrv->bForceConsoleRead = FALSE;

		return lbChanged;
	}

#ifdef _DEBUG
	DWORD nPacketID = gpSrv->pConsole->ConState.nPacketId;
#endif

#ifdef USE_COMMIT_EVENT
	if (gpSrv->hExtConsoleCommit)
	{
		WaitForSingleObject(gpSrv->hExtConsoleCommit, EXTCONCOMMIT_TIMEOUT);
	}
#endif

	if (abForceSend)
		gpSrv->pConsole->bDataChanged = TRUE;

	DWORD nTick1 = GetTickCount(), nTick2 = 0, nTick3 = 0, nTick4 = 0, nTick5 = 0;

	// Need to block all requests to output buffer in other threads
	MSectionLockSimple csRead = LockConsoleReaders(LOCK_READOUTPUT_TIMEOUT);

	nTick2 = GetTickCount();

	// #SERVER remove this
	auto& server = WorkerServer::Instance();

	// Read sizes flags and other information
	const int iInfoRc = server.ReadConsoleInfo();

	nTick3 = GetTickCount();

	if (iInfoRc == -1)
	{
		lbChanged = FALSE;
	}
	else
	{
		if (iInfoRc == 1)
			lbChanged = TRUE;

		// Read chars and attributes for visible (or locked) area
		if (server.ReadConsoleData())
			lbChanged = lbDataChanged = TRUE;

		nTick4 = GetTickCount();

		if (lbChanged && !gpSrv->pConsole->hdr.bDataReady)
		{
			gpSrv->pConsole->hdr.bDataReady = TRUE;
		}

		//if (memcmp(&(gpSrv->pConsole->hdr), gpSrv->pConsoleMap->Ptr(), gpSrv->pConsole->hdr.cbSize))
		int iMapCmp = Compare(&gpSrv->pConsole->hdr, gpSrv->pConsoleMap->Ptr());
		if (iMapCmp || gState.bWasReattached_)
		{
			lbChanged = TRUE;

			UpdateConsoleMapHeader(L"ReloadFullConsoleInfo");
		}

		if (lbChanged)
		{
			// Накрутить счетчик и Tick
			//gpSrv->pConsole->bChanged = TRUE;
			//if (lbDataChanged)
			gpSrv->pConsole->ConState.nPacketId++;
			gpSrv->pConsole->ConState.nSrvUpdateTick = GetTickCount();

			if (gpSrv->hDataReadyEvent)
				SetEvent(gpSrv->hDataReadyEvent);

			//if (nPacketID == gpSrv->pConsole->info.nPacketId) {
			//	gpSrv->pConsole->info.nPacketId++;
			//	TODO("Можно заменить на multimedia tick");
			//	gpSrv->pConsole->info.nSrvUpdateTick = GetTickCount();
			//	//			gpSrv->nFarInfoLastIdx = gpSrv->pConsole->info.nFarInfoIdx;
			//}
		}

		nTick5 = GetTickCount();
	}

	csRead.Unlock();

	UNREFERENCED_PARAMETER(nTick1);
	UNREFERENCED_PARAMETER(nTick2);
	UNREFERENCED_PARAMETER(nTick3);
	UNREFERENCED_PARAMETER(nTick4);
	UNREFERENCED_PARAMETER(nTick5);
	return lbChanged;
}

// BufferHeight  - высота БУФЕРА (0 - без прокрутки)
// crNewSize     - размер ОКНА (ширина окна == ширине буфера)
// rNewRect      - для (BufferHeight!=0) определяет new upper-left and lower-right corners of the window
//	!!! rNewRect по идее вообще не нужен, за блокировку при прокрутке отвечает nSendTopLine
// #PTY move to Server part
bool WorkerServer::SetConsoleSize(USHORT BufferHeight, COORD crNewSize, SMALL_RECT rNewRect, LPCSTR asLabel, bool bForceWriteLog)
{
	_ASSERTE(gState.realConWnd_);
	_ASSERTE(BufferHeight == 0 || BufferHeight > crNewSize.Y); // Otherwise - it will be NOT a bufferheight...
	PreConsoleSize(crNewSize);

	if (!gState.realConWnd_)
	{
		DEBUGSTRSIZE(L"SetConsoleSize: Skipped due to gState.realConWnd==NULL");
		return FALSE;
	}

	if (gpWorker->CheckHwFullScreen())
	{
		DEBUGSTRSIZE(L"SetConsoleSize was skipped due to CONSOLE_FULLSCREEN_HARDWARE");
		LogString("SetConsoleSize was skipped due to CONSOLE_FULLSCREEN_HARDWARE");
		return FALSE;
	}

	const DWORD dwCurThId = GetCurrentThreadId();
	DWORD dwWait = 0;
	DWORD dwErr = 0;

	if ((gState.runMode_ == RunMode::Server) || (gState.runMode_ == RunMode::AltServer))
	{
		// Запомним то, что последний раз установил сервер. пригодится
		gpSrv->nReqSizeBufferHeight = BufferHeight;
		gpSrv->crReqSizeNewSize = crNewSize;
		_ASSERTE(gpSrv->crReqSizeNewSize.X != 0);
		WARNING("выпилить gpSrv->rReqSizeNewRect и rNewRect");
		gpSrv->rReqSizeNewRect = rNewRect;
		gpSrv->sReqSizeLabel = asLabel;
		gpSrv->bReqSizeForceLog = bForceWriteLog;

		// Ресайз выполнять только в нити RefreshThread. Поэтому если нить другая - ждем...
		if (this->dwRefreshThread && dwCurThId != this->dwRefreshThread)
		{
			DEBUGSTRSIZE(L"SetConsoleSize: Waiting for RefreshThread");

			ResetEvent(gpSrv->hReqSizeChanged);
			if (InterlockedIncrement(&gpSrv->nRequestChangeSize) <= 0)
			{
				_ASSERTE(FALSE && "gpSrv->nRequestChangeSize has invalid value");
				gpSrv->nRequestChangeSize = 1;
			}
			// Ожидание, пока сработает RefreshThread
			HANDLE hEvents[2] = { ghQuitEvent, gpSrv->hReqSizeChanged };
			DWORD nSizeTimeout = REQSIZE_TIMEOUT;

#ifdef _DEBUG
			if (IsDebuggerPresent())
				nSizeTimeout = INFINITE;
#endif

			dwWait = WaitForMultipleObjects(2, hEvents, FALSE, nSizeTimeout);

			// Generally, it must be decremented by RefreshThread...
			if ((dwWait == WAIT_TIMEOUT) && (gpSrv->nRequestChangeSize > 0))
			{
				InterlockedDecrement(&gpSrv->nRequestChangeSize);
			}
			// Checking invalid value...
			if (gpSrv->nRequestChangeSize < 0)
			{
				// Decremented by RefreshThread and CurrentThread? Must not be...
				_ASSERTE(gpSrv->nRequestChangeSize >= 0);
				gpSrv->nRequestChangeSize = 0;
			}

			if (dwWait == WAIT_OBJECT_0)
			{
				// ghQuitEvent !!
				return FALSE;
			}

			if (dwWait == (WAIT_OBJECT_0 + 1))
			{
				return gpSrv->bRequestChangeSizeResult;
			}

			// ?? Может быть стоит самим попробовать?
			return FALSE;
		}
	}

	DEBUGSTRSIZE(L"SetConsoleSize: Started");

	MSectionLock rcs;
	if (gpSrv->pReqSizeSection && !rcs.Lock(gpSrv->pReqSizeSection, TRUE, 30000))
	{
		DEBUGSTRSIZE(L"SetConsoleSize: !!!Failed to lock section!!!");
		_ASSERTE(FALSE);
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	if (gpLogSize) LogSize(&crNewSize, BufferHeight, asLabel);

	_ASSERTE(crNewSize.X >= MIN_CON_WIDTH && crNewSize.Y >= MIN_CON_HEIGHT);

	// Проверка минимального размера
	if (crNewSize.X </*4*/MIN_CON_WIDTH)
		crNewSize.X = /*4*/MIN_CON_WIDTH;

	if (crNewSize.Y </*3*/MIN_CON_HEIGHT)
		crNewSize.Y = /*3*/MIN_CON_HEIGHT;

	CONSOLE_SCREEN_BUFFER_INFO csbi = {};

	// Нам нужно реальное состояние консоли, чтобы не поломать ее вид после ресайза
	if (!GetConsoleScreenBufferInfo(ghConOut, &csbi))
	{
		const DWORD nErrCode = GetLastError();
		DEBUGSTRSIZE(L"SetConsoleSize: !!!GetConsoleScreenBufferInfo failed!!!");
		_ASSERTE(FALSE && "GetConsoleScreenBufferInfo was failed");
		SetLastError(nErrCode ? nErrCode : ERROR_INVALID_HANDLE);
		return FALSE;
	}

	BOOL lbRc = TRUE;

	if (!AdaptConsoleFontSize(crNewSize))
	{
		DEBUGSTRSIZE(L"SetConsoleSize: !!!AdaptConsoleFontSize failed!!!");
		lbRc = FALSE;
		goto wrap;
	}

	// Делаем это ПОСЛЕ MyGetConsoleScreenBufferInfo, т.к. некоторые коррекции размера окна
	// она делает ориентируясь на gnBufferHeight
	gnBufferHeight = BufferHeight;

	// Размер видимой области (слишком большой?)
	PreConsoleSize(crNewSize.X, crNewSize.Y);
	gcrVisibleSize = crNewSize;

	if (gState.runMode_ == RunMode::Server || gState.runMode_ == RunMode::AltServer)
		UpdateConsoleMapHeader(L"SetConsoleSize"); // Обновить pConsoleMap.crLockedVisible

	if (gnBufferHeight)
	{
		// В режиме BufferHeight - высота ДОЛЖНА быть больше допустимого размера окна консоли
		// иначе мы запутаемся при проверках "буферный ли это режим"...
		if (gnBufferHeight <= (csbi.dwMaximumWindowSize.Y * 12 / 10))
			gnBufferHeight = std::max<int>(300, (csbi.dwMaximumWindowSize.Y * 12 / 10));

		// В режиме cmd сразу уменьшим максимальный FPS
		gpSrv->dwLastUserTick = GetTickCount() - USER_IDLE_TIMEOUT - 1;
	}

	// The resize itself
	if (BufferHeight == 0)
	{
		// No buffer in the console
		lbRc = ApplyConsoleSizeSimple(crNewSize, csbi, dwErr, bForceWriteLog);
	}
	else
	{
		// Начался ресайз для BufferHeight
		lbRc = ApplyConsoleSizeBuffer(gnBufferHeight, crNewSize, csbi, dwErr, bForceWriteLog);
	}

#ifdef _DEBUG
	DEBUGSTRSIZE(lbRc ? L"SetConsoleSize: FINISHED" : L"SetConsoleSize: !!! FAILED !!!");
#endif

wrap:
	gpSrv->bRequestChangeSizeResult = lbRc;

	if ((gState.runMode_ == RunMode::Server) && gpSrv->hRefreshEvent)
	{
		SetEvent(gpSrv->hRefreshEvent);
	}

	return lbRc;
}

bool WorkerServer::AdaptConsoleFontSize(const COORD& crNewSize)
{
	bool lbRc = true;
	char szLogInfo[128];

	// Minimum console size
	int curSizeY = -1, curSizeX = -1;
	wchar_t sFontName[LF_FACESIZE] = L"";
	bool bCanChangeFontSize = false; // Vista+ only
	if (apiGetConsoleFontSize(ghConOut, curSizeY, curSizeX, sFontName) && curSizeY && curSizeX)
	{
		bCanChangeFontSize = true;
		int nMinY = GetSystemMetrics(SM_CYMIN) - GetSystemMetrics(SM_CYSIZEFRAME) - GetSystemMetrics(SM_CYCAPTION);
		int nMinX = GetSystemMetrics(SM_CXMIN) - 2*GetSystemMetrics(SM_CXSIZEFRAME);
		if ((nMinX > 0) && (nMinY > 0))
		{
			// Теперь прикинуть, какой размер шрифта нам нужен
			int minSizeY = (nMinY / curSizeY);
			int minSizeX = (nMinX / curSizeX);
			if ((minSizeX > crNewSize.X) || (minSizeY > crNewSize.Y))
			{
				if (gpLogSize)
				{
					sprintf_c(szLogInfo, "Need to reduce minSize. Cur={%i,%i}, Req={%i,%i}", minSizeX, minSizeY, crNewSize.X, crNewSize.Y);
					LogString(szLogInfo);
				}

				apiFixFontSizeForBufferSize(ghConOut, crNewSize, szLogInfo, countof(szLogInfo));
				LogString(szLogInfo);

				apiGetConsoleFontSize(ghConOut, curSizeY, curSizeX, sFontName);
			}
		}
		if (gpLogSize)
		{
			sprintf_c(szLogInfo, "Console font size H=%i W=%i N=", curSizeY, curSizeX);
			int nLen = lstrlenA(szLogInfo);
			WideCharToMultiByte(CP_UTF8, 0, sFontName, -1, szLogInfo+nLen, countof(szLogInfo)-nLen, NULL, NULL);
			LogFunction(szLogInfo);
		}
	}
	else
	{
		LogFunction(L"Function GetConsoleFontSize is not available");
	}


	RECT rcConPos = {0};
	COORD crMax = MyGetLargestConsoleWindowSize(ghConOut);

	// Если размер превышает допустимый - лучше ничего не делать,
	// иначе получается неприятный эффект при попытке AltEnter:
	// размер окна становится сильно больше чем был, но FullScreen НЕ включается
	//if (crMax.X && crNewSize.X > crMax.X)
	//	crNewSize.X = crMax.X;
	//if (crMax.Y && crNewSize.Y > crMax.Y)
	//	crNewSize.Y = crMax.Y;
	if ((crMax.X && crNewSize.X > crMax.X)
		|| (crMax.Y && crNewSize.Y > crMax.Y))
	{
		if (bCanChangeFontSize)
		{
			BOOL bChangeRc = apiFixFontSizeForBufferSize(ghConOut, crNewSize, szLogInfo, countof(szLogInfo));
			LogString(szLogInfo);

			if (bChangeRc)
			{
				crMax = MyGetLargestConsoleWindowSize(ghConOut);

				if (gpLogSize)
				{
					sprintf_c(szLogInfo, "Largest console size is {%i,%i}", crMax.X, crMax.Y);
					LogString(szLogInfo);
				}
			}

			if (!bChangeRc
				|| (crMax.X && crNewSize.X > crMax.X)
				|| (crMax.Y && crNewSize.Y > crMax.Y))
			{
				lbRc = false;
				LogString("Change console size skipped: can't adapt font");
				goto wrap;
			}
		}
		else
		{
			LogString("Change console size skipped: too large");
			lbRc = false;
			goto wrap;
		}
	}

wrap:
	return lbRc;
}

bool WorkerServer::ApplyConsoleSizeBuffer(
	USHORT BufferHeight, const COORD& crNewSize, const CONSOLE_SCREEN_BUFFER_INFO& csbi, DWORD& dwErr, bool bForceWriteLog)
{
	bool lbRc = true;
	dwErr = 0;

	DEBUGSTRSIZE(L"SetConsoleSize: ApplyConsoleSizeBuffer started");

	RECT rcConPos = {};
	GetWindowRect(gState.realConWnd_, &rcConPos);

	TODO("Horizontal scrolling?");
	COORD crHeight = MakeCoord(crNewSize.X, BufferHeight);
	SMALL_RECT rcTemp = {};

	// По идее (в планах), lbCursorInScreen всегда должен быть true,
	// если только само консольное приложение не выполняет прокрутку.
	// Сам ConEmu должен "крутить" консоль только виртуально, не трогая физический скролл.
	bool lbCursorInScreen = CoordInSmallRect(csbi.dwCursorPosition, csbi.srWindow);
	bool lbScreenAtBottom = (csbi.srWindow.Top > 0) && (csbi.srWindow.Bottom >= (csbi.dwSize.Y - 1));
	bool lbCursorAtBottom = (lbCursorInScreen && (csbi.dwCursorPosition.Y >= (csbi.srWindow.Bottom - 2)));
	SHORT nCursorAtBottom = lbCursorAtBottom ? (csbi.srWindow.Bottom - csbi.dwCursorPosition.Y + 1) : 0;
	SHORT nBottomLine = csbi.srWindow.Bottom;
	SHORT nScreenAtBottom = 0;

	// Прикинуть, где должна будет быть нижняя граница видимой области
	if (!lbScreenAtBottom)
	{
		// Ищем снизу вверх (найти самую нижнюю грязную строку)
		SHORT nTo = lbCursorInScreen ? csbi.dwCursorPosition.Y : csbi.srWindow.Top;
		SHORT nWidth = (csbi.srWindow.Right - csbi.srWindow.Left + 1);
		SHORT nDirtyLine = FindFirstDirtyLine(nBottomLine, nTo, nWidth, csbi.wAttributes);

		// Если удачно
		if (nDirtyLine >= csbi.srWindow.Top && nDirtyLine < csbi.dwSize.Y)
		{
			if (lbCursorInScreen)
			{
				nBottomLine = std::max<int>(nDirtyLine, std::min<int>(csbi.dwCursorPosition.Y + 1/*-*/, csbi.srWindow.Bottom));
			}
			else
			{
				nBottomLine = nDirtyLine;
			}
		}
		nScreenAtBottom = (csbi.srWindow.Bottom - nBottomLine + 1);

		// Чтобы информации НАД курсором не стало меньше чем пустых строк ПОД курсором
		if (lbCursorInScreen)
		{
			if (nScreenAtBottom <= 4)
			{
				SHORT nAboveLines = (crNewSize.Y - nScreenAtBottom);
				if (nAboveLines <= (nScreenAtBottom + 1))
				{
					nCursorAtBottom = std::max<int>(1, crNewSize.Y - nScreenAtBottom - 1);
				}
			}
		}
	}

	SMALL_RECT rNewRect = csbi.srWindow;
	EvalVisibleResizeRect(rNewRect, nBottomLine, crNewSize, lbCursorInScreen, nCursorAtBottom, nScreenAtBottom, csbi);

#if 0
	// Подправим будущую видимую область
	if (csbi.dwSize.Y == (csbi.srWindow.Bottom - csbi.srWindow.Top + 1))
	{
		// Прокрутки сейчас нет, оставляем .Top без изменений!
	}
	// При изменении высоты буфера (если он уже был включен), нужно скорректировать новую видимую область
	else if (rNewRect.Bottom >= (csbi.dwSize.Y - (csbi.srWindow.Bottom - csbi.srWindow.Top)))
	{
		// Считаем, что рабочая область прижата к низу экрана. Нужно подвинуть .Top
		int nBottomLines = (csbi.dwSize.Y - csbi.srWindow.Bottom - 1); // Сколько строк сейчас снизу от видимой области?
		SHORT nTop = BufferHeight - crNewSize.Y - nBottomLines;
		rNewRect.Top = (nTop > 0) ? nTop : 0;
		// .Bottom подправится ниже, перед последним SetConsoleWindowInfo
	}
	else
	{
		// Считаем, что верх рабочей области фиксирован, коррекция не требуется
	}
#endif

	// Если этого не сделать - размер консоли нельзя УМЕНЬШИТЬ
	if (crNewSize.X <= (csbi.srWindow.Right - csbi.srWindow.Left)
		|| crNewSize.Y <= (csbi.srWindow.Bottom - csbi.srWindow.Top))
	{
#if 0
		rcTemp.Left = 0;
		WARNING("А при уменьшении высоты, тащим нижнюю границе окна вверх, Top глючить не будет?");
		rcTemp.Top = std::max(0, (csbi.srWindow.Bottom - crNewSize.Y + 1));
		rcTemp.Right = std::min((crNewSize.X - 1), (csbi.srWindow.Right - csbi.srWindow.Left));
		rcTemp.Bottom = std::min((BufferHeight - 1), (rcTemp.Top + crNewSize.Y - 1));//(csbi.srWindow.Bottom-csbi.srWindow.Top)); //-V592
		_ASSERTE(((rcTemp.Bottom - rcTemp.Top + 1) == crNewSize.Y) && ((rcTemp.Bottom - rcTemp.Top) == (rNewRect.Bottom - rNewRect.Top)));
#endif

		if (!SetConsoleWindowInfo(ghConOut, TRUE, &rNewRect))
		{
			// Last chance to shrink visible area of the console if ConApi was failed
			MoveWindow(gState.realConWnd_, rcConPos.left, rcConPos.top, 1, 1, 1);
		}
	}

	// crHeight, а не crNewSize - там "оконные" размеры
	if (!SetConsoleScreenBufferSize(ghConOut, crHeight))
	{
		lbRc = false;
		dwErr = GetLastError();
	}

	// Особенно в Win10 после "заворота строк",
	// нужно получить новое реальное состояние консоли после изменения буфера
	CONSOLE_SCREEN_BUFFER_INFO csbiNew = {};
	if (GetConsoleScreenBufferInfo(ghConOut, &csbiNew))
	{
		rNewRect = csbiNew.srWindow;
		EvalVisibleResizeRect(rNewRect, nBottomLine, crNewSize, lbCursorAtBottom, nCursorAtBottom, nScreenAtBottom, csbiNew);
	}

#if 0
	// Последняя коррекция видимой области.
	// Левую граница - всегда 0 (горизонтальную прокрутку пока не поддерживаем)
	// Вертикальное положение - пляшем от rNewRect.Top

	rNewRect.Left = 0;
	rNewRect.Right = crHeight.X - 1;

	if (lbScreenAtBottom)
	{
	}
	else if (lbCursorInScreen)
	{
	}
	else
	{
		TODO("Маркеры для блокировки положения в окне после заворота строк в Win10?");
	}

	rNewRect.Bottom = std::min((crHeight.Y - 1), (rNewRect.Top + gcrVisibleSize.Y - 1)); //-V592
#endif

	_ASSERTE((rNewRect.Bottom - rNewRect.Top) < 200);

	if (!SetConsoleWindowInfo(ghConOut, TRUE, &rNewRect))
	{
		dwErr = GetLastError();
	}

	LogSize(NULL, 0, lbRc ? "ApplyConsoleSizeBuffer OK" : "ApplyConsoleSizeBuffer FAIL", bForceWriteLog);

	return lbRc;
}

uint32_t WorkerServer::FindFirstDirtyLine(SHORT anFrom, SHORT anTo, SHORT anWidth, WORD wDefAttrs)
{
	int16_t iFound = anFrom;
	const int16_t iStep = (anTo < anFrom) ? -1 : 1;
	// ReSharper disable once CppLocalVariableMayBeConst
	HANDLE hCon = ghConOut;
	// ReSharper disable once CppJoinDeclarationAndAssignment
	BOOL bReadRc{};
	CHAR_INFO* pch = static_cast<CHAR_INFO*>(calloc(anWidth, sizeof(*pch)));
	const COORD crBufSize = {anWidth, 1};
	const COORD crNil = {};
	SMALL_RECT rcRead = {0, anFrom, anWidth-1, anFrom};
	const BYTE bDefAttr = LOBYTE(wDefAttrs); // Trim to colors only, do not compare extended attributes!

	for (rcRead.Top = anFrom; rcRead.Top != anTo; rcRead.Top += iStep)
	{
		rcRead.Bottom = rcRead.Top;

		InterlockedIncrement(&gnInReadConsoleOutput);
		bReadRc = ReadConsoleOutput(hCon, pch, crBufSize, crNil, &rcRead);
		InterlockedDecrement(&gnInReadConsoleOutput);
		if (!bReadRc)
			break;

		// Is line dirty?
		for (SHORT i = 0; i < anWidth; i++)
		{
			// Non-space char or non-default color/background
			if ((pch[i].Char.UnicodeChar != L' ') || (LOBYTE(pch[i].Attributes) != bDefAttr))
			{
				iFound = rcRead.Top;
				goto wrap;
			}
		}
	}

	iFound = std::min<int16_t>(anTo, anFrom);
wrap:
	SafeFree(pch);
	return static_cast<uint16_t>(iFound);
}

// По идее, rNewRect должен на входе содержать текущую видимую область
void WorkerServer::EvalVisibleResizeRect(SMALL_RECT& rNewRect, SHORT anOldBottom, const COORD& crNewSize,
	bool bCursorInScreen, SHORT nCursorAtBottom, SHORT nScreenAtBottom, const CONSOLE_SCREEN_BUFFER_INFO& csbi)
{
	// Абсолютная (буферная) координата
	const SHORT nMaxX = csbi.dwSize.X - 1, nMaxY = csbi.dwSize.Y - 1;

	// сначала - не трогая rNewRect.Left, вдруг там горизонтальная прокрутка?
	// anWidth - желаемая ширина видимой области
	rNewRect.Right = rNewRect.Left + crNewSize.X - 1;
	// не может выходить за пределы ширины буфера
	if (rNewRect.Right > nMaxX)
	{
		rNewRect.Left = std::max<int>(0, (csbi.dwSize.X - crNewSize.X));
		rNewRect.Right = std::min<int>(nMaxX, (rNewRect.Left + crNewSize.X - 1));
	}

	// Теперь - танцы с вертикалью. Логика такая
	// * Если ДО ресайза все видимые строки были заполнены (кейбар фара внизу экрана) - оставить anOldBottom
	// * Иначе, если курсор был видим
	//   * приоритетно - двигать верхнюю границу видимой области (показывать максимум строк из back-scroll-buffer)
	//   * не допускать, чтобы расстояние между курсором и низом видимой области УМЕНЬШИЛОСЬ до менее чем 2-х строк
	// * Иначе если курсор был НЕ видим
	//   * просто показывать максимум стро из back-scroll-buffer (фиксирую нижнюю границу)

	// BTW, сейчас при ресайзе меняется только ширина csbi.dwSize.X (ну, кроме случаев изменения высоты буфера)

	if ((nScreenAtBottom <= 0) && (nCursorAtBottom <= 0))
	{
		// Все просто, фиксируем нижнюю границу по размеру буфера
		rNewRect.Bottom = csbi.dwSize.Y - 1;
		rNewRect.Top = std::max<int>(0, (rNewRect.Bottom - crNewSize.Y + 1));
	}
	else
	{
		// Значит консоль еще не дошла до низа
		SHORT nRectHeight = (rNewRect.Bottom - rNewRect.Top + 1);

		if (nCursorAtBottom > 0)
		{
			_ASSERTE(nCursorAtBottom <= 3);
			// Оставить строку с курсором "приклеенной" к нижней границе окна (с макс. отступом nCursorAtBottom строк)
			rNewRect.Bottom = std::min<int>(nMaxY, (csbi.dwCursorPosition.Y + nCursorAtBottom - 1));
		}
		// Уменьшение видимой области
		else if (crNewSize.Y < nRectHeight)
		{
			if ((nScreenAtBottom > 0) && (nScreenAtBottom <= 3))
			{
				// Оставить nScreenAtBottom строк (включая) между anOldBottom и низом консоли
				rNewRect.Bottom = std::min<int>(nMaxY, anOldBottom + nScreenAtBottom - 1);
			}
			else if (anOldBottom > (rNewRect.Top + crNewSize.Y - 1))
			{
				// Если нижняя граница приблизилась или перекрыла
				// нашу старую строку (которая была anOldBottom)
				rNewRect.Bottom = std::min<int>(anOldBottom, csbi.dwSize.Y - 1);
			}
			else
			{
				// Иначе - не трогать верхнюю границу
				rNewRect.Bottom = std::min<int>(nMaxY, rNewRect.Top + crNewSize.Y - 1);
			}
			//rNewRect.Top = rNewRect.Bottom-crNewSize.Y+1; // на 0 скорректируем в конце
		}
		// Увеличение видимой области
		else if (crNewSize.Y > nRectHeight)
		{
			if (nScreenAtBottom > 0)
			{
				// Оставить nScreenAtBottom строк (включая) между anOldBottom и низом консоли
				rNewRect.Bottom = std::min<int>(nMaxY, anOldBottom + nScreenAtBottom - 1);
			}
			//rNewRect.Top = rNewRect.Bottom-crNewSize.Y+1; // на 0 скорректируем в конце
		}

		// Но курсор не должен уходить за пределы экрана
		if (bCursorInScreen && (csbi.dwCursorPosition.Y < (rNewRect.Bottom - crNewSize.Y + 1)))
		{
			rNewRect.Bottom = std::max<int>(0, csbi.dwCursorPosition.Y + crNewSize.Y - 1);
		}

		// And top, will be corrected to (>0) below
		rNewRect.Top = rNewRect.Bottom - crNewSize.Y + 1;

		// Проверка на выход за пределы буфера
		if (rNewRect.Bottom > nMaxY)
		{
			rNewRect.Bottom = nMaxY;
			rNewRect.Top = std::max<int>(0, rNewRect.Bottom - crNewSize.Y + 1);
		}
		else if (rNewRect.Top < 0)
		{
			rNewRect.Top = 0;
			rNewRect.Bottom = std::min<int>(nMaxY, rNewRect.Top + crNewSize.Y - 1);
		}
	}

	_ASSERTE((rNewRect.Bottom - rNewRect.Top + 1) == crNewSize.Y);
}

void WorkerServer::RefillConsoleAttributes(const CONSOLE_SCREEN_BUFFER_INFO& csbi5, const WORD wOldText, const WORD wNewText) const
{
	// #AltBuffer No need to process rows below detected dynamic height, use ScrollBuffer instead
	wchar_t szLog[140];
	swprintf_c(szLog, L"RefillConsoleAttributes started Lines=%u Cols=%u Old=x%02X New=x%02X", csbi5.dwSize.Y, csbi5.dwSize.X, wOldText, wNewText);
	LogString(szLog);

	const DWORD nMaxLines = std::max<int>(1, std::min<int>((8000 / csbi5.dwSize.X), csbi5.dwSize.Y));
	WORD* pnAttrs = static_cast<WORD*>(malloc(nMaxLines * csbi5.dwSize.X * sizeof(*pnAttrs)));
	if (!pnAttrs)
	{
		// Memory allocation error
		return;
	}

	const BYTE OldText = LOBYTE(wOldText);
	PerfCounter c_read = {0};
	PerfCounter c_fill = {1};
	MPerfCounter perf(2);

	BOOL b{};
	COORD crRead = {0,0};
	// #Refill Reuse DynamicHeight, just scroll-out contents outside of this height
	while (crRead.Y < csbi5.dwSize.Y)
	{
		const DWORD nReadLn = std::min<int>(nMaxLines, (csbi5.dwSize.Y-crRead.Y));
		DWORD nReady = 0;

		perf.Start(c_read);
		b = ReadConsoleOutputAttribute(ghConOut, pnAttrs, nReadLn * csbi5.dwSize.X, crRead, &nReady);
		perf.Stop(c_read);
		if (!b)
			break;

		bool bStarted = false;
		COORD crFrom = crRead;
		DWORD i = 0, iStarted = 0, iWritten;
		while (i < nReady)
		{
			if (LOBYTE(pnAttrs[i]) == OldText)
			{
				if (!bStarted)
				{
					_ASSERT(crRead.X == 0);
					crFrom.Y = static_cast<SHORT>(crRead.Y + (i / csbi5.dwSize.X));
					crFrom.X = i % csbi5.dwSize.X;
					iStarted = i;
					bStarted = true;
				}
			}
			else
			{
				if (bStarted)
				{
					bStarted = false;
					if (iStarted < i)
					{
						perf.Start(c_fill);
						FillConsoleOutputAttribute(ghConOut, wNewText, i - iStarted, crFrom, &iWritten);
						perf.Stop(c_fill);
					}
				}
			}
			// Next cell checking
			i++;
		}
		// Fill the tail if required
		if (bStarted && (iStarted < i))
		{
			perf.Start(c_fill);
			FillConsoleOutputAttribute(ghConOut, wNewText, i - iStarted, crFrom, &iWritten);
			perf.Stop(c_fill);
		}

		// Next block
		crRead.Y += static_cast<USHORT>(nReadLn);
	}

	free(pnAttrs);

	ULONG l_read_ms, l_read_p;
	const ULONG l_read = perf.GetCounter(c_read.ID, &l_read_p, &l_read_ms, nullptr);
	ULONG l_fill_ms, l_fill_p;
	const ULONG l_fill = perf.GetCounter(c_fill.ID, &l_fill_p, &l_fill_ms, nullptr);
	swprintf_c(szLog, L"RefillConsoleAttributes finished, Reads(%u, %u%%, %ums), Fills(%u, %u%%, %ums)",
		l_read, l_read_p, l_read_ms, l_fill, l_fill_p, l_fill_ms);
	LogString(szLog);
}

SleepIndicatorType WorkerServer::CheckIndicateSleepNum() const
{
	static SleepIndicatorType bCheckIndicateSleepNum = sit_None;
	static DWORD nLastCheckTick = 0;

	if (!nLastCheckTick || ((GetTickCount() - nLastCheckTick) >= 3000))
	{
		wchar_t szVal[32] = L"";
		DWORD nLen = GetEnvironmentVariable(ENV_CONEMU_SLEEP_INDICATE_W, szVal, countof(szVal)-1);
		if (nLen && (nLen < countof(szVal)))
		{
			if (lstrcmpni(szVal, ENV_CONEMU_SLEEP_INDICATE_NUM, lstrlen(ENV_CONEMU_SLEEP_INDICATE_NUM)) == 0)
				bCheckIndicateSleepNum = sit_Num;
			else if (lstrcmpni(szVal, ENV_CONEMU_SLEEP_INDICATE_TTL, lstrlen(ENV_CONEMU_SLEEP_INDICATE_TTL)) == 0)
				bCheckIndicateSleepNum = sit_Title;
			else
				bCheckIndicateSleepNum = sit_None;
		}
		else
		{
			bCheckIndicateSleepNum = sit_None; // Надо, ибо может быть обратный сброс переменной
		}
		nLastCheckTick = GetTickCount();
	}

	return bCheckIndicateSleepNum;
}

void WorkerServer::ShowSleepIndicator(const SleepIndicatorType sleepType, const bool bSleeping) const
{
	switch (sleepType)
	{
	case sit_Num:
		{
			// Num: Sleeping - OFF, Active - ON
			bool bNum = (GetKeyState(VK_NUMLOCK) & 1) == 1;
			if (bNum == bSleeping)
			{
				keybd_event(VK_NUMLOCK, 0, 0, 0);
				keybd_event(VK_NUMLOCK, 0, KEYEVENTF_KEYUP, 0);
			}
		} break;
	case sit_Title:
		{
			const wchar_t szSleepPrefix[] = L"[Sleep] ";
			const int nPrefixLen = lstrlen(szSleepPrefix);
			static wchar_t szTitle[2000] = L"";
			DWORD nLen = GetConsoleTitle(szTitle + nPrefixLen, countof(szTitle) - nPrefixLen);
			bool bOld = (wcsstr(szTitle + nPrefixLen, szSleepPrefix) != nullptr);
			if (bOld && !bSleeping)
			{
				wchar_t* psz;
				while ((psz = wcsstr(szTitle + nPrefixLen, szSleepPrefix)) != nullptr)
				{
					wmemmove(psz, psz + nPrefixLen, wcslen(psz + nPrefixLen) + 1);
				}
				SetConsoleTitle(szTitle + nPrefixLen);
			}
			else if (!bOld && bSleeping)
			{
				wmemmove(szTitle, szSleepPrefix, nPrefixLen);
				SetConsoleTitle(szTitle);
			}
		} break;
	default:
		_ASSERTE(FALSE && "unsupported sleepType");
	}
}

bool WorkerServer::IsRefreshFreezeRequests()
{
	return (nRefreshFreezeRequests.load() > 0);
}

void WorkerServer::FreezeRefreshThread()
{
	MSectionLockSimple csControl;
	csControl.Lock(&this->csRefreshControl, LOCK_REFRESH_CONTROL_TIMEOUT);
	if (!this->hFreezeRefreshThread)
		this->hFreezeRefreshThread = CreateEvent(nullptr, TRUE, FALSE, nullptr);

	if (GetCurrentThreadId() == this->dwRefreshThread)
	{
		_ASSERTE(GetCurrentThreadId() != this->dwRefreshThread);
		return;
	}

	this->nRefreshFreezeRequests.fetch_add(1);
	if (this->hFreezeRefreshThread != nullptr)
		ResetEvent(this->hFreezeRefreshThread);

	csControl.Unlock();

	// wait while refresh thread becomes frozen
	const DWORD nStartTick = GetTickCount();
	DWORD nCurTick = 0;
	DWORD nWait = static_cast<DWORD>(-1);
	HANDLE hWait[] = {this->hRefreshThread, ghQuitEvent};
	while (this->hRefreshThread && (this->nRefreshIsFrozen.load() <= 0))
	{
		nWait = WaitForMultipleObjects(countof(hWait), hWait, FALSE, 100);
		if ((nWait == WAIT_OBJECT_0) || (nWait == (WAIT_OBJECT_0+1)))
			break;
		if (((nCurTick = GetTickCount()) - nStartTick) > LOCK_REFRESH_CONTROL_TIMEOUT)
			break;
	}

	std::ignore = (this->nRefreshIsFrozen.load() > 0) || (this->hRefreshThread == nullptr);
}

void WorkerServer::ThawRefreshThread()
{
	MSectionLockSimple csControl;
	csControl.Lock(&this->csRefreshControl, LOCK_REFRESH_CONTROL_TIMEOUT);

	if (this->nRefreshFreezeRequests.load() > 0)
	{
		this->nRefreshFreezeRequests.fetch_sub(1);
	}
	else
	{
		_ASSERTE(FALSE && "Unbalanced FreezeRefreshThread/ThawRefreshThread calls");
	}

	// decrease counter, if == 0 thaw the thread
	if (this->hFreezeRefreshThread && (this->nRefreshFreezeRequests == 0))
		SetEvent(this->hFreezeRefreshThread);

	std::ignore = (this->nRefreshFreezeRequests == 0);
}

DWORD WorkerServer::GetAltServerPid() const
{
	return this->dwAltServerPID;
}

// ReSharper disable once CppParameterMayBeConst
DWORD WorkerServer::DuplicateHandleForAltServer(HANDLE hSrc, HANDLE& hDup) const
{
	DWORD nDupError;
	if (hAltServer)
	{
		if (hSrc == nullptr || hSrc == INVALID_HANDLE_VALUE)
			nDupError = static_cast<DWORD>(-3);
		else if (!DuplicateHandle(GetCurrentProcess(), hSrc, hAltServer, &hDup, 0, FALSE, DUPLICATE_SAME_ACCESS))
			nDupError = GetLastError();
		else
			nDupError = 0;
	}
	else
	{
		nDupError = static_cast<DWORD>(-2);
	}
	return nDupError;
}

DWORD WorkerServer::GetPrevAltServerPid() const
{
	return dwPrevAltServerPID;
}

void WorkerServer::SetPrevAltServerPid(const DWORD prevAltServerPid)
{
	dwPrevAltServerPID = prevAltServerPid;
}

DWORD WorkerServer::GetRefreshThreadId() const
{
	return dwRefreshThread;
}


DWORD WorkerServer::RefreshThread(LPVOID /*lpvParam*/)
{
	DWORD nWait = 0, nAltWait = 0, nFreezeWait = 0, nThreadWait = 0;

	HANDLE hEvents[4] = {ghQuitEvent, gpSrv->hRefreshEvent};
	DWORD  nEventsBaseCount = 2;
	DWORD  nRefreshEventId = (WAIT_OBJECT_0+1);

	DWORD nDelta = 0;
	DWORD nLastReadTick = 0; //GetTickCount();
	DWORD nLastConHandleTick = GetTickCount();
	BOOL  /*lbEventualChange = FALSE,*/ /*lbForceSend = FALSE,*/ lbChanged = FALSE; //, lbProcessChanged = FALSE;
	DWORD dwTimeout = 10; // периодичность чтения информации об окне (размеров, курсора,...)
	DWORD dwAltTimeout = 100;
	//BOOL  bForceRefreshSetSize = FALSE; // После изменения размера нужно сразу перечитать консоль без задержек
	BOOL lbWasSizeChange = FALSE;
	//BOOL bThaw = TRUE; // Если FALSE - снизить нагрузку на conhost
	BOOL bFellInSleep = FALSE; // Если TRUE - снизить нагрузку на conhost
	BOOL bConsoleActive = (BOOL)-1;
	BOOL bDCWndVisible = (BOOL)-1;
	BOOL bNewActive = (BOOL)-1, bNewFellInSleep = FALSE;
	BOOL ActiveSleepInBg = (gpSrv->guiSettings.Flags & CECF_SleepInBackg);
	BOOL RetardNAPanes = (gpSrv->guiSettings.Flags & CECF_RetardNAPanes);
	BOOL bOurConActive = (BOOL)-1, bOneConActive = (BOOL)-1;
	bool bLowSpeed = false;
	BOOL bOnlyCursorChanged;
	BOOL bSetRefreshDoneEvent;
	DWORD nWaitCursor = 99;
	DWORD nWaitCommit = 99;
	SleepIndicatorType SleepType = sit_None;
	DWORD nLastConsoleActiveTick = 0;
	DWORD nLastConsoleActiveDelta = 0;

	while (TRUE)
	{
		bOnlyCursorChanged = FALSE;
		bSetRefreshDoneEvent = FALSE;
		nWaitCursor = 99;
		nWaitCommit = 99;

		nWait = WAIT_TIMEOUT;
		//lbForceSend = FALSE;
		MCHKHEAP;


		if (this->hFreezeRefreshThread)
		{
			HANDLE hFreeze[2] = {this->hFreezeRefreshThread, ghQuitEvent};
			this->nRefreshIsFrozen.fetch_add(1);
			nFreezeWait = WaitForMultipleObjects(countof(hFreeze), hFreeze, FALSE, INFINITE);
			this->nRefreshIsFrozen.fetch_sub(1);
			if (nFreezeWait == (WAIT_OBJECT_0+1))
				break; // затребовано завершение потока
		}


		if (gpSrv->hWaitForSetConBufThread)
		{
			HANDLE hInEvent = gpSrv->hInWaitForSetConBufThread;
			HANDLE hOutEvent = gpSrv->hOutWaitForSetConBufThread;
			HANDLE hWaitEvent = gpSrv->hWaitForSetConBufThread;
			// Tell server thread, that it is safe to return control to application
			if (hInEvent)
				SetEvent(hInEvent);
			// What we are waiting for...
			HANDLE hThreadWait[] = {ghQuitEvent, hOutEvent, hWaitEvent};
			// To avoid infinite blocking - limit waiting time?
			if (hWaitEvent != INVALID_HANDLE_VALUE)
				nThreadWait = WaitForMultipleObjects(countof(hThreadWait), hThreadWait, FALSE, WAIT_SETCONSCRBUF_MAX_TIMEOUT);
			else
				nThreadWait = WaitForMultipleObjects(countof(hThreadWait)-1, hThreadWait, FALSE, WAIT_SETCONSCRBUF_MIN_TIMEOUT);
			// Done, close handles, they are no longer needed
			if (hInEvent == gpSrv->hInWaitForSetConBufThread) gpSrv->hInWaitForSetConBufThread = nullptr;
			if (hOutEvent == gpSrv->hOutWaitForSetConBufThread) gpSrv->hOutWaitForSetConBufThread = nullptr;
			if (hWaitEvent == gpSrv->hWaitForSetConBufThread) gpSrv->hWaitForSetConBufThread = nullptr;
			SafeCloseHandle(hWaitEvent);
			SafeCloseHandle(hInEvent);
			SafeCloseHandle(hOutEvent);
			UNREFERENCED_PARAMETER(nThreadWait);
			if (nThreadWait == WAIT_OBJECT_0)
				break; // затребовано завершение потока
		}

		// проверка альтернативного сервера
		if (this->hAltServer)
		{
			if (!this->hAltServerChanged)
				this->hAltServerChanged = CreateEvent(nullptr, FALSE, FALSE, nullptr);

			HANDLE hAltWait[3] = {this->hAltServer, this->hAltServerChanged, ghQuitEvent};
			nAltWait = WaitForMultipleObjects(countof(hAltWait), hAltWait, FALSE, dwAltTimeout);

			if ((nAltWait == (WAIT_OBJECT_0+0)) || (nAltWait == (WAIT_OBJECT_0+1)))
			{
				// Если это закрылся AltServer
				if (nAltWait == (WAIT_OBJECT_0+0))
				{
					MSectionLock CsAlt; CsAlt.Lock(gpSrv->csAltSrv, TRUE, 10000);

					if (this->hAltServer)
					{
						HANDLE h = this->hAltServer;
						this->hAltServer = nullptr;
						this->hCloseAltServer = h;

						DWORD nAltServerWasStarted = 0;
						DWORD nAltServerWasStopped = this->dwAltServerPID;
						AltServerInfo info = {};
						if (this->dwAltServerPID)
						{
							// Поскольку текущий сервер завершается - то сразу сбросим PID (его морозить уже не нужно)
							this->dwAltServerPID = 0;
							// Был "предыдущий" альт.сервер?
							if (this->AltServers.Get(nAltServerWasStopped, &info, true/*Remove*/))
							{
								// Переключаемся на "старый" (если был)
								if (info.nPrevPID)
								{
									_ASSERTE(info.hPrev!=nullptr);
									// Перевести нить монитора в обычный режим, закрыть this->hAltServer
									// Активировать альтернативный сервер (повторно), отпустить его нити чтения
									nAltServerWasStarted = info.nPrevPID;
									WorkerServer::Instance().AltServerWasStarted(info.nPrevPID, info.hPrev, true);
								}
							}
							// Обновить мэппинг
							wchar_t szLog[80]; swprintf_c(szLog, L"RefreshThread, new AltServer=%u", this->dwAltServerPID);
							WorkerServer::Instance().UpdateConsoleMapHeader(szLog);
						}

						CsAlt.Unlock();

						// Уведомить ГУЙ
						CESERVER_REQ *pGuiIn = nullptr, *pGuiOut = nullptr;
						int nSize = sizeof(CESERVER_REQ_HDR)+sizeof(CESERVER_REQ_STARTSTOP);
						pGuiIn = ExecuteNewCmd(CECMD_CMDSTARTSTOP, nSize);

						if (!pGuiIn)
						{
							_ASSERTE(pGuiIn!=nullptr && "Memory allocation failed");
						}
						else
						{
							pGuiIn->StartStop.dwPID = nAltServerWasStarted ? nAltServerWasStarted : nAltServerWasStopped;
							pGuiIn->StartStop.hServerProcessHandle = 0; // для GUI смысла не имеет
							pGuiIn->StartStop.nStarted = nAltServerWasStarted ? sst_AltServerStart : sst_AltServerStop;

							pGuiOut = ExecuteGuiCmd(gState.realConWnd_, pGuiIn, gState.realConWnd_);

							_ASSERTE(pGuiOut!=nullptr && "Can not switch GUI to alt server?"); // успешное выполнение?
							ExecuteFreeResult(pGuiIn);
							ExecuteFreeResult(pGuiOut);
						}
					}
				}

				// Смена сервера
				nAltWait = WAIT_OBJECT_0;
			}
			else if (nAltWait == (WAIT_OBJECT_0+2))
			{
				// затребовано завершение потока
				break;
			}
			#ifdef _DEBUG
			else
			{
            	// Неожиданный результат WaitForMultipleObjects
				_ASSERTE(nAltWait==WAIT_OBJECT_0 || nAltWait==WAIT_TIMEOUT);
			}
			#endif
		}
		else
		{
			nAltWait = WAIT_OBJECT_0;
		}

		if (this->hCloseAltServer)
		{
			// Чтобы не подраться между потоками - закрывать хэндл только здесь
			if (this->hCloseAltServer != this->hAltServer)
			{
				SafeCloseHandle(this->hCloseAltServer);
			}
			else
			{
				this->hCloseAltServer = nullptr;
			}
		}

		// Always update con handle, мягкий вариант
		// !!! В Win7 закрытие дескриптора в ДРУГОМ процессе - закрывает консольный буфер ПОЛНОСТЬЮ. В итоге, буфер вывода telnet'а схлопывается! !!!
		// 120507 - Если крутится альт.сервер - то игнорировать
		if (WorkerServer::Instance().IsReopenHandleAllowed()
			&& !nAltWait
			&& ((GetTickCount() - nLastConHandleTick) > UPDATECONHANDLE_TIMEOUT))
		{
			// Need to block all requests to output buffer in other threads
			WorkerServer::Instance().ConOutCloseHandle();
			nLastConHandleTick = GetTickCount();
		}

		//// Попытка поправить CECMD_SETCONSOLECP
		//if (gpSrv->hLockRefreshBegin)
		//{
		//	// Если создано событие блокировки обновления -
		//	// нужно дождаться, пока оно (hLockRefreshBegin) будет выставлено
		//	SetEvent(gpSrv->hLockRefreshReady);
		//
		//	while(gpSrv->hLockRefreshBegin
		//	        && WaitForSingleObject(gpSrv->hLockRefreshBegin, 10) == WAIT_TIMEOUT)
		//		SetEvent(gpSrv->hLockRefreshReady);
		//}

		#ifdef _DEBUG
		if (nAltWait == WAIT_TIMEOUT)
		{
			// Если крутится альт.сервер - то запрос на изменение размера сюда приходить НЕ ДОЛЖЕН
			_ASSERTE(!gpSrv->nRequestChangeSize);
		}
		#endif

		// Из другой нити поступил запрос на изменение размера консоли
		// 120507 - Если крутится альт.сервер - то игнорировать
		if (!nAltWait && (gpSrv->nRequestChangeSize > 0))
		{
			if (gState.bStationLocked_)
			{
				LogString("!!! Change size request received while station is LOCKED !!!");
				_ASSERTE(!gState.bStationLocked_);
			}

			InterlockedDecrement(&gpSrv->nRequestChangeSize);
			// AVP гундит... да вроде и не нужно
			//DWORD dwSusp = 0, dwSuspErr = 0;
			//if (gpSrv->hRootThread) {
			//	WARNING("A 64-bit application can suspend a WOW64 thread using the Wow64SuspendThread function");
			//	// The handle must have the THREAD_SUSPEND_RESUME access right
			//	dwSusp = SuspendThread(gpSrv->hRootThread);
			//	if (dwSusp == (DWORD)-1) dwSuspErr = GetLastError();
			//}
			SetConsoleSize(gpSrv->nReqSizeBufferHeight, gpSrv->crReqSizeNewSize, gpSrv->rReqSizeNewRect, gpSrv->sReqSizeLabel, gpSrv->bReqSizeForceLog);
			//if (gpSrv->hRootThread) {
			//	ResumeThread(gpSrv->hRootThread);
			//}
			// Событие выставим ПОСЛЕ окончания перечитывания консоли
			lbWasSizeChange = TRUE;
			//SetEvent(gpSrv->hReqSizeChanged);
		}

		// Проверить количество процессов в консоли.
		// Функция выставит ghExitQueryEvent, если все процессы завершились.
		//lbProcessChanged = CheckProcessCount();
		// Функция срабатывает только через интервал CHECK_PROCESSES_TIMEOUT (внутри защита от частых вызовов)
		// #define CHECK_PROCESSES_TIMEOUT 500
		gpWorker->Processes().CheckProcessCount();

		// While station is locked - no sense to scan console contents
		if (gState.bStationLocked_)
		{
			nWait = WaitForSingleObject(ghQuitEvent, 50);
			if (nWait == WAIT_OBJECT_0)
			{
				break; // Server stop was requested
			}
			// Skip until station will be unlocked
			continue;
		}

		// Подождать немножко
		if (gpSrv->nMaxFPS>0)
		{
			dwTimeout = 1000 / gpSrv->nMaxFPS;

			// Было 50, чтобы не перенапрягать консоль при ее быстром обновлении ("dir /s" и т.п.)
			if (dwTimeout < 10) dwTimeout = 10;
		}
		else
		{
			dwTimeout = 100;
		}

		// !!! Здесь таймаут должен быть минимальным, ну разве что консоль неактивна
		//		HANDLE hOut = (HANDLE)ghConOut;
		//		if (hOut == INVALID_HANDLE_VALUE) hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		//		TODO("Проверить, а собственно реагирует на изменения в консоли?");
		//		-- на изменения (запись) в консоли не реагирует
		//		dwConWait = WaitForSingleObject ( hOut, dwTimeout );
		//#ifdef _DEBUG
		//		if (dwConWait == WAIT_OBJECT_0) {
		//			DEBUGSTRCHANGES(L"STD_OUTPUT_HANDLE was set\n");
		//		}
		//#endif
		//
		if (lbWasSizeChange)
		{
			nWait = nRefreshEventId; // требуется перечитать консоль после изменения размера!
			bSetRefreshDoneEvent = TRUE;
		}
		else
		{
			DWORD nEvtCount = nEventsBaseCount;
			DWORD nWaitTimeout = dwTimeout;
			DWORD nFarCommit = 99;
			DWORD nCursorChanged = 99;

			if (gpSrv->bFarCommitRegistered)
			{
				if (nEvtCount < countof(hEvents))
				{
					nFarCommit = nEvtCount;
					hEvents[nEvtCount++] = gpSrv->hFarCommitEvent;
					nWaitTimeout = 2500; // No need to force console scanning, Far & ExtendedConsole.dll takes care
				}
				else
				{
					_ASSERTE(nEvtCount < countof(hEvents));
				}
			}
			if (gpSrv->bCursorChangeRegistered)
			{
				if (nEvtCount < countof(hEvents))
				{
					nCursorChanged = nEvtCount;
					hEvents[nEvtCount++] = gpSrv->hCursorChangeEvent;
				}
				else
				{
					_ASSERTE(nEvtCount < countof(hEvents));
				}
			}

			nWait = WaitForMultipleObjects(nEvtCount, hEvents, FALSE, nWaitTimeout);

			if (nWait == nFarCommit || nWait == nCursorChanged)
			{
				if (nWait == nFarCommit)
				{
					// После Commit вызванного из Far может быть изменена позиция курсора. Подождем чуть-чуть.
					if (gpSrv->bCursorChangeRegistered)
					{
						nWaitCursor = WaitForSingleObject(gpSrv->hCursorChangeEvent, 10);
					}
				}
				else if (gpSrv->bFarCommitRegistered)
				{
					nWaitCommit = WaitForSingleObject(gpSrv->hFarCommitEvent, 0);
				}

				if (gpSrv->bFarCommitRegistered && (nWait == nCursorChanged))
				{
					TODO("Можно выполнить облегченное чтение консоли");
					bOnlyCursorChanged = TRUE;
				}

				nWait = nRefreshEventId;
			}
			else
			{
				bSetRefreshDoneEvent = (nWait == nRefreshEventId);

				// Для информации и для гарантированного сброса событий

				if (gpSrv->bCursorChangeRegistered)
				{
					nWaitCursor = WaitForSingleObject(gpSrv->hCursorChangeEvent, 0);
				}

				if (gpSrv->bFarCommitRegistered)
				{
					nWaitCommit = WaitForSingleObject(gpSrv->hFarCommitEvent, 0);
				}
			}
			UNREFERENCED_PARAMETER(nWaitCursor);
		}

		if (nWait == WAIT_OBJECT_0)
		{
			break; // затребовано завершение нити
		}

		nLastConsoleActiveTick = gpSrv->nLastConsoleActiveTick;
		nLastConsoleActiveDelta = GetTickCount() - nLastConsoleActiveTick;
		if (!nLastConsoleActiveTick || (nLastConsoleActiveDelta >= REFRESH_FELL_SLEEP_TIMEOUT))
		{
			ReloadGuiSettings(nullptr);
			BOOL lbDCWndVisible = bDCWndVisible;
			bDCWndVisible = (IsWindowVisible(gState.conemuWndDC_) != FALSE);
			gpSrv->nLastConsoleActiveTick = GetTickCount();

			if (gpLogSize && (bDCWndVisible != lbDCWndVisible))
			{
				LogString(bDCWndVisible ? L"bDCWndVisible changed to true" : L"bDCWndVisible changed to false");
			}
		}

		BOOL lbOurConActive = FALSE, lbOneConActive = FALSE;
		for (size_t i = 0; i < countof(gpSrv->guiSettings.Consoles); i++)
		{
			ConEmuConsoleInfo& ci = gpSrv->guiSettings.Consoles[i];
			if ((ci.Flags & ccf_Active) && (HWND)ci.Console)
			{
				lbOneConActive = TRUE;
				if (gState.realConWnd_ == (HWND)ci.Console)
				{
					lbOurConActive = TRUE;
					break;
				}
			}
		}
		if (gpLogSize && (bOurConActive != lbOurConActive))
			LogString(lbOurConActive ? L"bOurConActive changed to true" : L"bOurConActive changed to false");
		bOurConActive = lbOurConActive;
		if (gpLogSize && (bOneConActive != lbOneConActive))
			LogString(lbOneConActive ? L"bOneConActive changed to true" : L"bOneConActive changed to false");
		bOneConActive = lbOneConActive;

		ActiveSleepInBg = (gpSrv->guiSettings.Flags & CECF_SleepInBackg);
		RetardNAPanes = (gpSrv->guiSettings.Flags & CECF_RetardNAPanes);

		BOOL lbNewActive;
		if (bOurConActive || (bDCWndVisible && !RetardNAPanes))
		{
			// Mismatch may appears during console closing
			//if (gpLogSize && gbInShutdown && (bDCWndVisible != bOurConActive))
			//	LogString(L"bDCWndVisible and bOurConActive mismatch");
			lbNewActive = gpSrv->guiSettings.bGuiActive || !ActiveSleepInBg;
		}
		else
		{
			lbNewActive = FALSE;
		}
		if (gpLogSize && (bNewActive != lbNewActive))
			LogString(lbNewActive ? L"bNewActive changed to true" : L"bNewActive changed to false");
		bNewActive = lbNewActive;

		BOOL lbNewFellInSleep = ActiveSleepInBg && !bNewActive;
		if (gpLogSize && (bNewFellInSleep != lbNewFellInSleep))
			LogString(lbNewFellInSleep ? L"bNewFellInSleep changed to true" : L"bNewFellInSleep changed to false");
		bNewFellInSleep = lbNewFellInSleep;

		if ((bNewActive != bConsoleActive) || (bNewFellInSleep != bFellInSleep))
		{
			bConsoleActive = bNewActive;
			bFellInSleep = bNewFellInSleep;

			bLowSpeed = (!bNewActive || bNewFellInSleep);
			InputLogger::Log(bLowSpeed ? InputLogger::Event::evt_SpeedLow : InputLogger::Event::evt_SpeedHigh);

			if (gpLogSize)
			{
				char szInfo[128];
				sprintf_c(szInfo, "ConEmuC: RefreshThread: Sleep changed, speed(%s)",
					bLowSpeed ? "low" : "high");
				LogString(szInfo);
			}
		}


		// Обновляется по таймауту
		SleepType = WorkerServer::Instance().CheckIndicateSleepNum();


		// Чтобы не грузить процессор неактивными консолями спим, если
		// только что не было затребовано изменение размера консоли
		if (!lbWasSizeChange
		        // не требуется принудительное перечитывание
		        && !gpSrv->bForceConsoleRead
		        // Консоль не активна
		        && (!bConsoleActive
		            // или активна, но сам ConEmu GUI не в фокусе
		            || bFellInSleep)
		        // и не дернули событие gpSrv->hRefreshEvent
		        && (nWait != nRefreshEventId)
				&& !gState.bWasReattached_)
		{
			DWORD nCurTick = GetTickCount();
			nDelta = nCurTick - nLastReadTick;

			if (SleepType)
			{
				// Выключить индикатор (low speed)
				WorkerServer::Instance().ShowSleepIndicator(SleepType, true);
			}

			// #define MAX_FORCEREFRESH_INTERVAL 500
			if (nDelta <= MAX_FORCEREFRESH_INTERVAL)
			{
				// Чтобы не грузить процессор
				continue;
			}
		}
		else if (SleepType)
		{
			WorkerServer::Instance().ShowSleepIndicator(SleepType, false);
		}


		#ifdef _DEBUG
		if (nWait == nRefreshEventId)
		{
			DEBUGSTR(L"*** hRefreshEvent was set, checking console...\n");
		}
		#endif

		// GUI was crashed or was detached?
		if (gState.conemuWndDC_ && isConEmuTerminated())
		{
			gState.bWasDetached_ = TRUE;
			WorkerServer::Instance().SetConEmuWindows(nullptr, nullptr, nullptr);
			_ASSERTE(!gState.conemuWnd_ && !gState.conemuWndDC_ && !gState.conemuWndBack_);
			gState.conemuPid_ = 0;
			WorkerServer::Instance().UpdateConsoleMapHeader(L"RefreshThread: GUI was crashed or was detached?");
			EmergencyShow(gState.realConWnd_);
		}

		// Reattach?
		if (!gState.conemuWndDC_ && gState.bWasDetached_ && (gState.runMode_ == RunMode::AltServer))
		{
			CESERVER_CONSOLE_MAPPING_HDR* pMap = gpSrv->pConsoleMap->Ptr();
			if (pMap && pMap->hConEmuWndDc && IsWindow(pMap->hConEmuWndDc))
			{
				// Reset GUI HWND's
				_ASSERTE(!gState.conemuPid_);
				WorkerServer::Instance().SetConEmuWindows(pMap->hConEmuRoot, pMap->hConEmuWndDc, pMap->hConEmuWndBack);
				_ASSERTE(gState.conemuPid_ && gState.conemuWnd_ && gState.conemuWndDC_ && gState.conemuWndBack_);

				// To be sure GUI will be updated with full info
				gpSrv->pConsole->bDataChanged = TRUE;
			}
		}

		// 17.12.2009 Maks - попробую убрать
		//if (gState.conemuWnd_ && GetForegroundWindow() == gState.realConWnd) {
		//	if (lbFirstForeground || !IsWindowVisible(gState.realConWnd)) {
		//		DEBUGSTR(L"...apiSetForegroundWindow(gState.conemuWnd_);\n");
		//		apiSetForegroundWindow(gState.conemuWnd_);
		//		lbFirstForeground = FALSE;
		//	}
		//}

		// Если можем - проверим текущую раскладку в консоли
		// 120507 - Если крутится альт.сервер - то игнорировать
		if (!nAltWait && !WorkerServer::Instance().IsDebuggerActive())
		{
			gpWorker->CheckKeyboardLayout();
		}

		/* ****************** */
		/* Перечитать консоль */
		/* ****************** */
		// 120507 - Если крутится альт.сервер - то игнорировать
		if (!nAltWait && !WorkerServer::Instance().IsDebuggerActive())
		{
			bool lbReloadNow = true;
			#if defined(TEST_REFRESH_DELAYED)
			static DWORD nDbgTick = 0;
			const DWORD nMax = 1000;
			HANDLE hOut = (HANDLE)ghConOut;
			DWORD nWaitOut = WaitForSingleObject(hOut, 0);
			DWORD nCurTick = GetTickCount();
			if ((nWaitOut == WAIT_OBJECT_0) || (nCurTick - nDbgTick) >= nMax)
			{
				nDbgTick = nCurTick;
			}
			else
			{
				lbReloadNow = false;
				lbChanged = FALSE;
			}
			//ShutdownSrvStep(L"ReloadFullConsoleInfo begin");
			#endif

			if (lbReloadNow)
			{
				lbChanged = WorkerServer::Instance().ReloadFullConsoleInfo(gState.bWasReattached_/*lbForceSend*/);
			}

			#if defined(TEST_REFRESH_DELAYED)
			//ShutdownSrvStep(L"ReloadFullConsoleInfo end (%u,%u)", (int)lbReloadNow, (int)lbChanged);
			#endif

			// При этом должно передернуться gpSrv->hDataReadyEvent
			if (gState.bWasReattached_)
			{
				_ASSERTE(lbChanged);
				_ASSERTE(gpSrv->pConsole && gpSrv->pConsole->bDataChanged);
				gState.bWasReattached_ = FALSE;
			}
		}
		else
		{
			lbChanged = FALSE;
		}

		// Событие выставим ПОСЛЕ окончания перечитывания консоли
		if (lbWasSizeChange)
		{
			SetEvent(gpSrv->hReqSizeChanged);
			lbWasSizeChange = FALSE;
		}

		if (bSetRefreshDoneEvent)
		{
			SetEvent(gpSrv->hRefreshDoneEvent);
		}

		// запомнить последний tick
		//if (lbChanged)
		nLastReadTick = GetTickCount();
		MCHKHEAP
	}

	return 0;
}



int WorkerServer::MySetWindowRgn(CESERVER_REQ_SETWINDOWRGN* pRgn)
{
	if (!pRgn)
	{
		_ASSERTE(pRgn!=nullptr);
		return 0; // Invalid argument!
	}

	BOOL bRedraw = pRgn->bRedraw;
	HRGN hRgn = nullptr, hCombine = nullptr;

	if (pRgn->nRectCount == 0)
	{
		// return SetWindowRgn((HWND)pRgn->hWnd, nullptr, pRgn->bRedraw);
	}
	else if (pRgn->nRectCount == -1)
	{
		bRedraw = FALSE;
		apiShowWindow((HWND)pRgn->hWnd, SW_HIDE);
		// return SetWindowRgn((HWND)pRgn->hWnd, nullptr, FALSE);
	}
	else
	{
		bRedraw = TRUE;
		// Need calculation
		HRGN hSubRgn = nullptr;
		BOOL lbPanelVisible = TRUE;
		hRgn = CreateRectRgn(pRgn->rcRects->left, pRgn->rcRects->top, pRgn->rcRects->right, pRgn->rcRects->bottom);

		for (int i = 1; i < pRgn->nRectCount; i++)
		{
			RECT rcTest;

			// IntersectRect не работает, если низ совпадает?
			_ASSERTE(pRgn->rcRects->bottom != (pRgn->rcRects+i)->bottom);
			if (!IntersectRect(&rcTest, pRgn->rcRects, pRgn->rcRects+i))
				continue;

			// Все остальные прямоугольники вычитать из hRgn
			hSubRgn = CreateRectRgn(rcTest.left, rcTest.top, rcTest.right, rcTest.bottom);

			if (!hCombine)
				hCombine = CreateRectRgn(0,0,1,1);

			int nCRC = CombineRgn(hCombine, hRgn, hSubRgn, RGN_DIFF);

			if (nCRC)
			{
				HRGN hTmp = hRgn; hRgn = hCombine; hCombine = hTmp;
				DeleteObject(hSubRgn); hSubRgn = nullptr;
			}

			if (nCRC == NULLREGION)
			{
				lbPanelVisible = FALSE; break;
			}
		}
	}

	if (RELEASEDEBUGTEST((gpLogSize!=nullptr), true))
	{
		wchar_t szInfo[255];
		RECT rcBox = {};
		int nRgn = hRgn ? GetRgnBox(hRgn, &rcBox) : NULLREGION;
		swprintf_c(szInfo,
			nRgn ? L"CECMD_SETWINDOWRGN(0x%08X, <%u> {%i,%i}-{%i,%i})" : L"CECMD_SETWINDOWRGN(0x%08X, nullptr)",
			(DWORD)(DWORD_PTR)(HWND)pRgn->hWnd, nRgn, LOGRECTCOORDS(rcBox));
		LogString(szInfo);
	}

	int iRc = 0;
	SetWindowRgn((HWND)pRgn->hWnd, hRgn, bRedraw);
	hRgn = nullptr;

	if (hCombine) { DeleteObject(hCombine); hCombine = nullptr; }

	return iRc;
}

void WorkerServer::SetFarPid(DWORD pid)
{
	this->nActiveFarPID_ = pid;

	// update mapping using MainServer if this is Alternative
	UpdateConsoleMapHeader(L"SetFarPid");
}

DWORD WorkerServer::GetLastActiveFarPid() const
{
	return this->nActiveFarPID_;
}

void WorkerServer::ApplyProcessSetEnvCmd()
{
	CStartEnv setEnv;
	EnvCmdProcessor()->Apply(&setEnv);
}

// Lines come from Settings/Environment page
void WorkerServer::ApplyEnvironmentCommands(LPCWSTR pszCommands)
{
	if (!pszCommands || !*pszCommands)
	{
		_ASSERTE(pszCommands && *pszCommands);
		return;
	}

	// These must be applied before commands from CommandLine
	EnvCmdProcessor()->AddLines(pszCommands, true);
}

int WorkerServer::CheckGuiVersion()
{
	// If we already know the ConEmu HWND (root window)
	if (!gState.hGuiWnd)
		return 0;

	// try to validate it's version
	DWORD nGuiPid = 0; GetWindowThreadProcessId(gState.hGuiWnd, &nGuiPid);
	DWORD nWrongValue = 0;
	SetLastError(0);
	const LgsResult lgsRc = ReloadGuiSettings(nullptr, &nWrongValue);
	if (lgsRc >= LgsResult::Succeeded)
		return 0;

	wchar_t szLgs[80] = L"";
	swprintf_c(szLgs, L"LGS=%u, Code=%u, GUI PID=%u, Srv PID=%u", lgsRc, GetLastError(), nGuiPid, GetCurrentProcessId());

	wchar_t szLgsError[200] = L"";
	switch (lgsRc)
	{
	case LgsResult::WrongVersion:
		swprintf_c(szLgsError, L"Failed to load ConEmu info!\n"
			L"Found ProtocolVer=%u but Required=%u.\n"
			L"%s.\n"
			L"Please update all ConEmu components!",
			nWrongValue, static_cast<DWORD>(CESERVER_REQ_VER), szLgs);
		break;
	case LgsResult::WrongSize:
		swprintf_c(szLgsError, L"Failed to load ConEmu info!\n"
			L"Found MapSize=%u but Required=%u."
			L"%s.\n"
			L"Please update all ConEmu components!",
			nWrongValue, static_cast<DWORD>(sizeof(ConEmuGuiMapping)), szLgs);
		break;
	default:
		swprintf_c(szLgsError, L"Failed to load ConEmu info!\n"
			L"%s.\n"
			L"Please update all ConEmu components!",
			szLgs);
	}

	// Add log info
	LogFunction(szLgs);

	// Show user message
	wchar_t szTitle[128] = L"";
	swprintf_c(szTitle, L"ConEmuC[Srv]: PID=%u", GetCurrentProcessId());
	MessageBox(nullptr, szLgsError, szTitle, MB_ICONSTOP | MB_SYSTEMMODAL);

	return CERR_WRONG_GUI_VERSION;
}
