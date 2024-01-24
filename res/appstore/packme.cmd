@rem This script creates the Rufus appxupload for upload to the Windows Store.
@rem It attemps to follow as closely as possible what Visual Studio does.
@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem if set, this will override the version for the package
rem set VERSION_OVERRIDE=4.4.2104.0

goto main

:ReplaceTokenInFile
setlocal EnableDelayedExpansion
set FILE=%~1
set TOKEN=%~2
set VALUE=%~3
for /f "delims=" %%i in ('type %FILE% ^& break ^> %FILE%') do (
  set "line=%%i"
  >>%FILE% echo(!line:%TOKEN%=%VALUE%!
)
endlocal
exit /B 0

:main
del /q *.appx >NUL 2>&1
del /q *.appxbundle >NUL 2>&1
del /q *.map >NUL 2>&1

set WDK_PATH=C:\Program Files (x86)\Windows Kits\10\bin\10.0.22000.0\x64
set ZIP_PATH=C:\Program Files\7-Zip
set SIGNATURE_SHA1=3dbc3a2a0e9ce8803b422cfdbc60acd33164965d
set MANIFEST=AppxManifest.xml
set ARCHS=x86 x64 arm arm64
set DEFAULT_SCALE=200
set OTHER_SCALES=100 125 150 400
set SCALED_IMAGES=LargeTile SmallTile Square44x44Logo Square150x150Logo StoreLogo Wide310x150Logo
rem All the languages listed below *MUST* match the ones from the Resources section of AppManifest.xml
rem Oh, and these must be *valid* codes that Microsoft accepts, else your users will get an error
rem (that the Microsoft validation and certification process will *NOT* catch) during install.
set DEFAULT_LANGUAGE=en-US
set ADDITIONAL_LANGUAGES=ar-SA bg-BG zh-CN zh-TW hr-HR cs-CZ da-DK nl-NL fi-FI fr-FR de-DE el-GR he-IL hu-HU id-ID it-IT ja-JP ko-KR lv-LV lt-LT ms-MY nb-NO fa-IR pl-PL pt-BR pt-PT ro-RO ru-RU sr-Latn-RS sk-SK sl-SI es-ES sv-SE th-TH tr-TR uk-UA vi-VN
set PACKAGE_IMAGES=^
 Square44x44Logo.altform-lightunplated_targetsize-16.png^
 Square44x44Logo.altform-lightunplated_targetsize-24.png^
 Square44x44Logo.altform-lightunplated_targetsize-256.png^
 Square44x44Logo.altform-lightunplated_targetsize-32.png^
 Square44x44Logo.altform-lightunplated_targetsize-48.png^
 Square44x44Logo.altform-unplated_targetsize-16.png^
 Square44x44Logo.altform-unplated_targetsize-256.png^
 Square44x44Logo.altform-unplated_targetsize-32.png^
 Square44x44Logo.altform-unplated_targetsize-48.png^
 Square44x44Logo.targetsize-16.png^
 Square44x44Logo.targetsize-24.png^
 Square44x44Logo.targetsize-24_altform-unplated.png^
 Square44x44Logo.targetsize-256.png^
 Square44x44Logo.targetsize-32.png^
 Square44x44Logo.targetsize-48.png

rem if you don't set the temp/tmp you get:
rem error MSB6001: Invalid command line switch for "CL.exe". System.ArgumentExcep Key being added: 'TEMP'
set temp=
set tmp=

cd /d "%~dp0"

for %%a in (%ARCHS%) do (
  if not exist rufus_%%a.exe (
    echo rufus_%%a.exe is missing from the current directory
    goto out
  )
)

rem exiftool.exe can't be installed in the Windows system directories...
if not exist exiftool.exe (
  echo exiftool.exe must exist in this directory
  goto out
)

rem Make sure we're not trying to create a package from an ALPHA or BETA version!
exiftool -s3 -*InternalName* rufus_x64.exe | findstr /C:"ALPHA" 1>nul && (
  echo Alpha version detected - ABORTED
  goto out
)
exiftool -s3 -*InternalName* rufus_x64.exe | findstr /C:"BETA" 1>nul && (
  echo Beta version detected - ABORTED
  goto out
)

rem Populate the version from the executable
if "%VERSION_OVERRIDE%"=="" (
  exiftool -s3 -*FileVersionNumber* rufus_x64.exe > version.txt
  set /p VERSION=<version.txt
  del version.txt
) else (
  echo WARNING: Forcing version to %VERSION_OVERRIDE%
  set VERSION=%VERSION_OVERRIDE%
)

echo Will create %VERSION% AppStore Bundle
pause

"%WDK_PATH%\signtool" sign /v /sha1 %SIGNATURE_SHA1% /fd SHA256 /tr http://sha256timestamp.ws.symantec.com/sha256/timestamp /td SHA256 *.exe
if ERRORLEVEL 1 goto out

echo [Files]> bundle.map

rem Now who the £$%^&* at Microsoft thought it was a good idea to have MakePri require '/dq lang-en-US_lang-fr-FR-...'
rem so that you actually end up with a <qualifier name="Language" value="en-US;fr-FR;..."/> in priconfig.xml?!?
rem Oh, and of course, good luck finding this documented ANYWHERE on Microsoft's website!
setlocal EnableDelayedExpansion
set STUPID_MAKEPRI_LANGUAGES=lang-%DEFAULT_LANGUAGE%
for %%l in (%ADDITIONAL_LANGUAGES%) do (
  set STUPID_MAKEPRI_LANGUAGES=!STUPID_MAKEPRI_LANGUAGES!_lang-%%l
)
setlocal DisableDelayedExpansion

for %%a in (%ARCHS%) do (
  echo.
  echo Creating Rufus_%VERSION%_%%a.appx...
  cd /d "%~dp0"
  echo "Rufus_%VERSION%_%%a.appx" "Rufus_%VERSION%_%%a.appx">> bundle.map
  mkdir %%a >NUL 2>&1
  cd %%a
  mkdir Images >NUL 2>&1
  for %%i in (%PACKAGE_IMAGES%) do (
    copy "..\Images\%%i" Images\ >NUL 2>&1
  )
  for %%i in (%SCALED_IMAGES%) do (
    copy "..\Images\%%i.scale-%DEFAULT_SCALE%.png" Images\ >NUL 2>&1
  )
  mkdir rufus
  copy "..\rufus_%%a.exe" "rufus\rufus.exe" >NUL 2>&1
  copy /y NUL "rufus\rufus.app" >NUL 2>&1
  rem When invoking MakePri, it is very important that you don't have files such as AppxManifest.xml or priconfig.xml
  rem in the directory referenced by /pr or you may get ERROR_MRM_DUPLICATE_ENTRY when validating the submission as,
  rem for instance, the 'AppxManifest.xml' from the 100% scale bundle will conflict the one from the x64 bundle.
  "%WDK_PATH%\MakePri" createconfig /o /pv 10.0.0 /cf ..\priconfig.xml /dq %STUPID_MAKEPRI_LANGUAGES%_scale-%DEFAULT_SCALE%_theme-light
  "%WDK_PATH%\MakePri" new /o /pr . /cf ..\priconfig.xml
  del /q ..\priconfig.xml
  copy ..\RufusAppxManifest.xml %MANIFEST% >NUL 2>&1
  call:ReplaceTokenInFile %MANIFEST% @ARCH@ %%a
  call:ReplaceTokenInFile %MANIFEST% @VERSION@ %VERSION%
  "%WDK_PATH%\MakeAppx" pack /o /d . /p ..\Rufus_%VERSION%_%%a.appx
  if ERRORLEVEL 1 goto out
)

for %%a in (%OTHER_SCALES%) do (
  echo.
  echo Creating Rufus_%VERSION%_scale-%%a.appx...
  cd /d "%~dp0"
  echo "Rufus_%VERSION%_scale-%%a.appx" "Rufus_%VERSION%_scale-%%a.appx">> bundle.map
  mkdir %%a >NUL 2>&1
  cd %%a
  mkdir Images >NUL 2>&1
  for %%i in (%SCALED_IMAGES%) do (
    copy "..\Images\%%i.scale-%%a.png" Images\ >NUL 2>&1
  )
  "%WDK_PATH%\MakePri" createconfig /o /pv 10.0.0 /cf ..\priconfig.xml /dq %STUPID_MAKEPRI_LANGUAGES%_scale-%%a_theme-light
  "%WDK_PATH%\MakePri" new /o /pr . /cf ..\priconfig.xml
  del /q ..\priconfig.xml
  copy ..\ScaleAppxManifest.xml %MANIFEST% >NUL 2>&1
  call:ReplaceTokenInFile %MANIFEST% @SCALE@ %%a
  call:ReplaceTokenInFile %MANIFEST% @VERSION@ %VERSION%
  "%WDK_PATH%\MakeAppx" pack /o /d . /p ..\Rufus_%VERSION%_scale-%%a.appx
)

setlocal EnableDelayedExpansion
set ALL_ARCHS=
for %%a in (%ARCHS%) do set ALL_ARCHS=!ALL_ARCHS!_%%a
cd /d "%~dp0"
"%WDK_PATH%\MakeAppx" bundle /f bundle.map /bv %VERSION% /p Rufus_%VERSION%%ALL_ARCHS%.appxbundle
rem Visual Studio zips the appxbundle into an appxupload for store upload, so we do the same...
"%ZIP_PATH%\7z" a -tzip Rufus_%VERSION%%ALL_ARCHS%_bundle.appxupload Rufus_%VERSION%%ALL_ARCHS%.appxbundle
endlocal

:out
cd /d "%~dp0"
for %%a in (%ARCHS%) do (
  rd /S /Q %%a >NUL 2>&1
)
for %%a in (%OTHER_SCALES%) do (
  rd /S /Q %%a >NUL 2>&1
)
del /q *.map >NUL 2>&1
del /q *.appx >NUL 2>&1
del /q *.appxbundle >NUL 2>&1
pause
exit
