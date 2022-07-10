@echo off
rem
rem Test a single program run
rem
rem Tests the compile and run of a single program.
rem
rem To do this, there must be the files:
rem
rem <file>.inp - Contains all input to the program
rem <file>.cmp - Used to compare the <file>.lst program to, should
rem              contain an older, fully checked version of <file>.lst.
rem
rem <file>.dif will contain the differences in output of the run.
rem

rem
rem Check there is a parameter
rem
if not "%1"=="" goto paramok
echo *** Error: Missing parameter
goto exit
:paramok

rem
rem Check the source file exists
rem
if exist %1.pas goto :sourcefileexist
echo *** Error: Source file %1.pas does not exist
goto exit
:sourcefileexist

rem
rem Check the input file exists
rem
if exist %1.inp goto :inputfileexist
echo *** Error: Input file %1.inp does not exist
goto exit
:inputfileexist

rem
rem Check the result compile file exists
rem
if exist %1.cmp goto :comparefileexist
echo *** Error: Compare file %1.cmp does not exist
goto exit
:comparefileexist

rem
rem Compile and run the program
rem
echo Compile and run %1
call compile %1
rem echo Error return after compile: %errorlevel%
rem
rem Proceed to run and compare only if compile suceeded
rem
if not errorlevel 1 (

    echo|set /p="running... "
    call run %1
    rem
    rem Check output matches the compare file
    rem
    echo|set /p="checking... "
    call diffnole %1.lst %1.cmp > %1.dif
    call :passfail %1.dif

)

rem
rem Terminate program
rem
goto :exit

:passfail
if %~z1 == 0 (

    echo PASSED

) else (

    echo *** FAILED ***

)
goto :eof

:exit

