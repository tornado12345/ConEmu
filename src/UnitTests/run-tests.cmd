@echo off

setlocal
chcp 65001
cd /d "%~dp0"
if exist "%~dp0*.log" del /Q "%~dp0*.log" > nul
if exist "%~dp0*.obj" del /Q "%~dp0*.obj" > nul
if exist "%~dp0tests.fail" del /Q "%~dp0tests.fail" > nul

set commons=../common/CEStr.cpp ../common/Memory.cpp ../common/WObjects.cpp ../common/WUser.cpp ../common/CmdLine.cpp ../common/CmdArg.cpp ^
            ../common/MStrSafe.cpp ../common/MStrDup.cpp ../common/MAssert.cpp ../common/WThreads.cpp ../common/EnvVar.cpp ^
            ../common/MProcess.cpp ../common/RConStartArgs.cpp ../common/RConStartArgsEx.cpp ../common/MModule.cpp

set colorcmn=../ConEmu/ColorFix.cpp

set cpp_def=/nologo /D_UNICODE /DUNICODE /D_DEBUG /DDEBUG /DCE_UNIT_TEST=1
set lnk_def=/MTd /link user32.lib advapi32.lib

if NOT "%~1" == "" (
  call cecho /blue "*** Selected test: %~1 ***"
  call :%~1
  goto :end_of_tests
)

rem call cecho /blue "Following files must NOT be compiled without errors"
rem call :fail1
rem call :fail2
rem call :fail3
rem call :fail4
rem call :fail5

call cecho /blue "Following files must BE compiled/executed without errors"
call :test1

call cecho /blue "UnitTests should be executed without failures"
call :gtests


:end_of_tests
rem *** End Of Tests ***

if exist "%~dp0*.obj" del /Q "%~dp0*.obj" > nul

if exist "%~dp0tests.fail" (
  call cecho /red "!!! Some tests failed, logfile: `tests.fail` !!!"
  exit /b 99
)

if exist "%~dp0*.log" del /Q "%~dp0*.log" > nul
call cecho /blue "*** All tests passed ***"

rem *** End Of Script ***
goto :EOF


:gtests
call cecho /yellow "  Tests_Release_Win32.exe"
..\..\Release\Tests_Release_Win32.exe 1>> unittests.log
if errorlevel 1 (
  echo/  Tests_Release_Win32.exe exitcode=%errorlevel%
  echo Tests_Release_Win32.exe failed > tests.fail
) else (
  call cecho /green "  Tests_Release_Win32.exe succeeded as expected"
)
call cecho /yellow "  Tests_Release_x64.exe"
..\..\Release\Tests_Release_x64.exe 1>> unittests.log
if errorlevel 1 (
  echo/  Tests_Release_x64.exe exitcode=%errorlevel%
  echo Tests_Release_x64.exe failed > tests.fail
) else (
  call cecho /green "  Tests_Release_x64.exe succeeded as expected"
)
goto :EOF


:fail1
rem call :test_cl_fail_9 fail1.cpp
call :test_cl_fail_14 fail1.cpp
goto :EOF
:fail2
rem call :test_cl_fail_9 fail2.cpp
call :test_cl_fail_14 fail2.cpp
goto :EOF
:fail3
call :test_cl_fail_14 fail3.cpp
goto :EOF
:fail4
call :test_cl_fail_14 fail4.cpp
goto :EOF
:fail5
call :test_cl_fail_14 fail5.cpp
goto :EOF
:test1
rem call :test_cl_luck_9 test1.cpp %commons% %lnk_def%
call :test_cl_luck_14 test1.cpp %commons% %lnk_def%
goto :EOF
:test2
rem call :test_cl_luck_9 test2.cpp %colorcmn% %lnk_def%
call :test_cl_luck_14 test2.cpp %colorcmn% %lnk_def%
goto :EOF


:test_cl_luck_9
call cecho /yellow "  VC9  cl test %~1"
setlocal
call "%~dp0..\vc.build.set.x32.cmd" 9 > nul
if errorlevel 1 goto vars_err
set VS_VERSION > "build-out.log"
call :build_std /Fe"vc-test-9.exe" %*
if errorlevel 1 (
  echo VC%VS_VERSION%: %* 1>> "tests.fail"
  type "build-out.log" >> "tests.fail"
  echo/ >> "tests.fail"
  call cecho /red "  CL FAILED, NOT EXPECTED"
) else (
  call :run_test vc-test-9.exe
)
endlocal
goto :EOF

:test_cl_luck_14
call cecho /yellow "  VC15 cl test %~1"
setlocal
call "%~dp0..\vc.build.set.x32.cmd" 15 > nul
if errorlevel 1 goto vars_err
cd /d "%~dp0"
set VS_VERSION > "build-out.log"
call :build_std /Fe"vc-test-15.exe" %*
if errorlevel 1 (
  echo VC%VS_VERSION%: %* 1>> "tests.fail"
  type "build-out.log" >> "tests.fail"
  echo/ >> "tests.fail"
  call cecho /red "  CL FAILED, NOT EXPECTED"
) else (
  call :run_test vc-test-15.exe
)
endlocal
goto :EOF

:run_test
call cecho /green "  cl succeeded as expected"
call %1 > nul
if errorlevel 1 (
  echo %1 >> "tests.fail"
  call %1
  call %1 >> "tests.fail"
  echo/ >> "tests.fail"
  call %1 > nul
) else (
  call cecho /green "  %~1 succeeded as expected"
)
goto :EOF


:test_cl_fail_9
call cecho /yellow "  VC9  cl test %~1"
setlocal
call "%~dp0..\vc.build.set.x32.cmd" 9 > nul
if errorlevel 1 goto vars_err
set VS_VERSION > "build-out.log"
call :build_err %*
if errorlevel 1 (
  call cecho /green "  cl failed as expected"
) else (
  echo VC%VS_VERSION%: %* 1>> "tests.fail"
  echo/   CL NOT FAILED, NOT EXPECTED >> "tests.fail"
  echo/ >> "tests.fail"
  call cecho /red "  CL NOT FAILED, NOT EXPECTED"
)
endlocal
goto :EOF

:test_cl_fail_14
call cecho /yellow "  VC15 cl test %~1"
setlocal
call "%~dp0..\vc.build.set.x32.cmd" 15 > nul
if errorlevel 1 goto vars_err
set VS_VERSION > "build-out.log"
call :build_err %*
if errorlevel 1 (
  call cecho /green "  cl failed as expected"
) else (
  echo VC%VS_VERSION%: %* 1>> "tests.fail"
  echo/   CL NOT FAILED, NOT EXPECTED >> "tests.fail"
  echo/ >> "tests.fail"
  call cecho /red "  CL NOT FAILED, NOT EXPECTED"
)
endlocal
goto :EOF



:build_std
if exist *.obj del /Q *.obj > nul
rem if exist test*.exe del /Q test*.exe > nul
cl %cpp_def% %* 1>> "build-out.log"
goto :EOF

:build_err
if exist *.obj del /Q *.obj > nul
cl /c %cpp_def% %* 1>> "build-out.log"
goto :EOF

:vars_err
call cecho "Failed to set up VC environment"
echo Failed to set up VC environment >> "tests.fail"
goto :EOF

:end
pause
