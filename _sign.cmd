@echo off
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.22000.0\x64\signtool" sign /v /sha1 3dbc3a2a0e9ce8803b422cfdbc60acd33164965d /fd SHA256 /tr http://sha256timestamp.ws.symantec.com/sha256/timestamp /td SHA256 %*
