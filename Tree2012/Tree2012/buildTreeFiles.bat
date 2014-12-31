REM @echo off

REM %1 -- FXC, %2 -- OUTPUT folder, %3 -- input folder (project dir), %4 -- platform, %5 -- config
setlocal
call %3\commonshader.bat %1 %2 %3 %4 %5 %6 %7 %8

ECHO Building Tree.hlsl

set C=%cmdline% ..\Game\Tree.hlsl

%C% /EVS /Tvs_5_0  /Fo"%~2VS.cso"

%C% /EPS /Tps_5_0  /Fo"%~2PS.cso"

