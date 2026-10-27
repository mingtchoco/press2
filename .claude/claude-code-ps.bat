@echo off
:: ==========================================================
::  프로젝트 폴더에 두고 더블클릭용
::  이 파일이 있는 폴더에서 Claude Code 를 연다
:: ==========================================================
setlocal EnableExtensions
for %%I in ("%~dp0.") do set "D=%%~fI"
call "C:\Tools\cc.bat" "%D%"
endlocal
exit /b
