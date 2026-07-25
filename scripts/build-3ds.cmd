@echo off
rem Nintendo 3DS build entry point for Windows. Enters devkitPro's bundled msys2
rem and runs scripts/build-3ds.sh from the repo root. Extra arguments are passed
rem through to CMake configure (e.g. build-3ds.cmd --fresh).
rem From WSL: cmd.exe /c scripts\build-3ds.cmd
setlocal
set DKP_MSYS_BASH=C:\devkitPro\msys2\usr\bin\bash.exe
if not exist "%DKP_MSYS_BASH%" (
    echo error: %DKP_MSYS_BASH% not found - is devkitPro installed at C:\devkitPro?
    exit /b 1
)
rem Resolve the repo root (this script lives in <root>\scripts) as an msys2 path.
pushd "%~dp0.."
for /f "delims=" %%p in ('"%DKP_MSYS_BASH%" -lc "cygpath -u '%CD%'"') do set REPO_UNIX=%%p
"%DKP_MSYS_BASH%" -lc "cd '%REPO_UNIX%' && ./scripts/build-3ds.sh %*"
set RC=%ERRORLEVEL%
popd
exit /b %RC%
