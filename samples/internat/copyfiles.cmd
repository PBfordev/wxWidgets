@echo off

REM Copies sample data files to the folder with its executable.
REM Called from MSVS as the sample's post-build step where the first
REM argument is the project folder with data files and the second
REM the folder where the executable is built. Both folder names must
REM end with a backslash.

IF "%2" EQU "" GOTO err_args
IF "%3" NEQ "" GOTO err_args

xcopy %1ar %2ar\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1bg %2bg\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1cs %2cs\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1de %2de\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1fr %2fr\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1it %2it\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1ja %2ja\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1ja_JP.EUC-JP %2ja_JP.EUC-JP\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1ka %2ka\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1pl %2pl\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1ru %2ru\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1sv %2sv\ /D /Y /S /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

:success
exit /B 0

:err_args
echo Error: Must be called with 2 arguments, see the comments at the top of the file.
exit /B -1

:err_copy
echo Error: Could not copy all sample files.
exit /B %errorlevel%