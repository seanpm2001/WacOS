@echo off
rem
rem Change version numbers on all compare files
rem
if not "%1"=="" goto paramok1
echo *** Error: Missing "from" version number
goto stop
:paramok1
if not "%2"=="" goto paramok2
echo *** Error: Missing "to" version number
goto stop
:paramok2

echo Changing version number %1 to %2

set List=sample_programs\*.cmp
for /f "delims=" %%a in ('dir /b "%List%"') do (

    call chgver sample_programs\%%~na.cmp %1 %2

)

set List=sample_programs\*.ecp
for /f "delims=" %%a in ('dir /b "%List%"') do (

    call chgver sample_programs\%%~na.ecp %1 %2

)

set List=standard_tests\*.cmp
for /f "delims=" %%a in ('dir /b "%List%"') do (

    call chgver standard_tests\%%~na.cmp %1 %2

)

set List=standard_tests\*.ecp
for /f "delims=" %%a in ('dir /b "%List%"') do (

    call chgver standard_tests\%%~na.ecp %1 %2

)

:stop