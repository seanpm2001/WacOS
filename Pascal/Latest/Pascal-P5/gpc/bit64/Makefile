#
# Makefile for Pascal-P5
#
# Makes the main compiler interpreter set.
#
# The generated executables are named according to:
#
# bin16
#
# Where 16 is the bit size of the target. The current bit lengths are:
# 
# 16
# 32
# 32
#
# Note that 16 bits actually covers both 8 bit and 16 bit processors, since 
# 8 bit processors usually have 16 bit addressing, regardless of their basic
# word size.
#
# The make process will create all of the combinations that are possible given
# the current host processor. Not all combinations make sense. For example,
# A 32 bit processor can host both 16 bit and 32 bit back ends, whereas a 64 bit
# processor can host 16, 32, and 64 bit options. 
#
# After all possible bit modes are generated, the versions  of the executable 
# that match the current host are copied to their executable names, but without
# the bit or endian endings. Thus it is not necessary to specifically state the
# characteristics of the host.
#
PC=gpc
PFLAGS=--classic-pascal-level-0 --no-warnings --transparent-file-names
CFLAGS=
CPPFLAGS64=-DWRDSIZ64

all: pcom pint spew

pcom: source/pcom.pas
	pascpp --linemacro source/pcom $(CPPFLAGS64)
	$(PC) $(PFLAGS) -o bin/pcom64 source/pcom.mpp.pas
	cp bin/pcom64 bin/pcom
	
pcom_immerr: source/pcom.pas
	pascpp --linemacro source/pcom $(CPPFLAGS64) -DIMM_ERR
	$(PC) $(PFLAGS) -o bin/pcom64 source/pcom.mpp.pas
	cp bin/pcom64 bin/pcom
	
pcom_no_preamble: source/pcom.pas
	pascpp --linemacro source/pcom $(CPPFLAGS64) -DNO_PREAMBLE
	$(PC) $(PFLAGS) -o bin/pcom64 source/pcom.mpp.pas
	cp bin/pcom64 bin/pcom
	
pint: source/pint.pas 
	pascpp --linemacro source/pint $(CPPFLAGS64)
	$(PC) $(PFLAGS) -o bin/pint64 source/pint.mpp.pas
	cp bin/pint64 bin/pint

spew: source/spew.c
	$(CC) $(CFLAGS) -o bin/spew source/spew.c
	
clean:
	rm -f bin/pcom bin/pint 
	find . -name "*.pint" -type f -delete
	find . -name "*.out" -type f -delete
	find . -name "*.lst" -type f -delete
	find . -name "*.obj" -type f -delete
	find . -name "*.sym" -type f -delete
	find . -name "*.int" -type f -delete
	find . -name "*.dif" -type f -delete
	find . -name "*.err" -type f -delete
	find . -name "*.ecd" -type f -delete
	find . -name "*.tmp" -type f -delete
	find . -name "prd" -type f -delete
	find . -name "prr" -type f -delete
	find . -name "temp" -type f -delete
	find . -name "tmp" -type f -delete
	find . -name "*~" -type f -delete
	find . -name "*.diflst" -type f -delete
	find . -name "*.ecdlst" -type f -delete
	find . -name "*.nocerr" -type f -delete
	find . -name "*.noerr" -type f -delete
	find . -name "*.norerr" -type f -delete
	find . -name "*.p2" -type f -delete
	find . -name "*.p4" -type f -delete
	find . -name "*.p5" -type f -delete
	find . -name "*.mpp.*" -type f -delete
	
help:
	@echo
	@echo Make targets:
	@echo
	@echo All	Make all binaries
	@echo
	@echo pcom	Make pcom, the Pascal compiler.
	@echo
	@echo pcom_immerr	Make pcom with print error immediate option. This causes
	@echo               errors to be printed immediately instead of waiting to
	@echo               collect an entire line. This is for debugging.
	@echo
	@echo pint          Make pint, the interpreter/debugger.
	@echo
	@echo spew          Make spew, a fault generator test program.
	@echo
	@echo clean         Clean intermediate/temp files from tree.
	@echo
    