@echo off

cls

clang -Wall -Wextra -IInclude main.c -o dfrmt.exe -lole32      

.\dfrmt.exe -d 2 -f exfat -t gpt

pause
cls