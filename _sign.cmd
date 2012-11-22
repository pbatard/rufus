:retry
@set /p password=Please enter PFX password: 
@"C:\Program Files (x86)\Windows Kits\8.0\bin\x64\signtool" sign /v /f D:\Secured\akeo\akeo.p12 /p %password% /tr http://timestamp.comodoca.com/rfc3161 %1 %2 %3 %4
@if ERRORLEVEL 1 goto retry
@set password=
@exit
