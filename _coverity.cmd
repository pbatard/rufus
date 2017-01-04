@echo off
set COV_DIR=E:\cov-analysis-win32-7.7.0.4
set PATH=%PATH%;%COV_DIR%\bin
set PWD=%~dp0
rmdir cov-int /s /q >NUL 2>NUL
del cov-int.zip >NUL 2>NUL
mkdir cov-int
rem *** for when/if Coverity manage to clean their act
rem cov-build --dir cov-int msbuild rufus.sln /p:Configuration=Release,Platform=x86_32 /maxcpucount
rem cov-build --dir cov-int C:\msys64\usr\bin\bash -cl "export PATH=/mingw32/bin:$PATH; cd /c/rufus; ./configure --build=i686-w64-mingw32 --host=i686-w64-mingw32 --disable-debug; make -j4"
cov-build --dir cov-int wdk_build.cmd
rem *** zip script by Peter Mortensen - http://superuser.com/a/111266/286681
echo Set objArgs = WScript.Arguments> zip.vbs
echo InputFolder = objArgs(0)>> zip.vbs
echo ZipFile = objArgs(1)>> zip.vbs
echo CreateObject("Scripting.FileSystemObject").CreateTextFile(ZipFile, True).Write "PK" ^& Chr(5) ^& Chr(6) ^& String(18, vbNullChar)>> zip.vbs
echo Set objShell = CreateObject("Shell.Application")>> zip.vbs
echo Set source = objShell.NameSpace(InputFolder)>> zip.vbs
echo objShell.NameSpace(ZipFile).CopyHere(source)>> zip.vbs
echo wScript.Sleep 2000>> zip.vbs
CScript zip.vbs %PWD%cov-int %PWD%cov-int.zip
del zip.vbs
