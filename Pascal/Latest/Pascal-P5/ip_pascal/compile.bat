@echo off
rem
rem Compile file in batch mode using IP Pascal.
rem
rem Runs a compile with the input and output coming from/
rem going to files.
rem 
rem Execution:
rem 
rem Compile <file>
rem
rem <file> is the filename without extention.
rem
rem The files are:
rem
rem <file>.pas - The Pascal source file
rem <file>.p5  - The intermediate file produced
rem <file>.err - The errors output from the compiler
rem
rem Note that the l+ option must be specified to get a full
rem listing in the .err file (or just a lack of l-).
rem

if "%1"=="" (

    echo *** Error: Missing parameter
    exit /b 1

)

if not exist "%1.pas" (

    echo *** Error: Missing %1.pas file
    exit /b 1

)

rem
rem Run the compile
rem
pcom %1.pas %1.p5 > %1.err

rem
rem Set the error status of the compile
rem
rem This will be zero if the compile was sucessful
rem
grep "Errors in program: 0" %1.err > %1.tmp
rem echo Error return after compile: %errorlevel%
if errorlevel 1 (

    rem
    rem For failed compiles, remove the intermediate file
    rem so it can't be run.
    rem
    echo Compile fails, examine the %1.err file
    del %1.p5
    exit /b 1

)
rem del %1.tmp

rem
rem Terminate
rem
:stop
exit /b 0
