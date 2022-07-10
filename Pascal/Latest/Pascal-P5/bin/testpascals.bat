@echo off
rem
rem Script to test pascals
rem
rem Compile pascals
rem
echo.
echo Test compile and run of Pascal-S

echo Compiling Pascal-S
call compile sample_programs\pascals
if ERRORLEVEL 1 (

    echo *** Compile failed
    exit /b
    
)
rem
rem Prepare a prd deck that has the pascals intermediate first, followed by the
rem program to run.
rem
cat sample_programs\pascals.p5 sample_programs\roman.pas > sample_programs\tmp.p5
rm sample_programs\pascals.p5
cp sample_programs\tmp.p5 sample_programs\pascals.p5
rm sample_programs\tmp.p5
rem
rem Run that
rem
echo Runing interpreted Pascal-S on test program
call run sample_programs\pascals
rem
rem Compare to reference
rem
echo Checking against reference file
call diffnole sample_programs\pascals.lst sample_programs\pascals.cmp > sample_programs\pascals.dif
rem
rem Show the file, so if the length is zero, it compared ok.
rem
call :passfail sample_programs\pascals.dif
goto :exit

:passfail
if %~z1 == 0 (

    echo PASSED
    
) else (

    echo *** FAILED ****
    
)
goto :eof

:exit