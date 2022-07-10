@echo off
rem
rem Difference without line endings
rem
rem Same as diff, but ignores the DOS/Unix line ending differences.
rem

if "%1" == "" (

    echo *** Error: Missing parameter 1
    echo "*** s/b \"diffnole \<file1> \<file2>\""
    exit /b 1

)

if not exist "%1" (

    echo *** Error: Missing %1 file
    exit /b 1

)

if "%2"=="" (

    echo *** Error: Missing parameter 2
    echo "*** s/b \"diffnole \<file1> \<file2>\""
    exit /b 1

)

if not exist "%2" (

    echo *** Error: Missing %2 file
    exit /b 1

)

cp %1 %1.tmp
cp %2 %2.tmp
flip -d -b %1.tmp
flip -d -b %2.tmp
rem ignore lines with compiler/interpreter vs number
diff -I 'P5 Pascal interpreter vs. .*' -I 'P5 Pascal compiler vs. .*' %1.tmp %2.tmp
rm -f %1.tmp
rm -f %2.tmp
