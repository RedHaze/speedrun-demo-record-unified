@echo off
setlocal

pushd "%~dp0"

call ..\speedrun_demorecord_dev_env.bat exit

if exist "venv\Scripts\activate.bat" goto :activate_venv

:: Setup venv
call setup_venv.bat
goto :run_tests

:activate_venv
call venv\Scripts\activate.bat

:: Run tests
:run_tests
pytest -s .
