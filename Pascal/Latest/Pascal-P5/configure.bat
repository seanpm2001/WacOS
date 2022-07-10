@echo off
rem ################################################################################
rem #
rem # Configure scipt for Pascal-P5
rem #
rem # Sets up the complete Pascal-P5 project.
rem #
rem ################################################################################

rem
rem Set default variables
rem
set compiler=gpc
set bits=32
set host=windows

rem
rem Determine if needed programs exist. The only fatal one is grep, because we
rem need that to run this script. The rest will impact the running of various
rem test and build scripts.
rem

where /q grep
if %errorlevel% neq 0 (

    echo *** No grep was found
    echo Terminating
    exit /b 1

)

where /q diff || echo *** No diff was found
where /q sed || echo *** No sed was found
where /q rm || echo *** No rm was found
where /q cp || echo *** No cp was found
where /q mv || echo *** No mv was found
where /q flip || echo *** No flip was found
where /q ls || echo *** No ls was found
where /q gzip || echo *** No zip was found
where /q tar || echo *** No tar was found
where /q cpp || echo *** No cpp was found

rem
rem Check flip (Unix/dos end of line changer) exists, and make it if not.
rem
where /q flip
if %errorlevel% neq 0 (

    echo Making flip.exe
    call bin\make_flip
    dir bin\flip.exe > temp
    grep "flip.exe" temp > temp2
    grep "flip.exe" temp > temp2 || echo *** Unable to make flip

)

rem
rem Detect and prioritize IP Pascal. The reason for this is, if IP Pascal
rem exists on the system, that is the preferable compiler.
rem pc.exe is the IP Pascal command shell. You might have to fix this up
rem if there is another pc.exe in the system.
rem
where /q pc
if %errorlevel% equ 0 (

    set compiler=ip_pascal
    goto :compiler_check_done

)

rem
rem Now check for GPC. Output scary message for no compiler found, but
rem otherwise do nothing. rem Its up to the user to find a compiler.
rem
where /q gpc
if %errorlevel% neq 0 (

    goto :compiler_check_done

)

set compiler=gpc

rem
rem Evaluate GPC compiler version and word size. This is required with GPC
rem because not all versions work as ISO 7185 Compilers. Also, we do the
rem 32 or 64 bit detection here.
rem
gpc -v 2> temp
grep "gpc version 20070904" temp > temp2
if %errorlevel% neq 0 (

    echo *** Warning, Pascal-P5 is only validated to work with gpc version 20070904

)
grep "build=x86_64" temp > temp2
if %errorlevel% neq 0 (

    set bits=32

) else (

    set bits=64

)
rm temp temp2

:compiler_check_done

rem
rem Check all arguments. Note that we don't attempt to check or fix bad choices
rem on the users part. We figure they know what they are doing.
rem

for %%x in (%*) do (

    if "%%x" == "--help" (

        echo "Configure program for Pascal-P5"
        echo
        echo "--gpc:       Select GPC as target compiler"
        echo "--ip_pascal: Select IP Pascal as target compiler"
        echo "--32:        Select 32 bit target"
        echo "--64:        Select 64 bit target"
        echo
        exit /b 1

    ) else if "%%x" == "--gpc" (

        set compiler=gpc

    ) else if "%%x" == "--ip_pascal" (

        set compiler=ip_pascal

    ) else if "%%x" == "--32" (

        set bits=32

    ) else if "%%x" == "--64" (

        set bits=64

    ) else (

        echo *** Option not recognized
        echo Terminating
        exit /b 1

    )

)

rem
rem Set up compiler to use.
rem
rem Presently implements:
rem
rem IP Pascal, named "ip_pascal"
rem
rem GPC Pascal, named "gpc"
rem

if "%compiler%" == "ip_pascal" (

    rem
    rem Set up for IP Pascal
    rem
    cp ip_pascal\p5.bat      bin\p5.bat
    cp ip_pascal\compile.bat bin\compile.bat
    cp ip_pascal\run.bat     bin\run.bat

    cp ip_pascal\p5          bin\p5
    cp ip_pascal\compile     bin\compile
    cp ip_pascal\run         bin\run

    cp ip_pascal\Makefile    .

    cp ip_pascal\standard_tests/iso7185pat.cmp standard_tests
    cp ip_pascal\standard_tests/iso7185pats.cmp standard_tests

    call doseol

    echo Compiler set to IP Pascal

)

if "%compiler%" == "gpc" (

    rem
    rem Set up for GPC Pascal
    rem
    cp %compiler%\p5.bat      bin\p5.bat
    cp %compiler%\compile.bat bin\compile.bat
    cp %compiler%\run.bat     bin\run.bat

    cp %compiler%\p5          bin\p5
    cp %compiler%\compile     bin\compile
    cp %compiler%\run         bin\run

    cp %compiler%/bit%bits%/bin/cpcoms     bin
    cp %compiler%/bit%bits%/bin/cpcoms.bat bin
    cp %compiler%/bit%bits%/bin/cpints     bin
    cp %compiler%/bit%bits%/bin/cpints.bat bin

    cp %compiler%/bit%bits%/Makefile    .

    cp %compiler%/bit%bits%/standard_tests/iso7185pat.cmp standard_tests
    cp %compiler%/bit%bits%/standard_tests/iso7185pats.cmp standard_tests
    cp %compiler%/bit%bits%/standard_tests/iso7185prt1847.ecp standard_tests
    cp %compiler%/bit%bits%/standard_tests/iso7185prt.cmp standard_tests

    cp %compiler%/bit%bits%/p4/standardp.cmp p4

    call doseol

    echo Compiler set to GPC Pascal

)

echo Configure completed!
