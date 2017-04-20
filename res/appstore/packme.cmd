@echo off
set VERSION=2.15
echo [Files]>mappings.lst
echo "..\icon-set\rufus-44.png"     "Assets\Rufus-44.png">>mappings.lst
echo "..\icon-set\rufus-48.png"     "Assets\Rufus-48.png">>mappings.lst
echo "..\icon-set\rufus-150.png"    "Assets\Rufus-150.png">>mappings.lst
echo "..\..\rufus-%VERSION%.exe"    "rufus.exe">>mappings.lst
echo "AppxManifest.xml"             "AppxManifest.xml">>mappings.lst
"C:\Program Files (x86)\Windows Kits\10\bin\x64\MakeAppx" pack /o /f mappings.lst /p Rufus-%VERSION%.appx
if ERRORLEVEL 1 goto out
"C:\Program Files (x86)\Windows Kits\10\bin\x64\SignTool" sign /v /sha1 5759b23dc8f45e9120a7317f306e5b6890b612f0 /fd SHA256 /tr http://timestamp.comodoca.com/rfc3161 /td SHA256 Rufus-%VERSION%.appx
:out
del mappings.lst
pause
exit
