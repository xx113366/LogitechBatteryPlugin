@echo off
rem ===================================================================
rem  LogitechBattery plugin build script
rem  Usage:  build.bat          -> build 64-bit DLL
rem          build.bat x86      -> build 32-bit DLL (needs i686 MinGW)
rem ===================================================================
setlocal
set ROOT=%~dp0
set ARCH=%~1
if "%ARCH%"=="" set ARCH=x64

rem Prefer an explicitly provided toolchain, then a sibling portable MinGW,
rem then whatever g++ is on PATH.
set CXX=g++
if defined MINGW_GXX set CXX=%MINGW_GXX%
set TOOLCHAIN=%ROOT%..\tools\mingw\mingw64\bin\g++.exe
if /I "%ARCH%"=="x86" set TOOLCHAIN=%ROOT%..\tools\mingw\mingw32\bin\g++.exe
if not defined MINGW_GXX if exist "%TOOLCHAIN%" set CXX=%TOOLCHAIN%

set ARCHFLAG=
if /I "%ARCH%"=="x86" set ARCHFLAG=-m32

if not exist "%ROOT%dist" mkdir "%ROOT%dist"

echo [1/2] Building LogitechBattery.dll (%ARCH%) ...
"%CXX%" %ARCHFLAG% -std=c++17 -O2 -shared -DUNICODE -D_UNICODE -o "%ROOT%dist\LogitechBattery.dll" "%ROOT%src\dllmain.cpp" "%ROOT%src\LogiBatteryPlugin.cpp" "%ROOT%src\battery_icon.cpp" "%ROOT%src\mouse_icon_data.cpp" "%ROOT%src\OptionsDialog.cpp" "%ROOT%src\hid_logitech.cpp" -lsetupapi -lhid -lshell32 -luser32 -lgdi32 -lgdiplus -lcomctl32 -static -static-libgcc -static-libstdc++
if errorlevel 1 goto fail

echo [2/2] Done: dist\LogitechBattery.dll
echo.
echo Install: copy dist\LogitechBattery.dll into the "plugins" folder under
echo          the TrafficMonitor program directory, then restart TrafficMonitor.
echo          The DLL bitness must match TrafficMonitor (x64 app -^> x64 DLL).
goto end

:fail
echo.
echo Build failed. Make sure g++ is available, or use CMake / Visual Studio.
exit /b 1

:end
endlocal
