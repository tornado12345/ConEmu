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
#include "../common/RConStartArgs.h"

#ifdef _DEBUG
//	#define SHOW_STARTED_MSGBOX  // immediately after ConEmuC.exe process start show MessageBox, allows debugger attach
#define PRINT_COMSPEC(f,a) //wprintf(f,a) // prints to ConOut Comspec mode information
#define _DEBUGSTR(s) //OutputDebugString(s)

#elif defined(__GNUC__)
//	#define SHOW_STARTED_MSGBOX  // immediately after ConEmuC.exe process start show MessageBox, allows debugger attach
#define PRINT_COMSPEC(f,a) //wprintf(f,a)
#define _DEBUGSTR(s)

#else
#define PRINT_COMSPEC(f,a)
#define _DEBUGSTR(s)

#endif

#ifdef _DEBUG
#define xf_check() { xf_validate(); xf_dump_chk(); }
#else
#define xf_check()
#endif

#define DEBUGLOG(s) //DEBUGSTR(s)
#define DEBUGLOGSIZE(s) DEBUGSTR(s)
#define DEBUGLOGLANG(s) //DEBUGSTR(s) //; Sleep(2000)

#define CSECTION_NON_RAISE

void ShutdownSrvStep(LPCWSTR asInfo, int nParm1 = 0, int nParm2 = 0, int nParm3 = 0, int nParm4 = 0);

enum SetTerminateEventPlace
{
	ste_None = 0,
	ste_ServerDone,
	ste_ConsoleMain,
	ste_ProcessCountChanged,
	ste_CheckProcessCount,
	ste_DebugThread,
	ste_WriteMiniDump,
	ste_CmdDetachCon,
	ste_HandlerRoutine,
	ste_Attach2GuiFailed,
};
extern SetTerminateEventPlace gTerminateEventPlace;
void SetTerminateEvent(SetTerminateEventPlace eFrom);

bool isConEmuTerminated();

/*  Global  */
extern DWORD   gnSelfPID;
extern wchar_t gsModuleName[32];
extern wchar_t gsVersion[20];
extern wchar_t gsSelfExe[MAX_PATH];  // Full path+exe to our executable
extern wchar_t gsSelfPath[MAX_PATH]; // Directory of our executable
//HANDLE  ghConIn = nullptr, ghConOut = nullptr;
extern DWORD   gnMainServerPID; // PID сервера (инициализируется на старте, при загрузке Dll)
extern DWORD   gnAltServerPID; // PID сервера (инициализируется на старте, при загрузке Dll)
extern BOOL    gbLogProcess; // (pInfo->nLoggingType == glt_Processes)
extern BOOL    gbWasBufferHeight;
extern BOOL    gbNonGuiMode;
extern DWORD   gnExitCode;
extern HANDLE  ghExitQueryEvent; // выставляется когда в консоли не остается процессов
extern int nExitQueryPlace, nExitPlaceStep, nExitPlaceThread;
extern HANDLE  ghQuitEvent;      // когда мы в процессе закрытия (юзер уже нажал кнопку "Press to close console")
extern bool    gbQuit;           // когда мы в процессе закрытия (юзер уже нажал кнопку "Press to close console")
//extern bool    gbSkipHookersCheck;
extern BOOL    gbInShutdown;
extern int     gbRootWasFoundInCon;
extern BOOL    gbComspecInitCalled;
extern DWORD   gdwMainThreadId;
extern wchar_t* gpszRunCmd;
extern bool    gbRunInBackgroundTab;
extern DWORD   gnImageSubsystem;
#ifdef _DEBUG
extern size_t gnHeapUsed, gnHeapMax;
extern HANDLE ghFarInExecuteEvent;
#endif

#include "../common/Common.h"
#include "../common/MFileLogEx.h"
#include "LogFunction.h"


#define START_MAX_PROCESSES 1000
#define CHECK_PROCESSES_TIMEOUT 500
#define CHECK_ANTIVIRUS_TIMEOUT (6*1000)
#define CHECK_ROOT_START_TIMEOUT (10*1000)
#ifdef _DEBUG
	#define CHECK_ROOT_OK_TIMEOUT (IsDebuggerPresent() ? ((DWORD)-1) : (10*1000)) // while debugging - wait infinitive
#else
	#define CHECK_ROOT_OK_TIMEOUT (10*1000)
#endif
#define MAX_FORCEREFRESH_INTERVAL 500
#define MAX_SYNCSETSIZE_WAIT 1000
#define GUI_PIPE_TIMEOUT 300
#define RELOAD_INFO_TIMEOUT 500
#define EXTCONCOMMIT_TIMEOUT 500
#define REQSIZE_TIMEOUT 5000
#define GUIREADY_TIMEOUT 10000
#define UPDATECONHANDLE_TIMEOUT 1000
#define GUIATTACH_TIMEOUT 10000
#define INPUT_QUEUE_TIMEOUT 100
#define ATTACH2GUI_TIMEOUT 10000
#define GUIATTACHEVENT_TIMEOUT 250
#define REFRESH_FELL_SLEEP_TIMEOUT 3000
#define LOCK_READOUTPUT_TIMEOUT 10000
#define LOCK_REOPENCONOUT_TIMEOUT 250
#define WAIT_SETCONSCRBUF_MAX_TIMEOUT 60000
#define WAIT_SETCONSCRBUF_MIN_TIMEOUT 15000
#define LOCK_REFRESH_CONTROL_TIMEOUT 2500

//#define IMAGE_SUBSYSTEM_DOS_EXECUTABLE  255

#define MAX_INPUT_QUEUE_EMPTY_WAIT 1000



#if !defined(CONSOLE_APPLICATION_16BIT)
#define CONSOLE_APPLICATION_16BIT       0x0001
#endif

#ifndef EVENT_CONSOLE_CARET
#define EVENT_CONSOLE_CARET             0x4001
#define EVENT_CONSOLE_UPDATE_REGION     0x4002
#define EVENT_CONSOLE_UPDATE_SIMPLE     0x4003
#define EVENT_CONSOLE_UPDATE_SCROLL     0x4004
#define EVENT_CONSOLE_LAYOUT            0x4005
#define EVENT_CONSOLE_START_APPLICATION 0x4006
#define EVENT_CONSOLE_END_APPLICATION   0x4007
#endif

//#undef USE_WINEVENT_SRV

BOOL createProcess(BOOL abSkipWowChange, LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);
int AttachRootProcessHandle();
void CreateLogSizeFile(int /* level */, const CESERVER_CONSOLE_MAPPING_HDR* pConsoleInfo = nullptr);
void LogSize(const COORD* pcrSize, int newBufferHeight, LPCSTR pszLabel, bool bForceWriteLog = false);
void LogModeChange(LPCWSTR asName, DWORD oldVal, DWORD newVal);
bool LogString(LPCSTR asText);
bool LogString(LPCWSTR asText);
void PrintExecuteError(LPCWSTR asCmd, DWORD dwErr, LPCWSTR asSpecialInfo = nullptr);

bool MyLoadConsolePalette(HANDLE ahConOut, CESERVER_CONSOLE_PALETTE& Palette);
BOOL MyGetConsoleScreenBufferInfo(HANDLE ahConOut, PCONSOLE_SCREEN_BUFFER_INFO apsc);
HWND FindConEmuByPID(DWORD anSuggestedGuiPID = 0);
CESERVER_CONSOLE_APP_MAPPING* GetAppMapPtr();

void SendStarted();
CESERVER_REQ* SendStopped(CONSOLE_SCREEN_BUFFER_INFO* psbi = nullptr);

bool IsOutputRedirected();
void _wprintf(LPCWSTR asBuffer);
void _printf(LPCSTR asBuffer);
void _printf(LPCSTR asFormat, DWORD dwErr);
void _printf(LPCSTR asFormat, DWORD dwErr, LPCWSTR asAddLine);
void _printf(LPCSTR asFormat, DWORD dw1, DWORD dw2, LPCWSTR asAddLine = nullptr);
void print_error(DWORD dwErr = 0, LPCSTR asFormat = nullptr);

int ParseCommandLine(LPCWSTR asCmdLine);
wchar_t* ParseConEmuSubst(LPCWSTR asCmd);
void UpdateConsoleTitle();
BOOL SetTitle(LPCWSTR lsTitle);
void Help();
void DosBoxHelp();
int  ExitWaitForKey(DWORD vkKeys, LPCWSTR asConfirm, BOOL abNewLine, BOOL abDontShowConsole, DWORD anMaxTimeout = 0);
bool IsMainServerPID(DWORD nPID);

void LoadExePath();
void UnlockCurrentDirectory();

extern BOOL gbDumpServerInitStatus;
extern WORD  gnDefTextColors, gnDefPopupColors;
extern BOOL  gbVisibleOnStartup;

#include "../common/PipeServer.h"
#include "../common/MArray.h"

struct ConProcess;

struct SrvInfo;
extern SrvInfo *gpSrv;
extern BOOL	gbTerminateOnCtrlBreak;

extern HMODULE ghOurModule;

#define USER_IDLE_TIMEOUT ((DWORD)1000)
#define CHECK_IDLE_TIMEOUT 250 /* 1000 / 4 */
#define USER_ACTIVITY ((gnBufferHeight == 0) || ((GetTickCount() - gpSrv->dwLastUserTick) <= USER_IDLE_TIMEOUT))

void PrintVersion();

extern COORD gcrVisibleSize;
extern BOOL  gbParmVisibleSize, gbParmBufSize;
extern SHORT gnBufferHeight, gnBufferWidth;

//extern HANDLE ghLogSize;
//extern wchar_t* wpszLogSizeFile;
class MFileLogEx;
extern MFileLogEx* gpLogSize;


extern BOOL gbInRecreateRoot;
