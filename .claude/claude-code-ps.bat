@echo off
:: Claude Code Windows Launcher - 1 Window, 2 Tabs
:: Set project directory
set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

:: UTF-8 encoding
chcp 65001 >nul 2>&1

echo.
echo ========================================
echo   Claude Code Launcher (PowerShell)
echo   Project: %PROJECT_DIR%
echo ========================================
echo.
echo Opening NEW window with 2 tabs...
echo   Tab 1: Terminal (for build/test)
echo   Tab 2: Claude Code
echo.

:: Check if Claude is installed
where claude >nul 2>&1
if %errorLevel% equ 0 (
    :: 새 창을 열도록 -w new 사용
    wt -w new nt --title "Terminal" -d "%PROJECT_DIR%" pwsh ; new-tab --title "Claude Code" -d "%PROJECT_DIR%" pwsh -NoExit -Command "claude --continue"
) else (
    echo [!] Claude not found locally, using npx...
    wt -w new nt --title "Terminal" -d "%PROJECT_DIR%" pwsh ; new-tab --title "Claude Code" -d "%PROJECT_DIR%" pwsh -NoExit -Command "npx @anthropic-ai/claude-code@latest --continue"
)

exit