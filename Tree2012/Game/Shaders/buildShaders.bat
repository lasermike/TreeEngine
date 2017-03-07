@rem echo off
echo CMD: %*
@REM %1 -- FXC, %2 -- OUTPUT folder, %3 -- layout folder, %4 -- input file (in project dir), %5 -- platform, %6 -- config, %7 -- incrementa flag
@SETLOCAL EnableDelayedExpansion
@set layoutFolder=%3
@set inputFile=%4
@set AnyErrors=0

call %~p4\commonshader.bat %1 %2 %3 %4 %5 %6 %7 %8

@rem Setup for hot recompile
@set incremental=0
@if "%7"=="1" set incremental=1

@if "%incremental%"=="0" (
    echo Incremental shader build
	@rem Output a batch file to rebuild this shader file
	@rem echo %~dp4 > %2%\shaderSrcPath.txt
	@copy %0 %2
	echo buildShaders.bat %1 %2 %3 %4 %5 %6 1 > %2build%~n4.cmd
)

@rem Color init
@call :ColorInit

@rem Build from each entry point in shader
@rem format:filename,vs/ps,entry_point
@FOR /F "tokens=1,2,3 delims=," %%G IN (%~p4ShaderFiles.txt) DO (
  if %%G==%~nx4 call :BuildShader %4 %%H %%I %2
)

@if %AnyErrors%==1 (
	call :SetError
)

@goto :ENDOFSCRIPT
@goto :EOF

:BuildShader 
@rem %1 = inputfile, %2 = stage, %3 = entrypoint %4 = Output dir
@echo BuildShader: %*
@if %2==vs (
  set target=vs_5_0
  set suffix=VS
) ELSE (
  set target=ps_5_0
  set suffix=PS
)

@rem Compile!
@set outputfile=%~4%~n3.cso
@ECHO Building %1 for %target%  
@set finalcmd=%cmdline% %1 /T%target% /E%3 /Fo"%outputfile%"
@echo !finalcmd!
call !finalcmd!

@if ERRORLEVEL 1 goto ENDOFSCRIPT

@rem Copy output to deployment directory (AppX)
copy "%outputfile%" %layoutFolder%

@goto :EOF

@rem Subroutines
:ENDOFSCRIPT
@if ERRORLEVEL 1 (
	@set AnyErrors=1
	echo Fail > %~2/builderr.txt
	if "%incremental%"=="1" (
		call :ColorText 04 "Shader compilation FAIL"
		echo.
	) ELSE (
		echo Shader compilation FAIL
	)
) else (
	echo OK > %~2/%~nx4.txt
	if "%incremental%"=="1" (
		call :ColorText 02 "All Shaders successfully compiled"
		echo.
	) ELSE (
		echo All Shaders successfully compiled
	)
)
goto :EOF


:ColorText
echo off
<nul set /p ".=%DEL%" > "%~2"
findstr /v /a:%1 /R "^$" "%~2" nul
del "%~2" > nul 2>&1
goto :eof

:ColorInit
@echo off
@for /F "tokens=1,2 delims=#" %%a in ('"prompt #$H#$E# & echo on & for %%b in (1) do rem"') do (
  set "DEL=%%a"
)

:SetError
@exit /b 1
