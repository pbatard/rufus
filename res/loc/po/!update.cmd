@echo off
if not exist pollock.exe curl https://rufus-web.akeo.ie/locale/pollock.exe --output pollock.exe
cls
:menu
echo 1 - Import .po into .loc
echo 2 - Create .po from .loc
echo 3 - Exit
choice /N /C:123%1
if ERRORLEVEL==3 goto exit
if ERRORLEVEL==2 (
  pollock.exe -l
  goto menu
)
if ERRORLEVEL==1 (
  pollock.exe -i
  goto menu
)
:exit
del *.pot 2>NUL:
del *.mo 2>NUL: