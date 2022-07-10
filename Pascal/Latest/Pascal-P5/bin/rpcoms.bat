@echo off
rem
rem Script to run pcom on interpreter with target file.
rem
rem Compiles pcom, then interprets that to run a target file. Used to find 
rem errors in pcom due to compiling a test source.
rem
rem Run macro preprocessor to configure source for self compile.
rem
call pascpp source\pcom -DWRDSIZ32 -DSELF_COMPILE
rem
rem Compile pcom to intermediate code using its binary version.
rem
echo Compiling pcom to intermediate code
call compile source\pcom.mpp
type source\pcom.mpp.err
rem
rem Now run that code on the interpreter and have it compile the target source.
rem
echo Running pcom to compile %1
cat source\pcom.mpp.p5 %1.pas > tmp.p5
mv source\pcom.mpp.p5 source\pcom.mpp.p5.org
cp tmp.p5 source\pcom.mpp.p5
echo.> source\pcom.mpp.inp
call run source\pcom.mpp
type source\pcom.mpp.lst
