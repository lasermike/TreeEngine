
@setlocal
set gamedir=..\Game
set dest=%1
set options=/XO
set files=Arial_16.abc Arial_16.tga bark2.dds Bark_0005_diffuse.dds Courier_New_11.abc Courier_New_11.tga snow.dds 

robocopy %gamedir% %dest% %files% %options%

exit 0
