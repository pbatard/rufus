echo on
SetLocal EnableDelayedExpansion

if [%platform%] == [x86_64] call "C:\Program Files\Microsoft SDKs\Windows\v7.1\Bin\SetEnv.cmd" /%configuration% /x64
msbuild rufus.sln /m /p:Configuration=%configuration%,Platform=%platform% /logger:"C:\Program Files\AppVeyor\BuildAgent\Appveyor.MSBuildLogger.dll"
