@echo off

cls

clang -Os -Wall -Wextra -IInclude main.c -o dfrmt.exe -lole32      

.\dfrmt.exe

pause
cls