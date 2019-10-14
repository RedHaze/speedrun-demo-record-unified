@echo off
setlocal

REM =-------------------------------=
REM SDK paths
REM =-------------------------------=
set SDK_2013_DIR=E:\LLVM\source-sdk-2013\sp

REM =-------------------------------=
REM Games
Rem =-------------------------------=
set GAME_HL2_DIR=C:\Program Files (x86)\Steam\steamapps\common\Half-Life 2\hl2


REM -- DO NOT EDIT BELOW THIS LINE --

REM =-------------------------------=
REM Launch sln
REM =-------------------------------=
start "" "%~dp0\speedrun_demorecord.sln"
