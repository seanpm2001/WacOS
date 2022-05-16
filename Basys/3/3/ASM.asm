; Start of script
; Assembly runtime script for BaSYStem 3
; Note: I do not know the Assembly language very well. For now, this is just pseudocode and is NOT functional

; Memory tests
memTotal = 1048576 ; Total storage on the main volume (1 MiB, or 1.048 MB)
memExter = 8388608 ; Total storage on the external volume (8 MiB, or 8.338 MB)
memEXFL1 = 360000 ; Total storage on the virtual floppy volume A (360K)
memEXFL2 = 720000 ; Total storage on the virtual floppy volume B (720K)
memEXFL3 = 1048576 ; Total storage on the virtual floppy volume C (1 MiB, or 1.048 MB)
memEXFL4 = 1440000 ; Total storage on the virtual floppy volume D (1.37 MiB, or 1.44 MB)
check 00001111 ; Checks the system identifier (old)
check 00000001 ; Checks the system identifier (new)
; System identifier guide
; System 1 (old/still in use) 000011111
; System 1 (new/cur) 000000001
; System 2 (cur) 00000011
; System 3 (cur) 00000111
; System 4 (cur) 00001111
; System 5 (cur) 00011111
; System 6 (cur) 00111111
; System 7 (cur) 01111111
; Cur = Current
; Random syntax test
mov AL, 8h ; Load AL with ? decimal (8 hex)
mov HEX, 16h ; Load AL with ? decimal (16 hex)
asm;asm

; File info
; File type: Assembly source file (*.asm)
; File version: 1 (2021, Wednesday, December 22nd at 6:22 pm)
; Line count (including blank lines and compiler line): 34
; End of script
