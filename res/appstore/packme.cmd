@echo off
set VERSION=3.0

rem Make sure you don't have anything you don't want included in the package, as anything residing in the
rem current directory will be included, including any previous .appx, which makes for nice recursion...
del /q *.appx >NUL 2>&1

rem As per the link below, you need "altform-unplated" icons and run MakePri to get transparent icons:
rem https://social.msdn.microsoft.com/Forums/windowsapps/en-US/dc505f68-d120-43e3-a9e1-d7c77746d588/uwpdesktop-bridgeunplated-taskbar-icons-in-desktop-bridge-apps
mkdir Assets >NUL 2>&1
copy "..\icon-set\rufus-44.png"  "Assets\Square44x44Logo.png"
copy "..\icon-set\rufus-48.png"  "Assets\Square44x44Logo.targetsize-48.png"
copy "..\icon-set\rufus-48.png"  "Assets\Square44x44Logo.targetsize-48_altform-unplated.png"
copy "..\icon-set\rufus-150.png" "Assets\Square150x150Logo.png"
copy "..\..\rufus-%VERSION%.exe" "rufus.exe"
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.16299.0\x64\MakePri" createconfig /o /dq en-US /cf priconfig.xml
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.16299.0\x64\MakePri" new /o /pr . /cf priconfig.xml
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.16299.0\x64\MakeAppx" pack /o /d . /p Rufus-%VERSION%.appx
if ERRORLEVEL 1 goto out
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.16299.0\x64\SignTool" sign /v /sha1 9ce9a71ccab3b38a74781b975f1c228222cf7d3b /fd SHA256 /tr http://timestamp.comodoca.com/rfc3161 /td SHA256 Rufus-%VERSION%.appx
:out
del /q rufus.exe
del /q priconfig.xml
del /q resources.pri
rmdir /s /q Assets
pause
exit
