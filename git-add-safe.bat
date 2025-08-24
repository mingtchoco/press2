@echo off
echo Safe Git Add - Handling NUL file issues
echo ========================================

REM Check for nul file first
if exist nul (
    echo NUL file detected! Attempting cleanup...
    
    REM Try to run the global nul.bat
    if exist "C:\programming\2025\nul.bat" (
        call "C:\programming\2025\nul.bat"
    ) else (
        REM Fallback to PowerShell method
        powershell -ExecutionPolicy Bypass -File "%~dp0fix-nul.ps1"
    )
    
    echo Cleanup completed.
    echo.
)

REM Now perform git add
echo Adding files to git...
git add -A 2>nul

REM Check if error occurred
if %errorlevel% neq 0 (
    echo.
    echo Git add failed. Checking for nul file...
    
    REM Try cleanup again
    if exist "C:\programming\2025\nul.bat" (
        call "C:\programming\2025\nul.bat"
    ) else (
        powershell -ExecutionPolicy Bypass -File "%~dp0fix-nul.ps1"
    )
    
    echo.
    echo Retrying git add...
    git add -A
)

echo.
echo Done!