echo on
SetLocal EnableDelayedExpansion

msbuild rufus.sln /m /p:Configuration=%configuration%,Platform=%platform% /logger:"C:\Program Files\AppVeyor\BuildAgent\Appveyor.MSBuildLogger.dll"
