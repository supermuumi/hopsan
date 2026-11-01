@echo off
REM Build the engine host and game DLL on Windows with MSVC.
REM
REM Usage:
REM   build.bat              REM build everything (default)
REM   build.bat all
REM   build.bat host         REM rebuild only the host
REM   build.bat game         REM rebuild only the game DLL
REM   build.bat shaders      REM compile SPIR-V / MSL shader blobs
REM   build.bat clean        REM remove the build cache
REM
REM Requires:
REM   - An x64 Native Tools Command Prompt for VS 2022, or vcvarsall.bat run.
REM   - SDL3 headers + import lib reachable via SDL3_DIR (containing
REM     include\SDL3\*.h and lib\SDL3.lib + lib\SDL3.dll). Defaults to
REM     %~dp0ext\sdl3.

setlocal
pushd "%~dp0"

set MODE=%1
if "%MODE%"=="" set MODE=all

set BUILD=build\win
if not exist %BUILD% mkdir %BUILD%

if "%SDL3_DIR%"=="" set SDL3_DIR=%~dp0ext\sdl3

set CXXFLAGS=/nologo /std:c++20 /EHsc /Zi /W3 /wd4100 /wd4324 /wd4505 /wd4244 /D_CRT_SECURE_NO_WARNINGS
set INCLUDES=/I . /I src /I external /I external\imgui /I external\imgui\backends /I "%SDL3_DIR%\include"

if /I "%MODE%"=="all"     goto :build_all
if /I "%MODE%"=="host"    goto :build_host
if /I "%MODE%"=="game"    goto :build_game
if /I "%MODE%"=="shaders" goto :build_shaders_only
if /I "%MODE%"=="clean"   goto :build_clean
echo usage: build.bat [all^|host^|game^|shaders^|clean]
goto :err

:build_all
call :build_shaders
call :build_game
if errorlevel 1 goto :err
call :build_host
if errorlevel 1 goto :err
goto :ok

:build_shaders_only
call :build_shaders
goto :ok

:build_shaders
echo ==^> shaders
if not exist assets\shaders\compiled mkdir assets\shaders\compiled
where glslc >nul 2>nul
if errorlevel 1 (
    echo     glslc not found ^(install Vulkan SDK^) -- skipping SPIR-V output
) else (
    for %%S in (assets\shaders\*.vert.glsl) do (
        glslc -fshader-stage=vert "%%S" -o "assets\shaders\compiled\%%~nS.spv"
    )
    for %%S in (assets\shaders\*.frag.glsl) do (
        glslc -fshader-stage=frag "%%S" -o "assets\shaders\compiled\%%~nS.spv"
    )
)
for %%S in (assets\shaders\*.metal) do (
    copy /Y "%%S" "assets\shaders\compiled\%%~nS.msl" >nul
)
exit /b 0

:build_game
echo ==^> game
REM The ImGui core is compiled into the game DLL too (it draws into the
REM host's context). Built once and cached so hot reloads stay fast.
call :compile_if_stale %BUILD%\game_imgui.obj src\build\imgui_core_unity.cpp "src\build\imgui_core_unity.cpp;external\imgui\imgui.cpp;external\imgui\imgui_draw.cpp;external\imgui\imgui_tables.cpp;external\imgui\imgui_widgets.cpp;external\imgui\imgui.h;external\imgui\imgui_internal.h"
if errorlevel 1 exit /b 1
cl %CXXFLAGS% /LD %INCLUDES% /Fo%BUILD%\game. /Fe%BUILD%\game.dll game.cpp %BUILD%\game_imgui.obj ^
   /link /PDB:%BUILD%\game.pdb
if errorlevel 1 exit /b 1
exit /b 0

:build_host
echo ==^> host
call :compile_if_stale %BUILD%\ui_renderer.obj src\engine\ui_renderer.cpp "src\engine\ui_renderer.cpp;src\engine\ui_renderer.h" "/wd4505"
if errorlevel 1 exit /b 1
call :compile_if_stale %BUILD%\imgui.obj src\build\imgui_unity.cpp "src\build\imgui_unity.cpp;external\imgui\imgui.cpp;external\imgui\imgui_draw.cpp;external\imgui\imgui_tables.cpp;external\imgui\imgui_widgets.cpp;external\imgui\imgui_demo.cpp;external\imgui\backends\imgui_impl_sdl3.cpp;external\imgui\backends\imgui_impl_sdlgpu3.cpp;external\imgui\imgui.h;external\imgui\imgui_internal.h"
if errorlevel 1 exit /b 1
REM SDL3-devel-VC zips lay out as `lib\x64\SDL3.lib`; older / hand-built zips
REM put it flat at `lib\SDL3.lib`. Pass both to the linker so either works.
cl %CXXFLAGS% %INCLUDES% /Fo%BUILD%\host. /Fe%BUILD%\engine.exe main.cpp ^
   %BUILD%\imgui.obj %BUILD%\ui_renderer.obj ^
   /link /LIBPATH:"%SDL3_DIR%\lib\x64" /LIBPATH:"%SDL3_DIR%\lib" SDL3.lib /PDB:%BUILD%\engine.pdb
if errorlevel 1 exit /b 1
if exist "%SDL3_DIR%\lib\x64\SDL3.dll" copy /Y "%SDL3_DIR%\lib\x64\SDL3.dll" %BUILD%\ > nul
if exist "%SDL3_DIR%\lib\SDL3.dll"     copy /Y "%SDL3_DIR%\lib\SDL3.dll"     %BUILD%\ > nul
exit /b 0

REM Rebuild %1 (output) from %2 (source) if any path in the semicolon-
REM separated glob list %3 is newer than %1. Extra cl flags go in %4.
:compile_if_stale
set _CIS_OUT=%~1
set _CIS_SRC=%~2
set _CIS_DEPS=%~3
set _CIS_EXTRA=%~4
set _CIS_STALE=1
if exist "%_CIS_OUT%" (
    for /f %%R in ('powershell -NoLogo -NoProfile -Command "$out=Get-Item '%_CIS_OUT%' -ErrorAction SilentlyContinue; if (-not $out) { Write-Output 1; exit }; $latest = (Get-ChildItem -Path ('%_CIS_DEPS%'.Split(';')) -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1); if ($latest -and $latest.LastWriteTime -gt $out.LastWriteTime) { Write-Output 1 } else { Write-Output 0 }"') do set _CIS_STALE=%%R
)
if "%_CIS_STALE%"=="0" (
    echo     %_CIS_OUT% up to date
    exit /b 0
)
echo     building %_CIS_OUT%
cl %CXXFLAGS% %INCLUDES% %_CIS_EXTRA% /c /Fo"%_CIS_OUT%" "%_CIS_SRC%"
exit /b %errorlevel%

:build_clean
echo ==^> clean
if exist %BUILD% rmdir /s /q %BUILD%
exit /b 0

:ok
if not exist %BUILD%\assets (
    mklink /J %BUILD%\assets %~dp0assets > nul 2>&1
)
popd
endlocal
echo OK -^> build\win
exit /b 0

:err
popd
endlocal
exit /b 1
