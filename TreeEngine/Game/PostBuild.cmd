@SETLOCAL EnableDelayedExpansion
@echo on
set binaryDir=%1
set layoutDir=%2
set dllProjectList=%3

call ..\Game\CopyResources.cmd %layoutDir%\
for /f %%G in ("%3") DO (
	call ..\..\Misc\copyrobo %binaryDir%\%%G %layoutDir% %%G.dll %%G.pdb
)
 
