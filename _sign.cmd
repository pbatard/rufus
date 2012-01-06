:retry
@set /p password=Please enter PFX password: 
@E:\WinDDK\7600.16385.0\bin\amd64\signtool sign /v /f D:\Secured\akeo\pbatard.p12 /p %password% /t http://time.certum.pl %1 %2 %3 %4
@if ERRORLEVEL 1 goto retry
@set password=
@exit
