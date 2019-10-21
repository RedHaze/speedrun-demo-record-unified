@echo off

REM =-------------------------------=
REM SDKs
REM =-------------------------------=
set SDK_2013_DIR=E:\LLVM\source-sdk-2013\sp
set SDK_2007_DIR=E:\LLVM\hl2sdk
set SDK_2007_LEAK_DIR=E:\LLVM\SourceEngine2007\src_main
set SDK_2006_DIR=E:\LLVM\hl2sdk

REM =-------------------------------=
REM Games
Rem =-------------------------------=
set GAME_2013_HL2_DIR=C:\Program Files (x86)\Steam\steamapps\common\Half-Life 2
set GAME_2007_HL2_DIR=C:\SourceUnpack\hl2_vanilla
set GAME_2006_HL2_DIR=C:\SourceUnpack\Half-Life 2 2009
set GAME_2013_PORTAL_DIR=E:\SteamLibrary\steamapps\common\Portal
set GAME_2007_PORTAL_DIR=C:\SourceUnpack\Portal
set GAME_2007_EP2_DIR=C:\SourceUnpack\ep2

REM -- DO NOT EDIT BELOW THIS LINE --

REM =-------------------------------=
REM Launch sln if no args
REM =-------------------------------=
if [%1] NEQ [] goto arb
start "" "%~dp0\speedrun_demorecord.sln"
goto :eof

:arb
start "" %*
