@echo off
:retry
set /p password=Please enter PFX password: 
"C:\Program Files (x86)\Windows Kits\8.1\bin\x64\signtool" sign /v /fd SHA1 /f D:\Secured\akeo\sha1\akeo.p12 /p %password% /tr http://timestamp.comodoca.com/rfc3161 /td SHA1 %1
if ERRORLEVEL 1 goto retry
"C:\Program Files (x86)\Windows Kits\8.1\bin\x64\signtool" sign /as /v /fd SHA256 /f D:\Secured\akeo\akeo.p12 /p %password% /tr http://timestamp.comodoca.com/rfc3161 /td SHA256 %1
if ERRORLEVEL 1 goto retry
set password=
exit
