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

#include "OptionsClass.h"
#include "SetPgMouse.h"
#include "SetDlgLists.h"

CSetPgMouse::CSetPgMouse()
{
}

CSetPgMouse::~CSetPgMouse()
{
}

LRESULT CSetPgMouse::OnInitDialog(HWND hDlg, bool abInitial)
{
	checkDlgButton(hDlg, cbEnableMouse, !gpSet->isDisableMouse);
	checkDlgButton(hDlg, cbSkipActivation, gpSet->isMouseSkipActivation);
	checkDlgButton(hDlg, cbSkipMove, gpSet->isMouseSkipMoving);
	checkDlgButton(hDlg, cbActivateSplitMouseOver, gpSet->bActivateSplitMouseOver);
	checkDlgButton(hDlg, cbMouseDragWindow, gpSet->isMouseDragWindow);

	// Prompt click
	checkDlgButton(hDlg, cbCTSClickPromptPosition, gpSet->AppStd.isCTSClickPromptPosition);
	UINT VkMod = gpSet->GetHotkeyById(vkCTSVkPromptClk);
	CSetDlgLists::FillListBoxItems(GetDlgItem(hDlg, lbCTSClickPromptPosition), CSetDlgLists::eKeysAct, VkMod, false);

	VkMod = gpSet->GetHotkeyById(vkCTSVkAct);
	CSetDlgLists::FillListBoxItems(GetDlgItem(hDlg, lbCTSActAlways), CSetDlgLists::eKeysAct,
		VkMod, false);
	CSetDlgLists::FillListBoxItems(GetDlgItem(hDlg, lbCTSRBtnAction), CSetDlgLists::eClipAct,
		reinterpret_cast<BYTE&>(gpSet->isCTSRBtnAction), false);
	CSetDlgLists::FillListBoxItems(GetDlgItem(hDlg, lbCTSMBtnAction), CSetDlgLists::eClipAct,
		reinterpret_cast<BYTE&>(gpSet->isCTSMBtnAction), false);

	gpSetCls->CheckSelectionModifiers(hDlg);

	return 0;
}

INT_PTR CSetPgMouse::OnComboBox(HWND hDlg, WORD nCtrlId, WORD code)
{
	if (code == CBN_SELCHANGE)
	{
		switch (nCtrlId)
		{
		case lbCTSClickPromptPosition:
			{
				BYTE VkMod = 0;
				CSetDlgLists::GetListBoxItem(hDlg, lbCTSClickPromptPosition, CSetDlgLists::eKeysAct, VkMod);
				gpSet->SetHotkeyById(vkCTSVkPromptClk, VkMod);
				gpSetCls->CheckSelectionModifiers(hDlg);
			} break;
		case lbCTSActAlways:
			{
				BYTE VkMod = 0;
				CSetDlgLists::GetListBoxItem(hDlg, lbCTSActAlways, CSetDlgLists::eKeysAct, VkMod);
				gpSet->SetHotkeyById(vkCTSVkAct, VkMod);
				gpSetCls->CheckSelectionModifiers(hDlg);
			} break;
		case lbCTSRBtnAction:
			{
				CSetDlgLists::GetListBoxItem(hDlg, lbCTSRBtnAction, CSetDlgLists::eClipAct,
					reinterpret_cast<BYTE&>(gpSet->isCTSRBtnAction));
			} break;
		case lbCTSMBtnAction:
			{
				CSetDlgLists::GetListBoxItem(hDlg, lbCTSMBtnAction, CSetDlgLists::eClipAct,
					reinterpret_cast<BYTE&>(gpSet->isCTSMBtnAction));
			} break;
		default:
			_ASSERTE(FALSE && "ListBox was not processed");
		}
	} // if (HIWORD(wParam) == CBN_SELCHANGE)

	return 0;
}

void CSetPgMouse::OnPostLocalize(HWND hDlg)
{
	// "Ctrl+Alt - drag ConEmu window"
	setCtrlTitleByHotkey(hDlg, cbMouseDragWindow, vkWndDragKey, nullptr, L" - ", nullptr);
}
