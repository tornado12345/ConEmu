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

class CFont;

class CFontPtr
{
private:
	CFont *mp_Ref = nullptr;

public:
	CFontPtr();
	CFontPtr(CFont* apRef);
	CFontPtr(const CFontPtr& aPtr);
	~CFontPtr();

	int  Release();
	bool Attach(CFont* apRef);

	CFontPtr(CFontPtr&&) = delete;
	CFontPtr& operator=(CFontPtr&&) = delete;

public:
	// Dereference
	CFont* operator->() const;

	// Releases any current VCon and loads specified
	CFontPtr& operator=(CFont* apRef);
	CFontPtr& operator=(const CFontPtr& aPtr);

	// Ptr, No Asserts
	operator CFont*() const;
	CFont* Ptr() const;
	const CFont* CPtr() const;

	// Validation
	bool IsSet() const;
	bool Equal(const CFont* pFont) const;
};

bool operator== (const CFontPtr &a, const CFontPtr &b);
bool operator!= (const CFontPtr &a, const CFontPtr &b);
