@setlocal
set gamedir=..\Game
set dest=%1
set options=/XO
set files=graphdata.txt Courier_New_11.tga Arial_16.abc Arial_16.tga bark2.dds Bark_0005_diffuse.dds Courier_New_11.abc snow.dds FirBranchWithNeedles.dds 
set dirs=Resources

mkdir %dest%

call ..\..\misc\copyrobo %gamedir% %dest% %files% %options%

call ..\..\misc\copyrobo %gamedir%\Resources %dest% %options%

 
