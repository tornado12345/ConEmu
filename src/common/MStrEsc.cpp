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
#include "Common.h"
#include "MStrEsc.h"

/// Set escapes: wchar(13) --> "\\r", etc.
/// pszSrc and pszDst must point to different memory blocks
/// pszDst must have at least 5 wchars available (four "\\xFF" and final "\0")
bool EscapeChar(LPCWSTR& pszSrc, LPWSTR& pszDst)
{
	if (!pszSrc || !pszDst)
	{
		_ASSERTE(pszSrc && pszDst);
		return false;
	}
	if (pszSrc == pszDst)
	{
		_ASSERTE(pszSrc != pszDst);
		return false;
	}

	const wchar_t wc = *pszSrc;
	switch (wc)
	{
	case L'"': // 34
		*(pszDst++) = L'\\';
		*(pszDst++) = L'"';
		pszSrc++;
		break;
	case L'\\': // 92
		*(pszDst++) = L'\\';
		*(pszDst++) = L'\\';
		pszSrc++;
		break;
	case L'\r': // 13
		*(pszDst++) = L'\\';
		*(pszDst++) = L'r';
		pszSrc++;
		break;
	case L'\n': // 10
		*(pszDst++) = L'\\';
		*(pszDst++) = L'n';
		pszSrc++;
		break;
	case L'\t': // 9
		*(pszDst++) = L'\\';
		*(pszDst++) = L't';
		pszSrc++;
		break;
	case L'\b': // 8 (BS)
		*(pszDst++) = L'\\';
		*(pszDst++) = L'b';
		pszSrc++;
		break;
	case 27: //ESC
		*(pszDst++) = L'\\';
		*(pszDst++) = L'e';
		pszSrc++;
		break;
	case L'\a': // 7 (BELL)
		*(pszDst++) = L'\\';
		*(pszDst++) = L'a';
		pszSrc++;
		break;
	default:
		// Escape (if possible) ASCII symbols with codes 01..31 (dec)
		if (wc < L' ')
		{
			wchar_t wcn = pszSrc[1];
			// If next string character is 'hexadecimal' digit - back conversion will be ambiguous
			if (!((wcn >= L'0' && wcn <= L'9') || (wcn >= L'a' && wcn <= L'f') || (wcn >= L'A' && wcn <= L'F')))
			{
				*(pszDst++) = L'\\';
				*(pszDst++) = L'x';
				// We've checked above for subsequent symbol to avoid ambiguous unescaping
				msprintf(pszDst, 3, L"%02X", (UINT)wc);
				pszDst+=2;
				break;
			}
		}
		*(pszDst++) = *(pszSrc++);
	}

	return true;
}

/// Remove escapes: "\\r" --> wchar(13), etc.
bool UnescapeChar(LPCWSTR& pszSrc, LPWSTR& pszDst)
{
	if (!pszSrc || !pszDst)
	{
		_ASSERTE(pszSrc && pszDst);
		return false;
	}

	if (*pszSrc == L'\\')
	{
		// -- Must be the same set in "Set escapes"
		switch (pszSrc[1])
		{
		case L'"':
			*(pszDst++) = L'"';
			pszSrc += 2;
			break;
		case L'\\':
			*(pszDst++) = L'\\';
			pszSrc += 2;
			break;
		case L'r': case L'R':
			*(pszDst++) = L'\r';
			pszSrc += 2;
			break;
		case L'n': case L'N':
			*(pszDst++) = L'\n';
			pszSrc += 2;
			break;
		case L't': case L'T':
			*(pszDst++) = L'\t';
			pszSrc += 2;
			break;
		case L'b': case L'B':
			*(pszDst++) = L'\b';
			pszSrc += 2;
			break;
		case L'e': case L'E':
			*(pszDst++) = 27; // ESC
			pszSrc += 2;
			break;
		case L'a': case L'A':
			*(pszDst++) = L'\a'; // BELL
			pszSrc += 2;
			break;
		case L'x':
		{
			// Up to four hexadecimal letters are allowed
			wchar_t sTemp[5] = L"", *pszEnd = NULL;
			lstrcpyn(sTemp, pszSrc+2, 5);
			UINT wc = wcstoul(sTemp, &pszEnd, 16);
			if (pszEnd > sTemp)
			{
				*(pszDst++) = LOWORD(wc);
				pszSrc += (pszEnd - sTemp) + 2;
			}
			else
			{
				*(pszDst++) = *(pszSrc++);
			}
			break;
		}
		default:
			*(pszDst++) = *(pszSrc++);
		}
	}
	else
	{
		*(pszDst++) = *(pszSrc++);
	}

	return true;
}

/// Set escapes: wchar(13) --> "\\r", etc.
/// pszSrc and pszDst must point to different memory blocks
/// pszDst must have at least 1+4*len(pszSrc) wchars available (four "\\xFF" and final "\0")
bool EscapeString(LPCWSTR& pszSrc, LPWSTR& pszDst)
{
	if (!pszSrc || !pszDst)
	{
		_ASSERTE(pszSrc && pszDst);
		return false;
	}
	if (pszSrc == pszDst)
	{
		_ASSERTE(pszSrc != pszDst);
		return false;
	}

	while (*pszSrc)
	{
		EscapeChar(pszSrc, pszDst);
	}

	*pszDst = 0;

	return true;
}

bool UnescapeString(LPCWSTR& pszSrc, LPWSTR& pszDst)
{
	if (!pszSrc || !pszDst)
	{
		_ASSERTE(pszSrc && pszDst);
		return false;
	}

	while (*pszSrc)
	{
		UnescapeChar(pszSrc, pszDst);
	}

	*pszDst = 0;

	return true;
}

/// Intended for GuiMacro representation
/// If *pszStr* doesn't contain special symbols with ONLY exception of "\" (paths)
/// it's more convenient to represent it as *Verbatim* string, otherwise - *C-String*.
/// Always show as *C-String* those strings, which contain CRLF, Esc, tabs etc.
bool CheckStrForSpecials(LPCWSTR pszStr, bool* pbSlash /*= NULL*/, bool* pbOthers /*= NULL*/)
{
	bool bSlash = false, bOthers = false;

	if (pszStr && *pszStr)
	{
		const wchar_t szEscaped[] = L"\r\n\t\a\x1B\"";

		bSlash = (wcschr(pszStr, L'\\') != NULL);
		bOthers = (wcspbrk(pszStr, szEscaped) != NULL);
	}

	if (pbSlash)
		*pbSlash = bSlash;
	if (pbOthers)
		*pbOthers = bOthers;
	return (bSlash || bOthers);
}

MakeOneLinerFlags operator|(const MakeOneLinerFlags e1, const MakeOneLinerFlags e2)
{
	return static_cast<MakeOneLinerFlags>(static_cast<int>(e1) | static_cast<int>(e2));
}

MakeOneLinerFlags operator&(const MakeOneLinerFlags e1, const MakeOneLinerFlags e2)
{
	return static_cast<MakeOneLinerFlags>(static_cast<int>(e1) & static_cast<int>(e2));
}

/// The function replaces all \r\n\t with spaces to paste the string safely in command prompt
CEStr MakeOneLinerString(const CEStr& source, const MakeOneLinerFlags flags)
{
	CEStr result(source.c_str(L""));

	const wchar_t* const pszEnd = result.c_str() + result.GetLen();
	const bool bTrimTailing = (flags & MakeOneLinerFlags::TrimTailing) == MakeOneLinerFlags::TrimTailing;

	wchar_t* pszBuf = result.data();
	if (!pszBuf)
	{
		_ASSERTE(FALSE && "failed to allocated buffer");
		return CEStr();
	}

	wchar_t* pszDst = pszBuf;
	wchar_t* pszSrc = pszBuf;
	while (pszSrc && pszSrc < pszEnd)
	{
		// Find LineFeed
		wchar_t* pszRN = wcspbrk(pszSrc, L"\r\n\t");
		if (pszRN && *pszRN == L'\t')
		{
			*pszRN = L' ';
			continue;
		}
		if (!pszRN)
		{
			pszRN = pszSrc + wcslen(pszSrc);
		}

		// Advance to next line
		wchar_t* pszNext = pszRN;
		if (*pszNext == L'\r') pszNext++;
		if (*pszNext == L'\n') pszNext++;
		// Find end of line, trim trailing spaces
		wchar_t* pszEol = pszRN;
		if (bTrimTailing)
		{
			while ((pszEol > pszSrc) && (*(pszEol - 1) == L' ')) pszEol--;
		}
		// If line was not empty and there was already some changes
		const size_t cchLine = pszEol - pszSrc;
		if ((pszEol > pszSrc) && (pszSrc != pszDst))
		{
			memmove(pszDst, pszSrc, cchLine * sizeof(*pszSrc));
		}
		// Move src pointer to next line
		pszSrc = pszNext;
		// Move dst pointer and add one trailing space (line delimiter)
		pszDst += cchLine;
		// No need to check ptr, memory for space-termination was reserved
		if (pszSrc < pszEnd)
		{
			// Delimit lines with space
			*(pszDst++) = L' ';
		}
	}
	// Z-terminate our string
	*pszDst = 0;
	// Done, it is ready for pasting
	
	_ASSERTE(result.GetLen() == (pszDst - pszBuf));
	// Buffer must not contain any line-feeds now! Safe for paste in command line!
	_ASSERTE(wcspbrk(pszBuf, L"\r\n\t") == nullptr);

	return result;
}
