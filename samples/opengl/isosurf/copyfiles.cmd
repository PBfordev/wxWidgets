@echo off

REM Copies sample data files to the folder with its executable.
REM Called from MSVS as the sample's post-build step where the first
REM argument is the project folder with data files and the second
REM the folder where the executable is built. Both folder names must
REM end with a backslash.

IF "%2" EQU "" GOTO err_args
IF "%3" NEQ "" GOTO err_args

xcopy %1isosurf.dat.gz %2 /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

:success
exit /B 0

:err_args
echo Error: Must be called with 2 arguments, see the comments at the top of the file.
exit /B -1

:err_copy
echo Error: Could not copy all sample files.
exit /B %errorlevel%