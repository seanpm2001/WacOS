@echo off
rem
rem Run cpp (C PreProcessor) on the input file .pas to
rem produce a .mpp (Macro post process) file. Accepts macro definitions and other
rem options after the filename. Execute as:
rem
rem pascpp <file> [<option>]...
rem
rem Example:
rem
rem pascpp hello -DGNU_PASCAL
rem
rem Preprocesses the file hello.pas to become hello.mpp, and defines the 
rem GNU_PASCAL macro.
rem
rem Supresses warnings, supresses 'rem' lines in the output, and supresses any 
rem attempt to automatically include system files, and preserves whitespace
rem from the original file.
rem
rem This is basically how you use cpp with a non-C language file. It *should* be
rem able to preprocess a file without "rem" line directives and give the same
rem file back without differences (check with diff). This is a good crosscheck
rem before you add macros to a file.
rem
if "%1"=="--linemacro" goto linemacro
cpp -P -nostdinc -traditional-cpp %1.pas %1.mpp.pas %2 %3 %4 %5 %6 %7 %8 %9
goto exit
:linemacro
cpp -nostdinc -traditional-cpp %2.pas %2.mpp.pas %3 %4 %5 %6 %7 %8 %9
:exit