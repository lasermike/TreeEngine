
@setlocal
set gamedir=..\Game
set dest=%1
set options=/XO
set files=graphdata.txt Courier_New_11.tga Arial_16.abc Arial_16.tga bark2.dds Bark_0005_diffuse.dds Courier_New_11.abc snow.dds  

..\..\misc\copyrobo %gamedir% %dest% %files% %options%
 
exit 0
