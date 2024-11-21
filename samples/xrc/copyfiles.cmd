@echo off

REM Copies sample data files to the folder with its executable.
REM Called from MSVS as the sample's post-build step where the first
REM argument is the project folder with data files and the second
REM the folder where the executable is built. Both folder names must
REM end with a backslash.

IF "%2" EQU "" GOTO err_args
IF "%3" NEQ "" GOTO err_args

xcopy %1rc\aui.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\aui.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\artprov.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\artprov.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\basicdlg.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\basicdlg2.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\basicdlg.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\controls.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\controls.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\custclas.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\custclas2.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\custclas.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\derivdlg.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\derivdlg2.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\derivdlg.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\fileopen.gif %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\filesave.gif %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\frame.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\fuzzy.gif %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\menu.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\platform.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\platform.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\quotes.gif %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\resource.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\toolbar.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\uncenter.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\uncenter2.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\objref.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\objrefdlg.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\uncenter.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\update.gif %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\variable.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\variable.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\variants.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\variants.xrc %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\throbber.gif %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\stop.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\stop_2x.xpm %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

xcopy %1rc\wxbanner.gif %2rc\ /D /Y /Q > nul
IF %ERRORLEVEL% NEQ 0 GOTO err_copy

:success
exit /B 0

:err_args
echo Error: Must be called with 2 arguments, see the comments at the top of the file.
exit /B -1

:err_copy
echo Error: Could not copy all sample files.
exit /B %errorlevel%