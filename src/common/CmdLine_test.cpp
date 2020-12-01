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

#define HIDE_USE_EXCEPTION_INFO
#define SHOWDEBUGSTR

#include "defines.h"
#include "../UnitTests/gtest.h"
#include "../UnitTests/test_mock_file.h"

#include "CmdLine.h"
#include "MStrDup.h"
#include "RConStartArgsEx.h"

TEST(CmdLine, NextArg_Switches)
{
	LPCWSTR pszCmd =
		L"conemu.exe /c/dir -run -inside=0x800 "
		L" /cmdlist \"-inside=\\eCD /d %1\" @\"C:\\long path\\args.tmp\" "
		L"@C:\\tmp\\args.tmp -bad|switch ";
	CmdArg ls;

	EXPECT_NE(nullptr, (pszCmd=NextArg(pszCmd,ls)));
	EXPECT_STREQ(ls.c_str(), L"conemu.exe");
	EXPECT_FALSE(ls.IsPossibleSwitch());

	EXPECT_NE(nullptr, (pszCmd=NextArg(pszCmd,ls)));
	EXPECT_STREQ(ls.c_str(), L"/c/dir");
	EXPECT_FALSE(ls.IsPossibleSwitch());

	EXPECT_NE(nullptr, (pszCmd=NextArg(pszCmd,ls)));
	EXPECT_STREQ(ls.c_str(), L"-run");
	EXPECT_TRUE(ls.OneOfSwitches(L"/cmd",L"/run"));
	EXPECT_FALSE(ls.OneOfSwitches(L"/cmd",L"/cmdlist"));
	EXPECT_TRUE(ls.IsSwitch(L"-run"));
	EXPECT_FALSE(ls.IsSwitch(L"-run:"));
	EXPECT_FALSE(ls.IsSwitch(L"-run="));
	EXPECT_STREQ(L"", ls.GetExtra());

	EXPECT_NE(nullptr, (pszCmd=NextArg(pszCmd,ls)));
	EXPECT_STREQ(ls.c_str(), L"-inside=0x800");
	EXPECT_TRUE(ls.IsSwitch(L"-inside="));
	EXPECT_TRUE(ls.IsSwitch(L"-inside:"));
	EXPECT_FALSE(ls.IsSwitch(L"-inside"));
	EXPECT_TRUE(ls.OneOfSwitches(L"-inside",L"-inside="));
	EXPECT_STREQ(L"0x800", ls.GetExtra());

	EXPECT_NE(nullptr, (pszCmd=NextArg(pszCmd,ls)));
	EXPECT_STREQ(ls.c_str(), L"/cmdlist");
	EXPECT_TRUE(ls.IsSwitch(L"-cmdlist"));

	EXPECT_NE(nullptr, (pszCmd=NextArg(pszCmd,ls)));
	EXPECT_STREQ(ls.c_str(), L"-inside=\\eCD /d %1");
	EXPECT_TRUE(ls.IsSwitch(L"-inside:"));
	EXPECT_STREQ(L"\\eCD /d %1", ls.GetExtra());

	// #NextArg '@' used in debugLogShell
	EXPECT_NE(nullptr, (pszCmd=NextArg(pszCmd,ls)));
	EXPECT_STREQ(ls.c_str(), L"@");
	EXPECT_NE(nullptr, (pszCmd=NextArg(pszCmd,ls)));
	EXPECT_STREQ(ls.c_str(), L"C:\\long path\\args.tmp");
	EXPECT_NE(nullptr, (pszCmd=NextArg(pszCmd,ls)));
	EXPECT_STREQ(ls.c_str(), L"@C:\\tmp\\args.tmp");

	EXPECT_NE(nullptr, (pszCmd=NextArg(pszCmd,ls)));
	EXPECT_STREQ(ls.c_str(), L"-bad|switch");
	EXPECT_FALSE(ls.IsPossibleSwitch());
}

TEST(CmdLine, NextArg_NeedCmd)
{
	struct StrTests { LPCWSTR pszCmd; bool expected; }
	tests[] = {
		{L"\"C:\\windows\\notepad.exe -f \"makefile\" COMMON=\"../../../plugins/common\"\"", FALSE},
		{L"\"\"C:\\windows\\notepad.exe  -new_console\"\"", FALSE},
		{L"\"\"cmd\"\"", FALSE},
		{L"cmd /c \"\"C:\\Program Files\\Windows NT\\Accessories\\wordpad.exe\" -?\"", FALSE},
		{L"cmd /c \"dir c:\\\"", FALSE},
		{L"abc.cmd", TRUE},
		// Do not do too many heuristic. If user really needs redirection (for 'root'!)
		// he must explicitly call "cmd /c ...". With only exception if first exe not found.
		{L"notepad text & start explorer", FALSE},
	};
	CEStr szExe;
	for (const auto& test : tests)
	{
		szExe.Clear();
		RConStartArgsEx rcs; rcs.pszSpecialCmd = lstrdup(test.pszCmd);
		rcs.ProcessNewConArg();
		const bool result = IsNeedCmd(TRUE, rcs.pszSpecialCmd, szExe);
		EXPECT_EQ(result, test.expected) << L"cmd: " << test.pszCmd;
	}
}

namespace
{
void TestIsNeedCmd(
	// ReSharper disable CppParameterMayBeConst
	LPCWSTR testCommand, LPCWSTR expectedExe, LPCWSTR expectedArgs,
	const bool expectedResult, const bool isServer, const bool expectedNeedCut, const bool expectedRootIsCmd, const bool expectedAlwaysConfirm)
{
	CEStr exe;
	NeedCmdOptions opt{};
	const auto isNeedCmd = IsNeedCmd(isServer, testCommand, exe, &opt);
	EXPECT_EQ(isNeedCmd, expectedResult) << "cmd: " << testCommand << ", srv: " << isServer;
	if (wcschr(expectedExe, L'\\') != nullptr)
	{
		EXPECT_STREQ(exe.c_str(L""), expectedExe) << "cmd: " << testCommand << ", srv: " << isServer;
	}
	else
	{
		EXPECT_STREQ(PointToName(exe.c_str(L"")), expectedExe) << "cmd: " << testCommand << ", srv: " << isServer;
	}
	EXPECT_STREQ(opt.arguments ? opt.arguments : L"", expectedArgs) << "cmd: " << testCommand << ", srv: " << isServer;
	EXPECT_EQ(opt.needCutStartEndQuot, expectedNeedCut) << "cmd: " << testCommand << ", srv: " << isServer;
	EXPECT_EQ(opt.rootIsCmdExe, expectedRootIsCmd) << "cmd: " << testCommand << ", srv: " << isServer;
	EXPECT_EQ(opt.alwaysConfirmExit, expectedAlwaysConfirm) << "cmd: " << testCommand << ", srv: " << isServer;
}
}

TEST(CmdLine, IsNeedCmd)
{
	gbVerifyIgnoreAsserts = true; // bypass debug asserts for invalid parameters

	test_mocks::FileSystemMock fileMock;
	fileMock.MockFile(LR"(C:\Tools\Arch\7z.exe)");
	fileMock.MockFile(LR"(c:\program files\arc\7z.exe)");
	fileMock.MockFile(LR"(C:\arc\7z.exe)");
	fileMock.MockFile(LR"(C:\far\far.exe)");
	fileMock.MockFile(LR"(C:\msys\bin\make.EXE)");
	fileMock.MockPathFile(L"7z.exe", LR"(C:\Tools\Arch\7z.exe)");
	fileMock.MockPathFile(L"cmd.exe", LR"(C:\Windows\System32\cmd.exe)");
	fileMock.MockPathFile(L"cacls.exe", LR"(C:\Windows\System32\cacls.exe)");
	fileMock.MockPathFile(L"chkdsk.exe", LR"(C:\Windows\System32\chkdsk.exe)");
	fileMock.MockPathFile(L"far.exe", LR"(c:\far\far.exe)");

	TestIsNeedCmd(nullptr,
		L"", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L" ",
		L"", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"\"\\\"", // NextArg failure expected
		L"", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"script.cmd arg1 arg2",
		L"script.cmd", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"set var1=val1 & set var2=val2 & pwsh.exe -noprofile",
		L"set", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"cmd.exe /c set var1=val1 & set var2=val2 & pwsh.exe -noprofile",
		L"cmd.exe", L"/c set var1=val1 & set var2=val2 & pwsh.exe -noprofile",
		false, false, false, true, true);
	TestIsNeedCmd(L"drive::path arg1 arg2",
		L"drive::path", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"start",
		L"", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"start explorer",
		L"", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"\"\"start\" \"explorer\" C:\\\"",
		L"start", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"start \"\" \"C:\\user data\\test.exe\"",
		L"", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"start \"\" C:\\Utils\\Hiew32\\hiew32.exe C:\\00\\Far.exe",
		L"", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"Call \"C:\\user scripts\\tool.cmd\" some args",
		L"Call", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"tool.exe < input > output",
		L"tool.exe", L"",
		true, false, false, true, false);
	TestIsNeedCmd(L"\"tool.exe\" < input > output",
		L"tool.exe", L"",
		true, true, false, true, false);
	TestIsNeedCmd(L"c:\\tool.exe < input > output",
		L"tool.exe", L"",
		true, false, false, true, false);
	// As it's a *server* mode (root command) we don't check for pipelining and redirection
	TestIsNeedCmd(L"%windir%\\system32\\cacls.exe < input > output",
		L"cacls.exe", L"< input > output",
		false, true, false, false, false);
	// But for ComSpec mode - the "cmd /c ..." is required
	TestIsNeedCmd(L"%windir%\\system32\\cacls.exe < input > output",
		L"cacls.exe", L"< input > output",
		true, false, false, true, false);
	// As it's a *server* mode (root command) we don't check for pipelining and redirection, even if there is no "/c"
	TestIsNeedCmd(L"%comspec% < input > output",
		L"cmd.exe", L"< input > output",
		false, true, false, true, true);
	TestIsNeedCmd(L"%comspec% /c < input > output",
		L"cmd.exe", L"/c < input > output",
		false, false, false, true, true);
	TestIsNeedCmd(L"%comspec% /K < input > output",
		L"cmd.exe", L"/K < input > output",
		false, true, false, true, true);
	// #IsNeedCmd expectedResult should be true
	TestIsNeedCmd(L"chkdsk < input > output",
		L"chkdsk.exe", L"< input > output",
		false, true, false, false, false);
	TestIsNeedCmd(L"\"\"%windir%\\system32\\cacls.exe\" a test.7z ConEmu.exe \"",
		L"cacls.exe", L"a test.7z ConEmu.exe \"",
		false, false, true, false, false);
	// #IsNeedCmd expectedArgs should not end with \"
	TestIsNeedCmd(L"\"\"7z\" a test.7z ConEmu.exe \"",
		L"7z.exe" /* via search */, L"a test.7z ConEmu.exe \"",
		false, false, true, false, false);
	TestIsNeedCmd(L"\"c:\\program files\\arc\\7z.exe\" -?",
		L"c:\\program files\\arc\\7z.exe", L"-?",
		false, false, false, false, false);
	// #IsNeedCmd expectedArgs should not end with \"
	TestIsNeedCmd(L"\"\"c:\\program files\\arc\\7z.exe\" -?\"",
		L"c:\\program files\\arc\\7z.exe", L"-?\"",
		false, false, true, false, false);
	// #IsNeedCmd expectedArgs should not end with \"
	TestIsNeedCmd(L"\"c:\\arc\\7z.exe -?\"",
		L"c:\\arc\\7z.exe", L"-?\"",
		false, false, true, false, false);
	// #IsNeedCmd expectedArgs should not end with \"
	TestIsNeedCmd(L"\"c:\\far\\far.exe -new_console\"",
		L"c:\\far\\far.exe", L"-new_console\"",
		false, false, true, false, false);
	// #IsNeedCmd expectedArgs should not end with \"\"
	// #IsNeedCmd expectedNeedCut should be true, but the command line is illegally quoted
	TestIsNeedCmd(L"\"\"c:\\far\\far.exe -new_console\"\"",
		L"c:\\far\\far.exe", L"-new_console\"\"",
		false, false, false, false, false);
	// #IsNeedCmd add test with "far" without ".exe", but we need to emulate apiSearchPath?
	TestIsNeedCmd(L"far.exe -new_console /p:c:\\far /e:some.txt",
		L"far.exe", L"-new_console /p:c:\\far /e:some.txt",
		false, false, false, false, false);
	TestIsNeedCmd(L"far -new_console /p:c:\\far /e:some.txt",
		L"far.exe", L"-new_console /p:c:\\far /e:some.txt",
		false, false, false, false, false);
	// #IsNeedCmd should work properly even without mock of "make.EXE"
	TestIsNeedCmd(L"\"C:\\msys\\bin\\make.EXE -f \"makefile\" COMMON=\"/../plugins\"\"",
		L"C:\\msys\\bin\\make.EXE", L"-f \"makefile\" COMMON=\"/../plugins\"\"",
		false, false, true, false, false);
	TestIsNeedCmd(L"C:\\msys\\bin\\make.EXE -f \"makefile\" COMMON=\"/../plugins\"",
		L"C:\\msys\\bin\\make.EXE", L"-f \"makefile\" COMMON=\"/../plugins\"",
		false, false, false, false, false);
	// #IsNeedCmd expectedAlwaysConfirm?
	TestIsNeedCmd(L"\"\"cmd\"\"",
		L"cmd.exe", L"",
		false, false, true, true, true);
	// #IsNeedCmd expectedAlwaysConfirm?
	TestIsNeedCmd(L"cmd /c \"\"c:\\program files\\arc\\7z.exe\" -?\"",
		L"cmd.exe", L"/c \"\"c:\\program files\\arc\\7z.exe\" -?\"",
		false, false, false, true, true);
	// #IsNeedCmd expectedAlwaysConfirm?
	TestIsNeedCmd(L"cmd /c \"dir c:\\\"",
		L"cmd.exe", L"/c \"dir c:\\\"",
		false, false, false, true, true);

	gbVerifyIgnoreAsserts = false; // restore default
}

TEST(CmdLine, DemangleArg)
{
	struct StrTests { wchar_t szTest[100], szCmp[100]; }
	tests[] = {
		{ L"\"Test1 & ^ \"\" Test2\"  Test3  \"Test \"\" 4\"", L"Test1 & ^ \" Test2\0Test3\0Test \" 4\0\0" }
	};

	CmdArg ls;
	for (auto& test : tests)
	{
		LPCWSTR command = test.szTest;
		LPCWSTR expected = test.szCmp;
		while ((command = NextArg(command, ls)))
		{
			DemangleArg(ls, ls.m_bQuoted);
			EXPECT_STREQ(ls.ms_Val, expected) << L"cmd: " << command;
			expected += wcslen(expected)+1;
		}
		EXPECT_EQ(*expected, 0);
	}
}

TEST(CmdLine, FromSpaceDelimitedString)
{
	auto testWithMock = [](const wchar_t* commandLine, const bool expectedResult, const wchar_t* expectedExe, const wchar_t* expectedArgs)
	{
		CEStr exe;
		const wchar_t* args = nullptr;
		const auto result = GetFilePathFromSpaceDelimitedString(commandLine, exe, args);
		EXPECT_EQ(result, expectedResult) << L"command(mock): " << commandLine;
		if (expectedResult)
		{
			EXPECT_STREQ(exe.c_str(), expectedExe);
			EXPECT_STREQ(args, expectedArgs);
		}
		else
		{
			EXPECT_TRUE(exe.IsEmpty());
			EXPECT_EQ(args, nullptr);
		}
	};

	auto testNoMock = [](const wchar_t* commandLine)
	{
		CEStr exe;
		const wchar_t* args = nullptr;
		const auto result = GetFilePathFromSpaceDelimitedString(commandLine, exe, args);
		EXPECT_EQ(result, false) << L"command(no-mock): " << commandLine;
	};
		
	
	test_mocks::FileSystemMock fileMock;
	fileMock.MockFile(LR"(C:\Far\Far.exe)");
	fileMock.MockFile(LR"(C:\Program Files\CodeBlocks/cb_console_runner.exe)");
	fileMock.MockFile(LR"(C:\Program Files\Internet Explorer\iexplore.exe)");
	fileMock.MockDirectory(LR"(C:\Program)");

	testWithMock(LR"(C:\Far\Far.exe /w /pC:\Far\Plugins\ConEmu;C:\Far\Plugins.My)", true,
		LR"(C:\Far\Far.exe)", LR"(/w /pC:\Far\Plugins\ConEmu;C:\Far\Plugins.My)");
	testWithMock(LR"(C:\Program Files\CodeBlocks/cb_console_runner.exe "C:\sources\app.exe")", true,
			LR"(C:\Program Files\CodeBlocks/cb_console_runner.exe)", LR"("C:\sources\app.exe")");
	testWithMock(LR"("C:\Program Files\CodeBlocks\cb_console_runner.exe" "C:\sources\app.exe")", true,
			LR"(C:\Program Files\CodeBlocks\cb_console_runner.exe)", LR"("C:\sources\app.exe")");
	testWithMock(LR"("C:\Program Files\CodeBlocks\cb_console_runner.exe C:\sources\app.exe")", true,
			LR"(C:\Program Files\CodeBlocks\cb_console_runner.exe)", LR"(C:\sources\app.exe")");
	testWithMock(LR"(C:\Program Files\Internet Explorer\iexplore.exe http://google.com)", true,
		LR"(C:\Program Files\Internet Explorer\iexplore.exe)", LR"(http://google.com)");


	fileMock.Reset();

	testNoMock(LR"(C:\Far\Far.exe /w /pC:\Far\Plugins\ConEmu;C:\Far\Plugins.My)");
	testNoMock(LR"(C:\Program Files\CodeBlocks/cb_console_runner.exe "C:\sources\app.exe")");
	testNoMock(LR"("C:\Program Files\CodeBlocks\cb_console_runner.exe" "C:\sources\app.exe")");
	testNoMock(LR"("C:\Program Files\CodeBlocks\cb_console_runner.exe C:\sources\app.exe")");
	testNoMock(LR"(C:\Program Files\Internet Explorer\iexplore.exe http://google.com)");
}

TEST(CmdLine, IsFilePath)
{
	// IsFilePath does not strip anything from the string, so the full string is checked
	// if it *could* be a real file path. The function does not check if the file exists.

	struct Test
	{
		const wchar_t* path;
		bool fullPathRequired;
		bool expectedResult;
	}
	tests[] = {
		// invalid characters in path
		{LR"("C:\Program Files\Far\Far.exe")", false, false},
		{LR"("C:\Program Files\Far\Far.exe")", true, false},
		{LR"(C:\Program"Files\Far\Far.exe)", true, false},
		{LR"(C:\Program|Files\Far\Far.exe)", true, false},
		{LR"(chkdsk.exe < input > output)", false, false},
		// double colon
		{LR"(C::\Program Files\Far\Far.exe)", false, false},
		{LR"(C:\Program Files:\Far\Far.exe)", false, false},
		{LR"(C:\Far\Far.exe /w /pC:\Far\Plugins\ConEmu;C:\Far\Plugins.My)", false, false},
		// full path required - e.g. started with "C:" or "\\server\...". optionally with "\\?\" prefix
		{LR"(C\Far\Far.exe)", true, false},
		{LR"(Far\Far.exe)", true, false},
		{LR"(Far.exe)", true, false},

		// file name only is ok, if no illegal characters are there
		{LR"(Far.exe)", false, true},
		// UNC prefix
		{LR"(\\?\C:\Far\Far.exe)", false, true},
		{LR"(\\?\C:\Far\Far.exe)", true, true},
		{LR"(\\?\UNC\server\share\Far.exe)", false, true},
		{LR"(\\?\UNC\server\share\Far.exe)", true, true},
		// old good
		{LR"(C:\Far\Far.exe)", false, true},
		{LR"(C:\Far\Far.exe)", true, true},
		// space included
		{LR"(C:\Program Files\Far\Far.exe)", false, true},
		{LR"(C:\Program Files\Far\Far.exe)", true, true},
		// network path
		{LR"(\\server\share\Far.exe)", false, true},
		{LR"(\\server\share\Far.exe)", true, true},
	};

	for (const auto& test : tests)
	{
		const auto result = IsFilePath(test.path, test.fullPathRequired);
		EXPECT_EQ(result, test.expectedResult) << L"path: " << test.path;
	}
}
