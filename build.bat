@echo off
setlocal
rem Run from a Visual Studio developer command prompt with CMake on PATH.
set WTOP_BUILD_TYPE=Debug
if "%~1"=="-release" set WTOP_BUILD_TYPE=Release
cmake -S "%~dp0" -B "%~dp0build" || exit /b 1
cmake --build "%~dp0build" --config %WTOP_BUILD_TYPE% || exit /b 1
ctest --test-dir "%~dp0build" -C %WTOP_BUILD_TYPE% --output-on-failure
exit /b %ERRORLEVEL%
