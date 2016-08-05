echo on
SetLocal EnableDelayedExpansion

if [%configuration%] == [Debug] exit 0

set bash=C:\msys64\usr\bin\bash
set arch=i686
if [%platform%] == [x86_64] set arch=x86_64

%bash% -e -l -c "mkdir %platform%"
%bash% -e -l -c ./bootstrap.sh
%bash% -e -l -c "cd %platform%"
%bash% -e -l -c "./configure --prefix=/mingw32 --build=%arch%-w64-mingw32 --host=%arch%-w64-mingw32"
%bash% -e -l -c "make -j4"
