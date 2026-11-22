@echo off
title Talk to STAN - Autonomous Desk Pet
cd /d "%~dp0"
python "%~dp0tools\talk_to_pet.py" %*
if "%1"=="" pause
