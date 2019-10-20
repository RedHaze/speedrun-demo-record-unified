@echo off

pushd "%~dp0"

:: Setup venv
python -m venv venv

:: Activate
call venv/Scripts/activate.bat

:: Upgrade pip & setuptools
python -m pip install --upgrade pip setuptools

:: Install requirements
pip install -r requirements.txt
