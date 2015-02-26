@rem echo off
echo CMD: %*
@REM %1 -- FXC, %2 -- OUTPUT folder, %3 -- layout folder, %4 -- input file (in project dir), %5 -- platform, %6 -- config
@SETLOCAL EnableDelayedExpansion

set inputFile=%4
set layoutFolder=%3

call %~p4\commonshader.bat %1 %2 %3 %4 %5 %6 %7 %8
@rem echo off

@rem format:filename,vs/ps,entry_point
FOR /F "tokens=1,2,3 delims=," %%G IN (%~p4ShaderFiles.txt) DO (
  if %%G==%~nx4 call :BuildShader %4 %%H %%I %2
)

goto ENDOFSCRIPT

:BuildShader 
@rem %1 = inputfile, %2 = stage, %3 = entrypoint %4 = Output dir
  echo BuildShader: %*
  if %2==vs (
    set target=vs_5_0
	set suffix=VS
  ) ELSE (
    set target=ps_5_0
	set suffix=PS
  )
  set outputfile=%~4/%~n3.cso
  ECHO Building %1 for %target%  
  set finalcmd=%cmdline% %1 /T%target% /E%3 /Fo"%outputfile%"
  echo !finalcmd!
  call !finalcmd!
  if ERRORLEVEL 1 goto ENDOFSCRIPT
rem  copy "%outputfile%" %layoutFolder%
  goto :EOF

:ENDOFSCRIPT
if ERRORLEVEL 1 (
echo Fail > %~2/builderr.txt
echo Shader compilation FAIL
) ELSE (
echo OK > %~2/%~nx4.txt
echo All Shaders successfully compiled
)

