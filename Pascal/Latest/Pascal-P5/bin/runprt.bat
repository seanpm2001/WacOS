@echo off
rem
rem Run rejection tests
rem
rem The rejection tests use the same format as the acceptance tests, but there
rem is no positive go/no go indication for them. Each test should generate a
rem failure, and all you can say is that the test has failed if there were no
rem error(s).
rem
rem Configure to drop the preamble so that miscompares are lessened.
rem
make pcom_no_preamble

rem
rem Run the tests
rem
echo Running tests
set List=standard_tests\iso7185prt*.pas
for /f "delims=" %%a in ('dir /b "%List%"') do (

    call testprog standard_tests\%%~na

)

echo Creating combined listing
echo *******************************************************************************> standard_tests\iso7185prt.rpt
echo.>> standard_tests\iso7185prt.rpt
echo Pascal Rejection test run for iso7185prt >> standard_tests\iso7185prt.rpt
echo.>> standard_tests\iso7185prt.rpt
echo *******************************************************************************>> standard_tests\iso7185prt.rpt
rem
rem Make a list of files WITHOUT compile errors
rem
echo Creating error accounting listings
grep -l "Errors in program: 0" standard_tests/iso7185prt*.err > standard_tests/iso7185prt.nocerr
rem
rem Make a list of files WITHOUT runtime errors.
rem
egrep -L "^\*\*\*" standard_tests/iso7185prt*.lst > standard_tests/iso7185prt.norerr
rem
rem Find files with NO errors either at compile time or runtime. This is done
rem by concatenating the files, sorting and finding duplicate filenames. That
rem is, if the filename list in both the no compile error and no runtime error
rem lists, then no error at all occurred on the file and it needs to be looked
rem at.
rem
cat standard_tests/iso7185prt.nocerr standard_tests/iso7185prt.norerr > temp
sort temp | uniq -d -w 30 > standard_tests/iso7185prt.noerr
rem
rem Place in combined listing as report
rem
echo.>> standard_tests\iso7185prt.rpt
echo Tests for which no compile or runtime error was flagged: **********************>> standard_tests\iso7185prt.rpt
echo.>> standard_tests\iso7185prt.rpt
type standard_tests\iso7185prt.noerr >> standard_tests\iso7185prt.rpt

rem
rem Make a listing of compiler output difference files to look at. If you are
rem satisfied with each of the prt output runs, then you can copy the .err file
rem to the .ecp file and get a 0 dif length file. Then this file will show you
rem the files that don't converge. Note DIFFERENT DOES NOT MEAN *** WRONG ***.
rem It simply may mean the error handling has changed. The purpose of diffing
rem the output files is that it allows you to check that simple changes have
rem not broken anything.
rem
echo creating compile time difference list
set List=standard_tests\iso7185prt*.err
for /f "delims=" %%a in ('dir /b "%List%"') do (

    call diffnole standard_tests\%%~na.err standard_tests\%%~na.ecp > standard_tests\%%~na.ecd

)
wc -l standard_tests/iso7185prt*.ecd > standard_tests/iso7185prt.ecdlst
rem
rem Place in combined listing
rem
echo.>> standard_tests\iso7185prt.rpt
echo Compile output differences: **********************>> standard_tests\iso7185prt.rpt
echo.>> standard_tests\iso7185prt.rpt
type standard_tests\iso7185prt.ecdlst >> standard_tests\iso7185prt.rpt

rem
rem Make a listing of run output difference files to look at. If you are satisfied
rem with each of the prt output runs, then you can copy the .lst file to the .cmp
rem file and get a 0 dif length file. Then this file will show you the files that
rem don't converge. Note DIFFERENT DOES NOT MEAN *** WRONG ***. It simply may
rem mean the error handling has changed. The purpose of diffing the output files
rem is that it allows you to check that simple changes have not broken anything.
rem
echo creating runtime difference list
rm -f standard_tests/iso7185prt.dif
wc -l standard_tests/iso7185prt*.dif > standard_tests/iso7185prt.diflst
rem
rem Place in combined listing
rem
echo.>> standard_tests\iso7185prt.rpt
echo Run output differences: **********************>> standard_tests\iso7185prt.rpt
echo.>> standard_tests\iso7185prt.rpt
type standard_tests\iso7185prt.diflst >> standard_tests\iso7185prt.rpt

rem
rem Add individual program compiles and runs
rem
echo Adding program compile and runs
echo.>> standard_tests\iso7185prt.rpt
echo *******************************************************************************>> standard_tests\iso7185prt.rpt
echo.>> standard_tests\iso7185prt.rpt
echo Listings for compile and run of iso7185prt >> standard_tests\iso7185prt.rpt
echo.>> standard_tests\iso7185prt.rpt
echo *******************************************************************************>> standard_tests\iso7185prt.rpt
set List=standard_tests\iso7185prt*.pas
for /f "delims=" %%a in ('dir /b "%List%"') do (

    echo.>> standard_tests\iso7185prt.rpt
    echo Listing for standard_tests/%%~na.pas *************************************>> standard_tests\iso7185prt.rpt
    echo.>> standard_tests\iso7185prt.rpt
    echo Compile: >> standard_tests\iso7185prt.rpt
    echo.>> standard_tests\iso7185prt.rpt
    type standard_tests\%%~na.err >> standard_tests\iso7185prt.rpt
    echo.>> standard_tests\iso7185prt.rpt
    if exist "standard_tests\%%~na.lst" (

        echo Run: >> standard_tests\iso7185prt.rpt
        echo.>> standard_tests\iso7185prt.rpt
        type standard_tests\%%~na.lst >> standard_tests\iso7185prt.rpt

    )


)

rem
rem Restore binaries
rem
make
