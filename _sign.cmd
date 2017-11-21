@echo off
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.16299.0\x64\signtool" sign /v /sha1 5759b23dc8f45e9120a7317f306e5b6890b612f0 /fd SHA256 /tr http://timestamp.comodoca.com/rfc3161 /td SHA256 %1
exit
