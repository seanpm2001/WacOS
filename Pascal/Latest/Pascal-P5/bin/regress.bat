@echo off
rem
rem Regression test
rem
rem Execution:
rem
rem regress [--full|--short]...
rem
rem Run the compiler through a few typical programs
rem to a "gold" standard file. Each mode is cycled through in sequence.
rem
rem The flags are one of:
rem
rem --full  Run full test sequence.
rem --short Run short test sequence.
rem
set full=0
for %%x in (%*) do (

   	if "%%~x"=="--full" (

   		set full=1

   	) else if "%%~x"=="--short" (

   		set full=0

   	) else if "%%~x"=="--help" (

   		echo.
   		echo Regression test
		echo.
		echo Execution:
		echo.
		echo regress [--full^|--short^]...
		echo.
		echo Run the compiler through a few typical programs
		echo to a "gold" standard file. Each mode is cycled through in sequence.
		echo.
		echo The flags are one of:
		echo.
		echo --full  Run full test sequence.
		echo --short Run short test sequence.
		echo.
		goto stop

    ) else if not "%%~x"=="" (

    	echo.
    	echo *** Option not recognized
    	echo.
		echo Execution:
		echo.
		echo regress [--full|--short]...
		echo.
		goto stop

	)

)
echo Regression Summary > regress_report.txt
echo Line counts should be 0 for pass >> regress_report.txt
set option=
echo pint run >> regress_report.txt
call :do_regress

rem
rem Print collected status
rem
echo.
date /t >> regress_report.txt
time /t >> regress_report.txt
call chkfiles >> regress_report.txt
cat regress_report.txt
echo.

goto stop

:do_regress
call testprog %option% sample_programs\hello
wc -l sample_programs\hello.dif >> regress_report.txt
call testprog %option% sample_programs\roman
wc -l sample_programs\roman.dif >> regress_report.txt
call testprog %option% sample_programs\match
wc -l sample_programs\match.dif >> regress_report.txt
call testprog %option% sample_programs\prime
wc -l sample_programs\prime.dif >> regress_report.txt
call testprog %option% sample_programs\qsort
wc -l sample_programs\qsort.dif >> regress_report.txt
call testprog %option% sample_programs\fbench
wc -l sample_programs\fbench.dif >> regress_report.txt
call testprog %option% sample_programs\drystone
wc -l sample_programs\drystone.dif >> regress_report.txt
call testprog %option% sample_programs\startrek
wc -l sample_programs\startrek.dif >> regress_report.txt
call testprog %option% sample_programs\basics
wc -l sample_programs\basics.dif >> regress_report.txt
call testprog %option% basic\basic
wc -l basic\basic.dif >> regress_report.txt
rem
rem Now run the ISO7185pat compliance test
rem
call testprog %option% standard_tests\iso7185pat
wc -l standard_tests\iso7185pat.dif >> regress_report.txt
call testprog %option% standard_tests\iso7185pat0001
wc -l standard_tests\iso7185pat0001.dif >> regress_report.txt
rem
rem Run previous versions of the system and Pascal-S
rem
call testpascals %option%
wc -l sample_programs\pascals.dif >> regress_report.txt
call testp2 %option%
wc -l p2\roman.dif >> regress_report.txt
call testp4 %option%
wc -l p4\standardp.dif >> regress_report.txt
if "%full%"=="1" (

    echo Running PRT...
    echo PRT run >> regress_report.txt
    rem
    rem Run rejection test
    rem
    call runprt %option%
    call diffnole standard_tests/iso7185prt.rpt standard_tests/iso7185prt.cmp > standard_tests/iso7185prt.dif
    wc -l standard_tests/iso7185prt.dif >> regress_report.txt

    echo Running self compile...
    rem
    rem Run pcom self compile (note this runs on P5/P6 only)
    rem
    echo pcom self compile >> regress_report.txt
    call cpcoms %option%
    wc -l source/pcom.mpp.dif >> regress_report.txt

    rem
    rem Run pint self compile (note this runs on P5/P6 only)
    rem
    echo pint self compile >> regress_report.txt
    call cpints %option%
    wc -l standard_tests/iso7185pats.dif >> regress_report.txt

)
exit /b

rem
rem Terminate program
rem
:stop
