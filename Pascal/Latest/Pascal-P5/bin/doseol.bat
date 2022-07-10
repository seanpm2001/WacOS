@echo off
rem
rem Change all line endings to DOS mode
rem
echo.
echo Fixing the line endings on DOS files
echo.
flip -m source/*.pas

flip -m sample_programs/*.pas
flip -m sample_programs/*.cmp
flip -m sample_programs/*.inp

flip -m standard_tests/*.pas
flip -m standard_tests/*.cmp
flip -m standard_tests/*.inp
flip -m standard_tests/*.ecp

flip -m p2/*.pas
flip -m p2/*.cmp

flip -m p4/*.pas
flip -m p4/*.cmp
