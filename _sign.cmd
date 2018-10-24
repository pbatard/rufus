@echo off
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.17134.0\x64\signtool" sign /v /sha1 9ce9a71ccab3b38a74781b975f1c228222cf7d3b /fd SHA256 /tr http://sha256timestamp.ws.symantec.com/sha256/timestamp %1
exit
