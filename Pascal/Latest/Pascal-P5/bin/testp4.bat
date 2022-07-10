@echo off
rem
rem Script to test p4 compile and run
rem
rem Compile p4
rem
echo.
echo Test compile and run of Pascal-P4

rem
rem Compile p4
rem
echo Compling pcom to intermediate code
call compile p4\pcom
if ERRORLEVEL 1 (

    echo *** Compile failed
    exit /b
    
)
rem
rem Copy the test file to the input file and compile it via interpreted p4
rem
cp p4\standardp.pas p4\pcom.inp
echo Compiling test file with interpreted P4 pcomp
call run p4\pcom
rem cat p4\pcom.lst
rem
rem For neatness sake, copy out the intermediate to .p4 file
rem
cp p4\pcom.out p4\standardp.p4
rem
rem Compile pint
rem
echo Compiling pint to intermediate code
call compile p4\pint
if ERRORLEVEL 1 (

    echo *** Compile failed
    exit /b
    
)
rem
rem Add the final target program to the end of pint.
rem This means that the version of pint will read and interpret
rem this.
rem
rem For those of you having fun reading this, yes, the next statement accurately
rem describes what is going on: we are concatenating and running two different
rem intermediate codes together in completely different formats!
rem
cat p4\pint.p5 p4\standardp.p4 > tmp.p5
del p4\pint.p5
cp tmp.p5 p4\pint.p5
rem
rem Create null input file
rem
echo.>p4\pint.inp
rem
rem Now run pint(p4) on pint(p5), which runs the test program.
rem
echo Running pint(p4) on pint(p5) to execute test program
call run p4\pint
rem
rem Copy the result listing back to standardp.lst, again for neatness
rem
cp p4\pint.lst p4\standardp.lst
rem
rem Now compare with reference
rem
echo Comparing result to reference
call diffnole p4\standardp.lst p4\standardp.cmp > p4\standardp.dif
rem
rem Show the file, so if the length is zero, it compared ok.
rem
call :passfail p4\standardp.dif
del p4\pcom.inp
del p4\pint.inp
goto :exit

:passfail
if %~z1 == 0 (

    echo PASSED
    
) else (

    echo *** FAILED ***
    
)
goto :eof

:exit