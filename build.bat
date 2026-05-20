@echo off
REM Build minihotkey.exe
REM Requires MinGW (gcc) in PATH
REM For MSVC:  rc minihotkey.rc && cl /O2 minihotkey.c minihotkey.res /link user32.lib gdi32.lib shell32.lib /SUBSYSTEM:WINDOWS

windres minihotkey.rc -o minihotkey_res.o
gcc -O2 -mwindows minihotkey.c minihotkey_res.o -o minihotkey.exe -luser32 -lgdi32 -lshell32

if %errorlevel% == 0 (
    echo Build OK: minihotkey.exe
) else (
    echo Build FAILED
)
