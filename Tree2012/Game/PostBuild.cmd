@SETLOCAL EnableDelayedExpansion
@echo on
set binaryDir=%1
set layoutDir=%2

call ..\Game\CopyResources.cmd %layoutDir%\
call ..\..\Misc\copyrobo %binaryDir%\RenderPlatform11UWP\ %layoutDir%\ RenderPlatform11UWP.dll
call ..\..\Misc\copyrobo %binaryDir%\RenderPlatform12UWP\ %layoutDir%\ RenderPlatform12UWP.dll
 
