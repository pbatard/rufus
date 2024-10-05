@echo off
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.22000.0\x64\signtool" sign /v /sha1 fc4686753937a93fdcd48c2bb4375e239af92dcb /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 %*
