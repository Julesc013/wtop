@echo off
setlocal
rem Run from any working directory using a Visual Studio developer prompt.
rem The project entrypoint stays in the repository root above this script.
set "WTOP_ROOT=%~dp0.."
set WTOP_BUILD_TYPE=Debug
if "%~1"=="-release" set WTOP_BUILD_TYPE=Release
cmake -S "%WTOP_ROOT%" -B "%WTOP_ROOT%\out\windows" || exit /b 1
cmake --build "%WTOP_ROOT%\out\windows" --config %WTOP_BUILD_TYPE% || exit /b 1
ctest --test-dir "%WTOP_ROOT%\out\windows" -C %WTOP_BUILD_TYPE% --output-on-failure
exit /b %ERRORLEVEL%
