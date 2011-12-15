@echo off
::# NB: You can pass the option FD to build a version of rufus that includes FreeDOS

if Test%BUILD_ALT_DIR%==Test goto usage

::# process commandline parameters
set FREEDOS=
if "%1" == "" goto no_more_args
::# /I for case insensitive
if /I Test%1==TestFD set C_DEFINES="/DWITH_FREEDOS"
:no_more_args

::# /M 2 for multiple cores
set BUILD_CMD=build -bcwgZ -M2
set PWD=%~dp0

::# Set target platform type
set ARCH_DIR=%_BUILDARCH%
if /I Test%_BUILDARCH%==Testx86 set ARCH_DIR=i386

cd src
if EXIST Makefile ren Makefile Makefile.hide

copy .msvc\rufus_sources sources >NUL 2>&1

@echo on
%BUILD_CMD%
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\%ARCH_DIR%\rufus.exe .. >NUL 2>&1

if EXIST Makefile.hide ren Makefile.hide Makefile
if EXIST sources del sources >NUL 2>&1

goto done

:builderror
if EXIST Makefile.hide ren Makefile.hide Makefile
if EXIST sources del sources >NUL 2>&1
echo Build failed
goto done

:usage
echo This command must be run in a Windows Driver Kit build environment.
echo See: http://msdn.microsoft.com/en-us/windows/hardware/gg487463
echo:
pause

:done
cd %PWD%