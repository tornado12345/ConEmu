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


#pragma once

#include "../common/defines.h"
#include "../common/CEStr.h"
#include "../common/MArray.h"

// enum LngResources
// ReSharper disable once CppUnusedIncludeDirective
#include "LngDataEnum.h"

//struct MSectionSimple;
class MJsonValue;

class CLngRc final
{
public:
	CLngRc();
	~CLngRc();

	CLngRc(const CLngRc&) = delete;
	CLngRc(CLngRc&&) = delete;
	CLngRc& operator=(const CLngRc&) = delete;
	CLngRc& operator=(CLngRc&&) = delete;

	static bool isLocalized();
	static void Initialize();
	void Reload(bool bForce = false);

public:
	// Methods
	static LPCWSTR getControl(LONG id, CEStr& lsText, LPCWSTR asDefault = nullptr);
	static bool getHint(UINT id, LPWSTR lpBuffer, size_t nBufferMax);
	static LPCWSTR getRsrc(UINT id, CEStr* lpText = nullptr);
	static LPCWSTR getLanguage();
	static bool getLanguages(MArray<const wchar_t*>& languages);

protected:
	// Definitions
	struct LngRcItem
	{
		bool     Processed; // This ID was processed
		bool     Localized; // true if string was loaded successfully
		uint16_t MaxLen;    // To avoid strings realloc
		wchar_t* Str;
	};

	struct LngDefinition
	{
		wchar_t* id;
		wchar_t* name;
		wchar_t* descr;
	};

protected:
	// Routines
	static void Clean(MArray<LngRcItem>& arr);
	static void Clear(MArray<LngDefinition>& arr);
	bool LoadLanguages(MJsonValue* pJson);
	bool LoadResources(LPCWSTR asLanguage, LPCWSTR asFile);
	bool LoadSection(MJsonValue* pJson, MArray<LngRcItem>& arr, int idDiff) const;
	static bool SetResource(MArray<LngRcItem>& arr, int idx, LPCWSTR asValue, bool bLocalized);
	static bool SetResource(MArray<LngRcItem>& arr, int idx, MJsonValue* pJson);
	static bool GetResource(MArray<LngRcItem>& arr, int idx, CEStr& lsText, LPCWSTR asDefault);

	static bool loadString(UINT id, LPWSTR lpBuffer, size_t nBufferMax);

protected:
	// Members
	CEStr ms_Lng;  // Current language id: en/ru/...
	CEStr ms_l10n; // Full path to localization file (ConEmu.l10n)

	//MSectionSimple* mp_Lock;

	MArray<LngDefinition> m_Languages;

	// Strings
	MArray<LngRcItem> m_CmnHints; // ID_GO ... _APS_NEXT_CONTROL_VALUE
	MArray<LngRcItem> m_MnuHints; // IDM__MIN_MNU_ITEM_ID ...
	MArray<LngRcItem> m_Controls; // ID_GO ... _APS_NEXT_CONTROL_VALUE
	MArray<LngRcItem> m_Strings;  // lng_DlgFastCfg ... lng_NextId
};

extern CLngRc* gpLng;
