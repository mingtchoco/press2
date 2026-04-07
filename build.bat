@echo off
setlocal enabledelayedexpansion

echo ==============================================
echo        AutoKey E Simple Build System
echo ==============================================
echo.

REM Check if g++ is available
where g++ >nul 2>&1
if errorlevel 1 (
    echo [ERROR] g++ compiler not found!
    echo Please install MinGW-w64 or add it to PATH.
    pause
    exit /b 1
)

REM Check if windres is available
where windres >nul 2>&1
if errorlevel 1 (
    echo [ERROR] windres not found!
    echo Please install MinGW-w64 resource compiler.
    pause
    exit /b 1
)

echo [1/4] Compiling resource file...
windres resource.rc -O coff -o resource.res
if errorlevel 1 (
    echo [ERROR] Resource compilation failed!
    goto error
)
echo       Resource file compiled successfully.

echo [2/4] Compiling main program...
g++ -Wall -Wextra -O3 -mwindows -static autokey_e.cpp resource.res -o autokey_e.exe -lcomctl32 -luser32 -lpsapi -std=c++17
if errorlevel 1 (
    echo [ERROR] Program compilation failed!
    goto error
)
echo       Program compiled successfully.

echo [3/4] Optimizing binary size...
where strip >nul 2>&1
if errorlevel 1 (
    echo [WARNING] strip not found, skipping optimization.
) else (
    strip autokey_e.exe
    echo       Binary optimized.
)

echo [4/4] Verifying output...
if exist autokey_e.exe (
    echo       Output file: autokey_e.exe created successfully.
) else (
    echo [ERROR] Output file not found!
    goto error
)

echo.
echo ==============================================
echo           BUILD COMPLETED SUCCESSFULLY!
echo ==============================================
echo File: autokey_e.exe
echo.
goto end

:error
echo.
echo ==============================================
echo              BUILD FAILED!
echo ==============================================
echo Check the error messages above for details.
echo.
pause
exit /b 1

:end
echo Build process finished.
echo.