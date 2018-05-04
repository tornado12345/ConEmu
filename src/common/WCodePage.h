﻿
/*
Copyright (c) 2015-present Maximus5
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

#include "Common.h"

enum CpCvtResult
{
	ccr_Dummy = 0,
	ccr_OK,
	ccr_Pending,
	ccr_BadUnicode,
	ccr_BadBuffer,   // unprocessed source chars are stored in buffer while SetCP was called
	ccr_BadCodepage,
	ccr_TailChar,
	ccr_Surrogate,
	ccr_BadTail,
	ccr_DoubleBad,
};

struct CpCvt
{
	BOOL bInitialized;
	UINT nCP;
	CPINFOEX CP;

	char    buf[8];
	int     blen;
	wchar_t wrc[2];
	bool    wpair;
	int     bmax;
	DWORD   nCvtFlags;

	void ResetBuffer();
	CpCvtResult SetCP(UINT anCP);
	CpCvtResult Convert(char c, wchar_t& wc);
	CpCvtResult GetTail(wchar_t& wc);
};
