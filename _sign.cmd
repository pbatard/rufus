@echo off
:retry_sha1
"C:\Program Files (x86)\Windows Kits\8.1\bin\x64\signtool" sign /v /sha1 655f6413a8f721e3286ace95025c9e0ea132a984 /fd SHA1 /tr http://timestamp.comodoca.com/rfc3161 /td SHA1 %1
if ERRORLEVEL 1 goto retry_sha1
:retry_sha256
"C:\Program Files (x86)\Windows Kits\8.1\bin\x64\signtool" sign /as /v /sha1 5759b23dc8f45e9120a7317f306e5b6890b612f0 /fd SHA256 /tr http://timestamp.comodoca.com/rfc3161 /td SHA256 %1
if ERRORLEVEL 1 goto retry_sha256
exit
