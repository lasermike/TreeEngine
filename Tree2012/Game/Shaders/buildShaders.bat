@echo off
@REM %1 -- FXC, %2 -- OUTPUT folder, %3 -- layout folder, %4 -- input folder (project dir), %5 -- platform, %6 -- config
@SETLOCAL EnableDelayedExpansion

del "%~2/builderr.txt"
del "%~2/status.txt"

call %4\commonshader.bat %1 %2 %3 %4 %5 %6 %7 %8
@echo off


@rem format:filename,vs/ps,entry_point
FOR /F "tokens=1,2,3 delims=," %%G IN (%4\ShaderFiles.txt) DO (
  if %%H==vs (
    set target=vs_5_0
	set suffix=VS
  ) ELSE (
    set target=ps_5_0
	set suffix=PS
  )
  ECHO Building %%G !suffix!
  set finalcmd=%cmdline% %4\%%G /T!target! /E%%I /Fo"%~2/%%~nI.cso"
  echo !finalcmd!
  call !finalcmd!
  if ERRORLEVEL 1 goto ENDOFSCRIPT
  copy "%~2/%%~nI.cso" %3
)

:ENDOFSCRIPT
if ERRORLEVEL 1 (
echo Fail > %~2/builderr.txt
echo Shader compilation FAIL
) ELSE (
echo OK > %~2/status.txt
echo All Shaders successfully compiled
)