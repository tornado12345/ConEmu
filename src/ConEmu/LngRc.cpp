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
#include "ConEmuStart.h"
#include "LngData.h"
#include "LngRc.h"
#include "Options.h"
#include "../common/EnvVar.h"
#include "../common/MJsonReader.h"
#include "../common/WFiles.h"

const wchar_t gsResourceFileName[] = L"ConEmu.l10n";

//#include "../common/MSectionSimple.h"

CLngRc* gpLng = nullptr;

CLngRc::CLngRc()
{
	//mp_Lock = new MSectionSimple();
}

CLngRc::~CLngRc()
{
	//SafeDelete(mp_Lock);
}

// static
bool CLngRc::isLocalized()
{
	if (!gpLng)
		return false;
	if (gpLng->ms_Lng.IsEmpty() || gpLng->ms_l10n.IsEmpty())
		return false;
	return true;
}

// static
void CLngRc::Initialize()
{
	if (!gpLng)
		gpLng = new CLngRc();

	CLngPredefined::Initialize();
	
	// No sense to load resources now,
	// options were not initialized yet
	// -- if (gpLng) gpLng->Reload();
}

void CLngRc::Reload(bool bForce /*= false*/)
{
	bool bChanged = bForce;
	bool bExists = false;
	CEStr lsNewLng, lsNewFile;

	lsNewLng.Set(gpConEmu->opt.Language.Exists ? gpConEmu->opt.Language.GetStr() : gpSet->Language);

	// Language was requested?
	if (!lsNewLng.IsEmpty())
	{
		if (ms_Lng.IsEmpty() || (0 != wcscmp(lsNewLng, ms_Lng)))
		{
			bChanged = true;
		}

		// We need a file
		if (gpConEmu->opt.LanguageFile.Exists)
		{
			// It may contain environment variables
			lsNewFile = ExpandEnvStr(gpConEmu->opt.LanguageFile.GetStr());
			if (lsNewFile.IsEmpty())
				lsNewFile.Set(gpConEmu->opt.LanguageFile.GetStr());
			// Check if the file exists
			bExists = FileExists(lsNewFile);
		}
		else
		{
			if (!bExists)
				bExists = FileExists(lsNewFile = JoinPath(gpConEmu->ms_ConEmuExeDir, gsResourceFileName));
			if (!bExists)
				bExists = FileExists(lsNewFile = JoinPath(gpConEmu->ms_ConEmuBaseDir, gsResourceFileName));
			#ifdef _DEBUG
			if (!bExists)
				bExists = FileExists(lsNewFile = JoinPath(gpConEmu->ms_ConEmuExeDir, L"..\\Release\\ConEmu", gsResourceFileName));
			#endif
		}

		// File name was changed?
		if ((bExists == ms_l10n.IsEmpty())
			|| (bExists
				&& (ms_l10n.IsEmpty()
					|| (0 != wcscmp(lsNewFile, ms_l10n)))
				)
			)
		{
			bChanged = true;
		}
	}
	else if (!ms_Lng.IsEmpty())
	{
		bChanged = true;
	}

	// Check if we have to reload data
	if (bChanged)
	{
		if (!bExists
			|| !LoadResources(lsNewLng, lsNewFile))
		{
			ms_Lng.Clear();
			ms_l10n.Clear();

			Clear(m_Languages);
			Clean(m_CmnHints);
			Clean(m_MnuHints);
			Clean(m_Controls);
		}
	}
}

void CLngRc::Clear(MArray<CLngRc::LngDefinition>& arr)
{
	for (INT_PTR i = arr.size()-1; i >= 0; --i)
	{
		LngDefinition& l = arr[i];
		SafeFree(l.id);
		SafeFree(l.name);
		SafeFree(l.descr);
	}
	arr.clear();
}

void CLngRc::Clean(MArray<CLngRc::LngRcItem>& arr)
{
	for (INT_PTR i = arr.size()-1; i >= 0; --i)
	{
		LngRcItem& l = arr[i];
		l.Processed = false;
		l.Localized = false;
		l.MaxLen = 0;
		SafeFree(l.Str);
	}
}

bool CLngRc::LoadResources(LPCWSTR asLanguage, LPCWSTR asFile)
{
	bool  bOk = false;
	CEStr lsJsonData;
	DWORD jsonDataSize = 0, nErrCode = 0;
	MJsonValue* jsonFile = nullptr;
	MJsonValue jsonSection;
	DWORD nStartTick = 0, nLoadTick = 0, nFinTick = 0, nDelTick = 0;
	struct { LPCWSTR pszSection; MArray<LngRcItem>* arr; int idDiff; }
	sections[] = {
		{ L"cmnhints", &m_CmnHints, 0 },
		{ L"mnuhints", &m_MnuHints, IDM__MIN_MNU_ITEM_ID },
		{ L"controls", &m_Controls, 0 },
		{ L"strings",  &m_Strings, 0 },
	};

	if (gpSet->isLogging())
	{
		const CEStr lsLog(L"Loading resources: Lng=`", asLanguage, L"` File=`", asFile, L"`");
		gpConEmu->LogString(lsLog);
	}

	const int iRc = ReadTextFile(asFile, 1 << 24 /*16Mb max*/, lsJsonData.ms_Val, jsonDataSize, nErrCode);
	if (iRc != 0)
	{
		// TODO: Log error
		goto wrap;
	}
	
	nStartTick = GetTickCount();
	jsonFile = new MJsonValue();
	if (!jsonFile->ParseJson(lsJsonData))
	{
		// TODO: Log error
		const CEStr lsErrMsg(
			L"Language resources loading failed!\r\n"
			L"File: ", asFile, L"\r\n"
			L"Error: ", jsonFile->GetParseError());
		gpConEmu->LogString(lsErrMsg.ms_Val);
		DisplayLastError(lsErrMsg.ms_Val, static_cast<DWORD>(-1), MB_ICONSTOP);
		goto wrap;
	}
	nLoadTick = GetTickCount();

	// Remember language parameters
	ms_Lng.Set(asLanguage);
	ms_l10n.Set(asFile);

	// Allocate intial array size
	m_CmnHints.reserve(4096);
	m_MnuHints.reserve(512);
	m_Controls.reserve(4096);
	m_Strings.reserve(lng_NextId);

	if (jsonFile->getItem(L"languages", jsonSection) && (jsonSection.getType() == MJsonValue::json_Array))
		LoadLanguages(&jsonSection);

	// Process sections
	for (auto& section : sections)
	{
		if (jsonFile->getItem(section.pszSection, jsonSection) && (jsonSection.getType() == MJsonValue::json_Object))
			bOk |= LoadSection(&jsonSection, *(section.arr), section.idDiff);
		else
			Clean(*(section.arr));
	}

	nFinTick = GetTickCount();

wrap:
	SafeDelete(jsonFile);
	nDelTick = GetTickCount();
	if (bOk)
	{
		wchar_t szLog[120];
		swprintf_c(szLog, L"Loading resources: duration (ms): Parse: %u; Internal: %u; Delete: %u", (nLoadTick - nStartTick), (nFinTick - nLoadTick), (nDelTick - nFinTick));
		gpConEmu->LogString(szLog);
	}
	return bOk;
}

bool CLngRc::LoadLanguages(MJsonValue* pJson)
{
	const bool bRc = false;
	MJsonValue jRes, jItem;

	Clear(m_Languages);

	const size_t iCount = pJson->getLength();

	for (size_t i = 0; i < iCount; i++)
	{
		if (!pJson->getItem(i, jRes) || (jRes.getType() != MJsonValue::json_Object))
			continue;

		// Now, jRes contains something like this:
	    // {"id": "en", "name": "English" }
		CEStr lsId, lsName;
		if (jRes.getItem(L"id", jItem) && (jItem.getType() == MJsonValue::json_String))
			lsId.Set(jItem.getString());
		if (jRes.getItem(L"name", jItem) && (jItem.getType() == MJsonValue::json_String))
			lsName.Set(jItem.getString());

		if (!lsId.IsEmpty() && !lsName.IsEmpty())
		{
			LngDefinition lng = {};
			lng.id = lsId.Detach();
			lng.name = lsName.Detach();
			lng.descr = lstrmerge(lng.id, L": ", lng.name);
			m_Languages.push_back(lng);
		}
	} // for (size_t i = 0; i < iCount; i++)

	return bRc;
}

bool CLngRc::LoadSection(MJsonValue* pJson, MArray<LngRcItem>& arr, int idDiff) const
{
	bool bRc = false;
	MJsonValue jRes, jItem;

	_ASSERTE(!ms_Lng.IsEmpty());

	for (INT_PTR i = arr.size()-1; i >= 0; --i)
	{
		LngRcItem& l = arr[i];
		l.Processed = false;
		l.Localized = false;
	}

	const size_t iCount = pJson->getLength();

	for (size_t i = 0; i < iCount; i++)
	{
		if (!pJson->getItem(i, jRes) || (jRes.getType() != MJsonValue::json_Object))
			continue;

		// Now, jRes contains something like this:
	    // {
	    //  "en": "Decrease window height (check ‘Resize with arrows’)",
	    //  "ru": [ "Decrease window height ", "(check ‘Resize with arrows’)" ],
	    //  "id": 2046
		// }
		int64_t id = -1;
		const size_t childCount = jRes.getLength();

		for (INT_PTR c = (childCount - 1); c >= 0; --c)
		{
			// ReSharper disable once CppLocalVariableMayBeConst
			LPCWSTR pszName = jRes.getObjectName(c);
			if (!pszName || !*pszName)
			{
				_ASSERTE(FALSE && "Invalid object name!");
				return false;
			}

			// "id" field must be LAST!
			if (wcscmp(pszName, L"id") == 0)
			{
				if (!jRes.getItem(c, jItem) || (jItem.getType() != MJsonValue::json_Integer))
				{
					_ASSERTE(FALSE && "Invalid 'id' field");
					return false;
				}
				id = jItem.getInt();
				if ((id <= idDiff) || ((id - idDiff) > 0xFFFF))
				{
					_ASSERTE(FALSE && "Invalid 'id' value");
					return false;
				}

			} // "id"

			// "en" field must be FIRST!
			else if ((wcscmp(pszName, ms_Lng) == 0)
				|| (wcscmp(pszName, L"en") == 0)
				)
			{
				if (id == -1)
				{
					_ASSERTE(FALSE && "Wrong format, 'id' not found!");
					return false;
				}

				if (!jRes.getItem(c, jItem)
					|| ((jItem.getType() != MJsonValue::json_String)
						&& (jItem.getType() != MJsonValue::json_Array))
					)
				{
					_ASSERTE(FALSE && "Invalid 'lng' field");
					return false;
				}

				switch (jItem.getType())
				{
				case MJsonValue::json_String:
					if (!SetResource(arr, static_cast<int>(id - idDiff), jItem.getString(), true))
					{
						// Already asserted
						return false;
					}
					bRc = true;
					break;
				case MJsonValue::json_Array:
					if (!SetResource(arr, static_cast<int>(id - idDiff), &jItem))
					{
						// Already asserted
						return false;
					}
					bRc = true;
					break;
				default:
					_ASSERTE(FALSE && "Unsupported object type!")
					return false;
				} // switch (jItem.getType())

				// proper lng string found and processed, go to next resource
				break; // for (size_t c = 0; c < childCount; c++)

			} // ms_Lng || "en"

		} // for (size_t c = 0; c < childCount; c++)

	} // for (size_t i = 0; i < iCount; i++)

	return bRc;
}

// Set resource item
bool CLngRc::SetResource(MArray<LngRcItem>& arr, const int idx, LPCWSTR asValue, const bool bLocalized)
{
	if (idx < 0)
	{
		_ASSERTE(idx >= 0);
		return false;
	}

	_ASSERTE(!bLocalized || (asValue && *asValue));

	if (idx >= arr.size())
	{
		const LngRcItem dummy = {};
		arr.set_at(idx, dummy);
	}

	bool bOk = false;
	LngRcItem& item = arr[idx];

	// Caching: no resource was found for that id
	if (!asValue || !*asValue)
	{
		if (item.Str)
			item.Str[0] = 0;
		item.Processed = true;
		item.Localized = false;
		return true;
	}

	const size_t iLen = wcslen(asValue);
	if (iLen >= static_cast<uint16_t>(-1))
	{
		// Too long string?
		_ASSERTE(iLen < static_cast<uint16_t>(-1));
	}
	else
	{
		if (item.Str && (item.MaxLen >= iLen))
		{
			_wcscpy_c(item.Str, item.MaxLen+1, asValue);
		}
		else
		{
			//TODO: thread-safe
			SafeFree(item.Str);
			item.MaxLen = iLen;
			item.Str = lstrdup(asValue);
		}
		bOk = (item.Str != nullptr);
	}

	item.Processed = bOk;
	item.Localized = (bOk && bLocalized);

	return bOk;
}

// Concatenate strings from array and set resource item
bool CLngRc::SetResource(MArray<LngRcItem>& arr, int idx, MJsonValue* pJson)
{
	CEStr lsValue;
	MJsonValue jStr;

	// [ "Decrease window height ", "(check ‘Resize with arrows’)" ]
	const size_t iCount = pJson->getLength();

	for (size_t i = 0; i < iCount; i++)
	{
		if (!pJson->getItem(i, jStr) || (jStr.getType() != MJsonValue::json_String))
		{
			_ASSERTE(FALSE && "String format failure");
			return false;
		}
		lstrmerge(&lsValue.ms_Val, jStr.getString());
	}

	if (lsValue.IsEmpty())
	{
		_ASSERTE(FALSE && "Empty resource string (array)");
		return false;
	}

	return SetResource(arr, idx, lsValue.ms_Val, true);
}

LPCWSTR CLngRc::getControl(LONG id, CEStr& lsText, LPCWSTR asDefault /*= nullptr*/)
{
	if (!gpLng)
	{
		_ASSERTE(gpLng != nullptr);
		return asDefault;
	}
	if (!id || (id > static_cast<uint16_t>(-1)))
	{
		_ASSERTE(FALSE && "Control ID out of range");
		return asDefault;
	}

	gpLng->GetResource(gpLng->m_Controls, id, lsText, asDefault);

	return lsText.ms_Val;
}

bool CLngRc::GetResource(MArray<LngRcItem>& arr, const int idx, CEStr& lsText, LPCWSTR asDefault)
{
	bool bFound = false;

	if ((idx >= 0) && (idx < arr.size()))
	{
		const LngRcItem& item = arr[idx];

		if (item.Processed && (item.Str && *item.Str))
		{
			lsText.Set(item.Str);
			bFound = true;
		}
	}

	if (!bFound)
		lsText.Set(asDefault);

	return bFound;
}

LPCWSTR CLngRc::getLanguage()
{
	return (gpLng && !gpLng->ms_Lng.IsEmpty()) ? gpLng->ms_Lng.c_str() : L"en";
}

bool CLngRc::getLanguages(MArray<const wchar_t*>& languages)
{
	languages.clear();

	if (gpLng)
	{
		for (auto& m_Language : gpLng->m_Languages)
		{
			languages.push_back(m_Language.descr);
		}
	}

	if (languages.empty())
		languages.push_back(L"en: English");

	return (!languages.empty());
}

bool CLngRc::getHint(UINT id, LPWSTR lpBuffer, size_t nBufferMax)
{
	if (!gpLng)
	{
		_ASSERTE(gpLng != nullptr);
		return loadString(id, lpBuffer, nBufferMax);
	}

	// IDM__MIN_MNU_ITEM_ID
	const UINT idDiff = (id < IDM__MIN_MNU_ITEM_ID) ? 0 : IDM__MIN_MNU_ITEM_ID;
	const INT_PTR idx = (id - idDiff);
	_ASSERTE(idx >= 0);

	MArray<LngRcItem>& arr = (id < IDM__MIN_MNU_ITEM_ID) ? gpLng->m_CmnHints : gpLng->m_MnuHints;

	if (arr.size() > idx)
	{
		const LngRcItem& item = arr[idx];

		if (item.Processed)
		{
			if (item.Str && *item.Str)
			{
				lstrcpyn(lpBuffer, item.Str, nBufferMax);
				// Succeeded
				return true;
			}
			// No such resource
			goto wrap;
		}
	}

	// Use binary search to find resource
	if (loadString(id, lpBuffer, nBufferMax))
	{
		// Succeeded
		return true;
	}

	// Don't try to load it again
	gpLng->SetResource(arr, idx, nullptr, false);

wrap:
	if (lpBuffer && nBufferMax)
		lpBuffer[0] = 0;
	return false;
}

LPCWSTR CLngRc::getRsrc(UINT id, CEStr* lpText /*= nullptr*/)
{
	LPCWSTR pszRsrc = nullptr;
	if (!gpLng)
	{
		pszRsrc = CLngPredefined::getRsrc(id);
	}
	else
	{
		const INT_PTR idx = static_cast<INT_PTR>(id);
		MArray<LngRcItem>& arr = gpLng->m_Strings;

		if ((idx >= 0) && (arr.size() > idx))
		{
			const LngRcItem& item = arr[idx];

			if (item.Processed)
			{
				if (item.Str && *item.Str)
				{
					// Succeeded
					pszRsrc = item.Str;
					goto wrap;
				}
				// No such resource
				goto wrap;
			}
		}

		// Use binary search to find resource
		pszRsrc = CLngPredefined::getRsrc(id);
	}

wrap:
	if (lpText)
		lpText->Set(pszRsrc);
	// Don't return nullptr-s ever
	return (pszRsrc ? pszRsrc : L"");
}

// static
bool CLngRc::loadString(UINT id, LPWSTR lpBuffer, size_t nBufferMax)
{
	// ReSharper disable once CppLocalVariableMayBeConst
	LPCWSTR pszPredefined = CLngPredefined::getHint(id);
	if (pszPredefined == nullptr)
		return false;
	lstrcpyn(lpBuffer, pszPredefined, nBufferMax);
	return true;
}
