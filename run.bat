@echo off

cls

clang -Wall -Wextra -IInclude main.c -o dfrmt.exe -lole32      

.\dfrmt.exe -d 1 -f exfat -t gpt -v nihhka

pause
cls