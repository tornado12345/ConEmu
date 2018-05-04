﻿
/*
Copyright (c) 2013-present Maximus5
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

#include "defines.h"

// CEStr
struct CEStr
{
public:
	wchar_t *ms_Val = nullptr;
private:
	INT_PTR mn_MaxCount = 0; // Including termination \0
public:
	// Point to the end dblquot, if we need drop first and last quotation marks
	LPCWSTR mpsz_Dequoted = nullptr;
	// true if it's a double-quoted argument from NextArg
	bool mb_Quoted = false;
	// if 0 - this is must be first call (first token of command line)
	// so, we need to test for mpsz_Dequoted
	int mn_TokenNo = 0;
	// To bee able corretly parse double quotes in commands like
	// "C:\Windows\system32\cmd.exe" /C ""C:\Python27\python.EXE""
	// "reg.exe add "HKEY_CLASSES_ROOT\Directory\Background\shell\Command Prompt\command" /ve /t REG_EXPAND_SZ /d "\"D:\Applications\ConEmu\ConEmuPortable.exe\" /Dir \"%V\" /cmd \"cmd.exe\" \"-new_console:nC:cmd.exe\" \"-cur_console:d:%V\"" /f"
	enum { cc_Undefined, cc_CmdExeFound, cc_CmdCK, cc_CmdCommand } mn_CmdCall = cc_Undefined;

	#ifdef _DEBUG
	// Debug, для отлова "не сброшенных" вызовов
	LPCWSTR ms_LastTokenEnd = nullptr;
	wchar_t ms_LastTokenSave[32] = L"";
	#endif

	bool mb_RestoreEnvVar = false; // Если используется для сохранения переменной окружения, восстановить при закрытии объекта
	wchar_t ms_RestoreVarName[32] = L"";


	// *** Not copyable, not implemented, use explicit Set method ***
	#if defined(__GNUC__)
	public:
	CEStr(const CEStr&) = delete;
	CEStr& operator=(const CEStr &) = delete;
	#else
	private:
	// We may use "=delete" in C++11, but than cl shows only first error
	CEStr(const CEStr&);
	CEStr& operator=(const CEStr &);
	#endif
	// *** Not copyable, not implemented, use explicit Set method ***

	// *** VC9 can't distinct wchar_t* as lval or rval ***
	// *** so we prohibit wchar_t assignments in VC14 to find problems ***
	#if defined(HAS_CPP11)
	private:
	CEStr(wchar_t*&);
	CEStr& operator=(wchar_t*&);
	#endif
	// *** VC9 can't distinct wchar_t* as lval or rval ***


private:
	LPCWSTR AttachInt(wchar_t*& asPtr);

	bool CompareSwitch(LPCWSTR asSwitch) const;

public:
	operator LPCWSTR() const;
	operator bool() const;
	LPCWSTR c_str(LPCWSTR asNullSubstitute = NULL) const;
	int Compare(LPCWSTR asText, bool abCaseSensitive = false) const;
	LPCWSTR Right(INT_PTR cchMaxCount) const;
	LPCWSTR Mid(INT_PTR cchOffset) const;
	CEStr& operator=(CEStr&& asStr);
	CEStr& operator=(wchar_t*&& asPtr);
	CEStr& operator=(const wchar_t* asPtr);

	INT_PTR GetLen() const;
	INT_PTR GetMaxCount();

	wchar_t* GetBuffer(INT_PTR cchMaxLen);
	wchar_t* Detach();
	LPCWSTR  Attach(wchar_t* RVAL_REF asPtr);
	LPCWSTR  Append(const wchar_t* asStr1, const wchar_t* asStr2 = NULL, const wchar_t* asStr3 = NULL, const wchar_t* asStr4 = NULL, const wchar_t* asStr5 = NULL, const wchar_t* asStr6 = NULL, const wchar_t* asStr7 = NULL, const wchar_t* asStr8 = NULL);
	void Clear();
	void Empty();
	bool IsEmpty() const;
	LPCWSTR Set(LPCWSTR asNewValue, INT_PTR anChars = -1);
	void SavePathVar(LPCWSTR asCurPath);
	void SaveEnvVar(LPCWSTR asVarName, LPCWSTR asNewValue);
	void SetAt(INT_PTR nIdx, wchar_t wc);

	void GetPosFrom(const CEStr& arg);

	// If this may be supported switch like "-run"
	bool IsPossibleSwitch() const;
	// For example, compare if ms_Val is "-run"
	bool IsSwitch(LPCWSTR asSwitch) const;
	// Stops checking on first NULL
	bool OneOfSwitches(LPCWSTR asSwitch1, LPCWSTR asSwitch2 = NULL, LPCWSTR asSwitch3 = NULL, LPCWSTR asSwitch4 = NULL, LPCWSTR asSwitch5 = NULL, LPCWSTR asSwitch6 = NULL, LPCWSTR asSwitch7 = NULL, LPCWSTR asSwitch8 = NULL, LPCWSTR asSwitch9 = NULL, LPCWSTR asSwitch10 = NULL) const;

	CEStr();
	CEStr(CEStr&& asStr);
	CEStr(wchar_t*&& asPtr);
	CEStr(const wchar_t* asStr1, const wchar_t* asStr2 = NULL, const wchar_t* asStr3 = NULL, const wchar_t* asStr4 = NULL, const wchar_t* asStr5 = NULL, const wchar_t* asStr6 = NULL, const wchar_t* asStr7 = NULL, const wchar_t* asStr8 = NULL, const wchar_t* asStr9 = NULL);
	~CEStr();
};

// Minimalistic storage for ANSI/UTF8 strings
struct CEStrA
{
public:
	CEStrA();
	CEStrA(const char* asPtr);
	CEStrA(char*&& asPtr);

	CEStrA(const CEStrA& src);
	CEStrA(CEStrA&& src);

	CEStrA& operator=(const char* asPtr);
	CEStrA& operator=(char*&& asPtr);

	CEStrA& operator=(const CEStrA& src);
	CEStrA& operator=(CEStrA&& src);

	operator const char*() const;
	operator bool() const;
	const char* c_str(const char* asNullSubstitute = NULL) const;
	INT_PTR length() const;
	void clear();

	char* getbuffer(INT_PTR cchMaxLen);
	char* release();

public:
	char* ms_Val = nullptr;
};
