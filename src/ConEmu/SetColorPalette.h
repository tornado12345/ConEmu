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

#include "../common/defines.h"

#include "../common/PaletteColors.h"

#define CEDEF_BackColorAuto    16
#define CEDEF_FontNormalColor  1
#define CEDEF_FontBoldColor    12
#define CEDEF_FontItalicColor  13

struct ColorPalette
{
	wchar_t* pszName;
	bool bPredefined;

	//reg->Load(L"TextColorIdx", nTextColorIdx);
	BYTE nTextColorIdx; // 0..15,16
	//reg->Load(L"BackColorIdx", nBackColorIdx);
	BYTE nBackColorIdx; // 0..15,16
	//reg->Load(L"PopTextColorIdx", nPopTextColorIdx);
	BYTE nPopTextColorIdx; // 0..15,16
	//reg->Load(L"PopBackColorIdx", nPopBackColorIdx);
	BYTE nPopBackColorIdx; // 0..15,16

	// Loaded
	ConEmu::PaletteColors Colors;

	// Computed
	ConEmu::PaletteColors ColorsFade;
	bool FadeInitialized;

	const ConEmu::PaletteColors& GetColors(bool abFade);

	void FreePtr();
};
