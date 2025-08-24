@echo off
:: Claude Code Windows Launcher - 1 Window, 2 Tabs
:: Set project directory
set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

:: UTF-8 encoding
chcp 65001 >nul 2>&1

echo.
echo ========================================
echo   Claude Code Launcher
echo   Project: %PROJECT_DIR%
echo ========================================
echo.
echo Opening 1 window with 2 tabs...
echo   Tab 1: Terminal (for build/test)
echo   Tab 2: Claude Code
echo.

:: Check if Claude is installed
where claude >nul 2>&1
if %errorLevel% equ 0 (
    :: Claude installed - use directly with --continue
    wt -w new nt --title "Terminal" -d "%PROJECT_DIR%" cmd ; new-tab --title "Claude Code" -d "%PROJECT_DIR%" cmd /k claude --continue
) else (
    :: Claude not installed - use npx with --continue
    echo [!] Claude not found locally, using npx...
    wt -w new nt --title "Terminal" -d "%PROJECT_DIR%" cmd ; new-tab --title "Claude Code" -d "%PROJECT_DIR%" cmd /k "npx @anthropic-ai/claude-code@latest --continue"
)

exit