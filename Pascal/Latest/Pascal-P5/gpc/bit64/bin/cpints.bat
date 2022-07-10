@echo off
rem
rem Script to run a pint self compile
rem
rem Run macro preprocessor to configure source for self compile.
rem
call pascpp source\pint -DWRDSIZ64 -DSELF_COMPILE
rem
rem Compile the final target, the PAT
rem
echo Compiling the ISO 7185 PAT
call compile standard_tests\iso7185pat
if ERRORLEVEL 1 (

    echo *** Compile fails
    cat standard_tests\iso7185pat.err
    goto exit

)
rem
rem Compile pint itself
rem
echo Compiling pint to intermediate code
call compile source\pint.mpp
if ERRORLEVEL 1 (

    echo *** Compile fails
    cat source\pint.mpp.err
    goto exit

)
rem
rem Add the final target program (the pat) to the end of pint.
rem This means that the version of pint will read and interpret
rem this.
rem
cat source\pint.mpp.p5 standard_tests\iso7185pat.p5 > source\tmp.p5
del source\pint.mpp.p5
mv source\tmp.p5 source\pint.mpp.p5
rem
rem Create input file
rem
cp standard_tests\iso7185pat.inp source\pint.mpp.inp
rem
rem Now run pint on pint, which runs the PAT.
rem
echo Running pint on itself, to run the ISO 7185 PAT
call run source\pint.mpp
copy source\pint.mpp.lst standard_tests\iso7185pats.lst > temp
del temp
echo Comparing PAT result to reference
call diffnole standard_tests\iso7185pats.lst standard_tests\iso7185pats.cmp > standard_tests\iso7185pats.dif
call :passfail standard_tests\iso7185pats.dif
goto :exit

:passfail
if %~z1 == 0 (

    echo PASSED

) else (

    echo *** FAILED

)
goto :eof

:exit
