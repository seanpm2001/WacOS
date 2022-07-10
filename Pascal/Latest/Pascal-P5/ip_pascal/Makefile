#
# Makefile for Pascal-p5 using IP Pascal
#
# Makes the main compiler interpreter settings.
#
all: pcom pint

pcom: source\pcom.pas
	pc source\pcom.pas/nrf
	mv source\pcom.exe bin
	
pint: source\pint.pas
	pc source\pint/nrf
	mv source\pint.exe bin
	
clean:
	rm -f bin\pcom.exe bin\pint.exe 
	find . -name "*.p5" -type f -delete
	find . -name "*.out" -type f -delete
	find . -name "*.lst" -type f -delete
	find . -name "*.obj" -type f -delete
	find . -name "*.sym" -type f -delete
	find . -name "*.int" -type f -delete
	find . -name "*.dif" -type f -delete
	find . -name "*.err" -type f -delete
	find . -name "*.tmp" -type f -delete
	find . -name "prd" -type f -delete
	find . -name "prr" -type f -delete
	find . -name "temp" -type f -delete
	find . -name "tmp" -type f -delete
	find . -name "*~" -type f -delete
