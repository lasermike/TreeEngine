SETLOCAL ENABLEDELAYEDEXPANSION

@REM %1 -- FXC/DXC, %2 -- OUTPUT folder, %3 -- layout folder, %4 -- input file (in project dir), %5 -- platform, %6 -- config, %7 -- manifest filename 
@SETLOCAL EnableDelayedExpansion

@set AnyErrors=0

set compiler=%1
set outputFolder=%2
@set layoutFolder=%3
@set inputFile=%4
set platform=%5
set config=%6
set manifestFilename=%7

echo *BuildShaders
echo  InputFile     !inputFile!
echo  Compiler      !compiler!
echo  Output folder !outputFolder!
echo  Layout folder !layoutFolder!
echo  Platform:     !platform!
echo  config:       !config!
echo  Manifest      !manifestFilename!

@rem Setup for hot recompile
@set incremental=0

@rem Build from each entry point in shader
@rem format:filename,vs/ps,entry_point
@FOR /F "tokens=1,2,3 delims=," %%G IN (%~dp4!manifestFilename!) DO (
  if %%G==%~nx4 call :BuildShader !InputFile! %%H %%I !outputFolder!
)

@if %AnyErrors%==1 (
	call :SetError
)

@goto :ENDOFSCRIPT
@goto :EOF

@rem ***************************************************************************
:BuildShader 
@rem %1 = inputfile, %2 = stage, %3 = entrypoint %4 = Output dir
echo  BuildShader inputFile  %1
echo  BuildShader stage      %2
echo  BuildShader entryPoint %3
echo  BuildShader outputDir  %4

set copyToLayout=1

@if %2==vs (
  set target=vs_6_0
  set suffix=VS
) ELSE if %2==ps (
  set target=ps_6_0
  set suffix=PS
) ELSE if %2==lib (
  set target=lib_6_3
  set suffix=lib
  set copyToLayout=0
) ELSE if %2==rootsig (
  set target=rootsig_1_1
  set suffix=inc
  set copyToLayout=0
) ELSE (
  set target=cs_6_0
  set suffix=CS
)

ECHO  BuildShader target !target!

@rem Compile!
set cmdline=!compiler! /Zi /T %target% /E %3 

if !platform! == "Gaming.Xbox.Scarlett.x64" (
   echo  Compiling shaders for Scarlett...
   set cmdline=!cmdline! /Fc !outputFolder!%3.cso.txt 
) else (
	rem set cmdline=!cmdline!
)

if [%2] == [rootsig] (
	set cmdline=!cmdline! /Fh !outputFolder!%3.inc 
) else if [%2] == [lib] (
	set cmdline=!cmdline! /Fh !outputFolder!%3.inc /Fd !outputFolder!%3.pdb /Vn g_%3 
) else (
	set outputfile=%~4%~n3.cso
	set cmdline=!cmdline! /Zpr /Od /Fo "!outputfile!" /Fd !outputFolder!%3.pdb
)

set finalcmd=!cmdline! !inputFile!

rem SET count=1
rem FOR /F "tokens=* USEBACKQ" %%F IN (`where dxc`) DO (
rem   SET var!count!=%%F
rem   SET /a count=!count!+1
rem )
rem ECHO %var1%

echo !finalcmd!
call !finalcmd!

@if ERRORLEVEL 1 goto ENDOFSCRIPT

echo Succeeded

@rem Copy output to deployment directory (AppX)
if [!copyToLayout!]==[1] (
	echo Copying !outputfile! to layout folder %layoutFolder%
	if not exist %layoutFolder% ( 
		mkdir %layoutFolder%
	)
	xcopy "!outputfile!" %layoutFolder% /y
)

@goto :EOF
@rem END BuildShader ***********************************************************************


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
	exit /b %ERRORLEVEL%
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
