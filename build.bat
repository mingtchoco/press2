@echo off
echo Building AutoKey E...

REM Compile resource file
windres resource.rc -O coff -o resource.res
if errorlevel 1 goto error

REM Compile and link the main program
g++ -O3 -mwindows -static autokey_e.cpp resource.res -o autokey_e.exe -lcomctl32 -luser32 -std=c++17
if errorlevel 1 goto error

REM Strip debug symbols for smaller size
strip autokey_e.exe

echo Build completed successfully!
echo Output: autokey_e.exe
goto end

:error
echo Build failed!
pause

:end