@echo off
rem
rem test p2 compile and run
rem
echo.
echo Test compile and run of Pascal-P2

echo Compling pcomp to intermediate code
call compile p2\pcomp
if ERRORLEVEL 1 (

    echo *** Compile fails
    exit /b
    
)
rem
rem Copy the test file to the input file and compile it via interpreted P2 pcomp
rem
cp p2\roman.pas p2\pcomp.inp
echo Compiling test file with interpreted P2 pcomp
call run p2\pcomp
rem cat p2\pcomp.lst
rem
rem For neatness sake, copy out the intermediate to .p2 file
rem
cp p2\pcomp.out p2\roman.p2
rem
rem Compile pasint to intermediate code
rem
echo Compile pasint to intermediate code
call compile p2\pasint
if ERRORLEVEL 1 (

    echo *** Compile fails
    exit /b
    
)
rem
rem Add the final target program to the end of pasint.
rem This means that the version of pint will read and interpret
rem this.
rem
rem For those of you having fun reading this, yes, the next statement accurately
rem describes what is going on: we are concatenating and running two different
rem intermediate codes together in completely different formats!
rem
cat p2\pasint.p5 p2\roman.p2 > tmp.p5
rm p2\pasint.p5
mv tmp.p5 p2\pasint.p5
rem
rem Create null input file
rem
echo.>p2\pasint.inp
rem
rem Now run pasint on pint, which runs the test program.
rem
echo Run pasint on pint, to run test program
call run p2\pasint
rem
rem Copy the result listing back to roman.lst, again for neatness
rem
cp p2\pasint.lst p2\roman.lst
rem
rem Now compare with reference
rem
echo Checking against reference file
call diffnole p2\roman.lst p2\roman.cmp > p2\roman.dif
rem
rem Show the file, so if the length is zero, it compared ok.
rem
call :passfail p2\roman.dif
del p2\pcomp.inp
del p2\pasint.inp
goto :exit

:passfail
if %~z1 == 0 (

    echo PASSED
    
) else (

    echo *** FAILED ***
    
)
goto :eof

:exit
