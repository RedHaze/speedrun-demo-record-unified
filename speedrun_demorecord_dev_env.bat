@echo off
setlocal

REM =-------------------------------=
REM SDK paths
REM =-------------------------------=
set SDK_2013_DIR=E:\LLVM\source-sdk-2013\sp
set SDK_2007_DIR=E:\LLVM\hl2sdk

REM =-------------------------------=
REM Games
Rem =-------------------------------=
set GAME_2013_HL2_DIR=C:\Program Files (x86)\Steam\steamapps\common\Half-Life 2
set GAME_2007_HL2_DIR=C:\SourceUnpack\hl2_vanilla


REM -- DO NOT EDIT BELOW THIS LINE --

REM =-------------------------------=
REM Launch sln
REM =-------------------------------=
start "" "%~dp0\speedrun_demorecord.sln"
