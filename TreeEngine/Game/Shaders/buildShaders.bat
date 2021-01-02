SETLOCAL ENABLEDELAYEDEXPANSION

echo CMD: %*
@REM %1 -- FXC/DXC, %2 -- OUTPUT folder, %3 -- layout folder, %4 -- input file (in project dir), %5 -- platform, %6 -- config, %7 -- manifest filename -- incrementa flag
@SETLOCAL EnableDelayedExpansion
@set layoutFolder=%3
@set inputFile=%4
@set AnyErrors=0

set manifestFilename=%7

call %~dp4commonshader.bat %1 %2 %3 %4 %5 %6 %7 %8

@rem Setup for hot recompile
@set incremental=0


@call :ColorInit

@rem Build from each entry point in shader
@rem format:filename,vs/ps,entry_point
@FOR /F "tokens=1,2,3 delims=," %%G IN (%~dp4!manifestFilename!) DO (
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
  set target=vs_6_0
  set suffix=VS
) ELSE if %2==ps (
  set target=ps_6_0
  set suffix=PS
) ELSE if %2==lib (
  set target=lib_6_3
  set suffix=lib
) ELSE (
  set target=cs_6_0
  set suffix=CS
)

@rem Compile!
set outputfile=%~4%~n3.cso
ECHO Building %1 for %target%  
set finalcmd=%cmdline% %1 /T%target% /Fo"%outputfile%"
if not [!suffix!] == [lib] (
	set finalcmd=!finalcmd! /E%3
)

echo !finalcmd!
call !finalcmd!

@if ERRORLEVEL 1 goto ENDOFSCRIPT

@rem Copy output to deployment directory (AppX)
if not exist %layoutFolder% ( 
mkdir %layoutFolder%
xcopy "%outputfile%" %layoutFolder% /y
)

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
