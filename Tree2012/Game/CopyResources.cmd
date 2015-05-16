
@setlocal
set gamedir=..\Game
set dest=%1


robocopy %gamedir% %dest% Arial_16.abc Arial_16.tga bark2.dds Courier_New.abc Courier_New.tga snow.dds /XO

