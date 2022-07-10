@echo off
rem
rem Change all line endings to Unix mode
rem
echo.
echo Fixing the line endings on Unix files
echo.
flip -u source/*.pas

flip -u sample_programs/*.pas
flip -u sample_programs/*.cmp
flip -u sample_programs/*.inp

flip -u standard_tests/*.pas
flip -u standard_tests/*.cmp
flip -u standard_tests/*.inp

flip -u p2/*.pas
flip -u p2/*.cmp

flip -u p4/*.pas
flip -u p4/*.cmp
