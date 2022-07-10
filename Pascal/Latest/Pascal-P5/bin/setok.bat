@echo off
rem
rem Set prt test as passed. Copies the compiler and run outputs to
rem the compare files.
rem
cp standard_tests/iso7185prt%1.err standard_tests/iso7185prt%1.ecp
if exist "standard_tests/iso7185prt%1.lst" (

    cp standard_tests/iso7185prt%1.lst standard_tests/iso7185prt%1.cmp

)
