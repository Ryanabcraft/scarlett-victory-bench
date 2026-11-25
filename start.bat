@echo off
setlocal
cd /d "%~dp0"
if exist "%~dp0victory.toml" set VICTORY_CONFIG=%~dp0victory.toml
rem A tela de selecao de conta so aceita botao de controle: sem ninguem no
rem teclado, o proprio runtime aperta A. 0 desliga.
if not defined VICTORY_AUTO_ENTER set VICTORY_AUTO_ENTER=8
"%~dp0target\release\victory.exe" run "D:\Jogos\GTAVI\GTA6.Xbox.v1.0.2.1-CYBERLEEK\game_scarlett_master.exe" --execute
if errorlevel 1 pause
