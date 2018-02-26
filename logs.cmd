@echo off

set TEST=logs
set LOGFILE=C:\MrBig\testfile.txt
set BADTEXT=failure
set PICKUPDIR=C:\temp\pickup
set TMPFILE=C:\temp\%TEST%.%RANDOM%
set COLOR=red

findstr %BADTEXT% %LOGFILE% > %TMPFILE%
if errorlevel 1 set COLOR=green
echo %COLOR% > %PICKUPDIR%/%TEST%
type %TMPFILE% >> %PICKUPDIR%/%TEST%
del %TMPFILE%
