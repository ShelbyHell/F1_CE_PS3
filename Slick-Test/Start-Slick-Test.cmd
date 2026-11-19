@echo off
setlocal
cd /d "%~dp0" || exit /b 1
if exist "%~dp0local.cmd" call "%~dp0local.cmd"
set "TEST_EXE=f1ce-slick.exe"
if /i "%~1"=="2026" set "TEST_EXE=f1ce-slick-2026.exe"
if /i "%~1"=="handling" set "TEST_EXE=f1ce-slick-handling.exe"
if not exist "%TEST_EXE%" set "TEST_EXE=..\build-win\f1ce.exe"
if not exist "%TEST_EXE%" (
    echo Build f1ce.exe or copy it to this folder. See README.md.
    pause
    exit /b 1
)
if not defined DISC set "DISC=..\PS3_GAME"
if not exist "%DISC%\USRDIR\EBOOT.BIN" (
    echo PS3_GAME folder not found. Set DISC in local.cmd. See README.md.
    pause
    exit /b 1
)
set "F1CE_SLICK_TEST=1"
if /i "%~1"=="stock" set "F1CE_SLICK_TEST=0"
if /i "%~1"=="2026" set "F1CE_SLICK_TEST=2026"
if /i "%~1"=="handling" set "F1CE_SLICK_TEST=handling"
set "F1CE_MOD_PACK="
if /i "%~2"=="textures" (
    if not exist "%~dp0MODS\SLICK_TEXTURES\offline.wad\" (
        echo Generate SLICK_TEXTURES first. See README.md.
        pause
        exit /b 1
    )
    set "F1CE_MOD_PACK=SLICK_TEXTURES"
    set "F1CE_MODS_DIR=%~dp0MODS"
)
set "F1CE_RENDERER=vulkan"
set "F1CE_SKIP_INTRO=1"
set "F1CE_FULLSCREEN=0"
set "F1CE_NO_LOG=0"
"%TEST_EXE%" "%DISC%"
