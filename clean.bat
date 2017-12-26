@ECHO OFF
START git clean -fXd

for %%a in (".") do set PROJ_NAME=%%~na

set CUR_DIR=%~dp0
set DEST_DIR=%~dp0..\%PROJ_NAME%_clean

REM echo CUR_DIR=%CUR_DIR%
REM echo PROJ_NAME=%PROJ_NAME%
REM echo DEST_DIR=%DEST_DIR%


IF EXIST %DEST_DIR% RMDIR %DEST_DIR%
MKDIR %DEST_DIR%

ROBOCOPY %CUR_DIR% %DEST_DIR% /xd %CUR_DIR%\.git /xf %CUR_DIR%.gitattributes /xf %CUR_DIR%.gitignore /xf %CUR_DIR%clean.bat

