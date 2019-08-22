@echo off
setlocal EnableExtensions DisableDelayedExpansion
set VERSION=3.7

del /q *.appx >NUL 2>&1
del /q *.appxbundle >NUL 2>&1
del /q *.map >NUL 2>&1

set WDK_PATH=C:\Program Files (x86)\Windows Kits\10\bin\10.0.18362.0\x64
set MSBUILD_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin
set MANIFEST=AppxManifest.xml
set ARCHS=x86 x64 arm arm64

cd /d "%~dp0"
setlocal EnableDelayedExpansion
set FILES_TO_SIGN=
for %%a in (%ARCHS%) do (
  echo [Building %%a]
  "%MSBUILD_PATH%\MSBuild" ..\..\rufus.sln /m /nologo /verbosity:minimal /t:Clean,Build /p:Configuration=Release,Platform=%%a
  if ERRORLEVEL 1 goto out
  echo.
  set FILES_TO_SIGN=!FILES_TO_SIGN! "..\..\%%a\Release\rufus.exe"
)
"%WDK_PATH%\SignTool" sign /v /sha1 9ce9a71ccab3b38a74781b975f1c228222cf7d3b /fd SHA256 /tr http://sha256timestamp.ws.symantec.com/sha256/timestamp %FILES_TO_SIGN%
if ERRORLEVEL 1 goto out
endlocal

echo [Files]> bundle.map

for %%a in (%ARCHS%) do (
  echo Creating %%a appx...
  cd /d "%~dp0"
  echo "Rufus-%%a.appx" "Rufus-%%a.appx">> bundle.map
  mkdir %%a >NUL 2>&1
  copy %MANIFEST% %%a\%MANIFEST% >NUL 2>&1
  cd %%a
  for /f "delims=" %%i in ('type %MANIFEST% ^& break ^> %MANIFEST%') do (
    set "line=%%i"
    setlocal EnableDelayedExpansion
    >>%MANIFEST% echo(!line:@ARCH@=%%a!
    endlocal
  )
  mkdir Assets >NUL 2>&1
  copy "..\..\icons\rufus-44.png" "Assets\Square44x44Logo.png" >NUL 2>&1
  copy "..\..\icons\rufus-48.png" "Assets\Square44x44Logo.targetsize-48.png" >NUL 2>&1
  copy "..\..\icons\rufus-48.png" "Assets\Square44x44Logo.targetsize-48_altform-unplated.png" >NUL 2>&1
  copy "..\..\icons\rufus-150.png" "Assets\Square150x150Logo.png" >NUL 2>&1
  copy "..\..\..\%%a\Release\rufus.exe" "rufus.exe" >NUL 2>&1
  "%WDK_PATH%\MakePri" createconfig /o /dq en-US /cf priconfig.xml
  "%WDK_PATH%\MakePri" new /o /pr . /cf priconfig.xml
  "%WDK_PATH%\MakeAppx" pack /o /d . /p ../Rufus-%%a.appx
  if ERRORLEVEL 1 goto out
)

cd /d "%~dp0"
"%WDK_PATH%\SignTool" sign /v /sha1 9ce9a71ccab3b38a74781b975f1c228222cf7d3b /fd SHA256 /tr http://sha256timestamp.ws.symantec.com/sha256/timestamp *.appx
if ERRORLEVEL 1 goto out
"%WDK_PATH%\MakeAppx" bundle /f bundle.map /p Rufus-%VERSION%.appxbundle
"%WDK_PATH%\SignTool" sign /v /sha1 9ce9a71ccab3b38a74781b975f1c228222cf7d3b /fd SHA256 /tr http://sha256timestamp.ws.symantec.com/sha256/timestamp Rufus-%VERSION%.appxbundle

:out
cd /d "%~dp0"
for %%a in (%ARCHS%) do (
  rd /S /Q %%a >NUL 2>&1
)
del /q *.map >NUL 2>&1
del /q *.appx >NUL 2>&1
rename *.appxbundle *.appx >NUL 2>&1
pause
exit
