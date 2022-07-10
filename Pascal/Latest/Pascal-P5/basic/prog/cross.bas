100 REM ************************************************
110 REM **** CROSS.BAS          28-FEB-78/11-APR-78 ****
120 REM **** N A BOURGEOIS      SANDIA LABORATORIES ****
130 REM ************************************************
140 REM **** CROSS REFERENCE PROGRAM FOR BASIC TEXT ****
150 REM **** CLINT PURDUE   KEN NOWOTNY   20-JAN-75 ****
160 REM ************************************************
170 REM **** INITIALIZATION ****
180 REM ************************
190 DIM S1%(857),S2%(2200,1),S4%(500,1)
200 S3%=1
210 S5%=0
220 DIM D1$(1)
230 D1$(0)="'"
240 D1$(1)=""""
250 DIM D$(98)
260 I8%=98
270 FOR I%=0 TO I8%
280 READ D$(I%)
290 NEXT I%
300 P$=CHR$(12)
310 T$="   CROSS REFERENCE   "&DAT$&"   "&CLK$
320 REM **************************
330 REM **** KEYBOARD ENTRIES ****
340 REM **************************
350 PRINT "ENTER THE INPUT FILE NAME",
360 LINPUT A$
370 IF POS(A$,':',1)=0 THEN A$='SY:'&A$
380 IF POS(A$,'.',1)=0 THEN A$=A$&'.BAS'
390 PRINT 'NAME THE OUTPUT DEVICE',
400 LINPUT F$
410 IF F$='' THEN F$='LP:'
420 IF POS(F$,':',1)=0 THEN 390
430 I%=POS(A$,':',1)
440 J%=POS(A$,".",1)
450 Z$=SEG$(A$,I%+1,J%-1)
460 F$=F$&Z$&'.CRF'
470 I%=POS(A$,':',1)
480 Z$=SEG$(A$,I%+1,255)
490 T$=Z$&T$
500 REM ************************
510 REM **** OPEN THE FILES ****
520 REM ************************
530 OPEN A$ FOR INPUT AS FILE #1
540 OPEN F$ FOR OUTPUT AS FILE #2
550 IF SEG$(F$,1,3)='TT:' THEN PRINT #2
560 PRINT #2,T$
570 PRINT #2
580 REM ********************************
590 REM **** START OF MAIN SEQUENCE ****
600 REM ********************************
610 IF END #1 THEN 1990 \ REM **** PRINT THE VARIABLES USAGE TABLE
620 LINPUT #1,B$
630 PRINT #2,B$
640 I1%=1
650 GOSUB 2580 \ REM **** OBTAIN THE LINE NUMBER
660 I9%=I2%
670 REM *********************************
680 REM **** IGNORE 'REM' STATEMENTS ****
690 REM *********************************
700 IF SEG$(B$,I1%,I1%+2)='REM' THEN 610 \ REM **** GET ANOTHER LINE
710 REM ****************************************
720 REM **** FIND AND REMOVE QUOTED STRINGS ****
730 REM ****************************************
740 FOR I%=0 TO 1
750 I1%=POS(B$,D1$(I%),1)
760 IF I1%=0 THEN 880
770 I4%=POS(B$,D1$(I%),I1%+1)
780 IF I4%=0 THEN 880
790 I3%=POS(B$,D1$(1-I%),1)
800 IF I3%>I1% THEN 850
810 I5%=POS(B$,D1$(1-I%),I3%+1)
820 IF I5%=0 THEN 850
830 I1%=I3%
840 I4%=I5%
850 I4%=I4%+1
860 GOSUB 2900 \ REM **** REMOVE QUOTED STRING
870 GO TO 750
880 NEXT I%
890 REM ****************************************
900 REM **** TRUNCATE '    \ REM' STATEMENTS ****
910 REM ****************************************
920 I1%=POS(B$,'\ REM',1)
930 IF I1%=0 THEN 980
940 B$=SEG$(B$,1,I1%-1)
950 REM ***********************************
960 REM **** FIND AND REMOVE KEY WORDS ****
970 REM ***********************************
980 FOR I%=0 TO I8%
990 I1%=POS(B$,D$(I%),1)
1000 IF I1%=0 THEN 1220
1010 I4%=I1%+LEN(D$(I%))
1020 GOSUB 2900 \ REM **** REMOVE KEY WORD
1030 IF I%>2 THEN 990
1040 GOSUB 2580 \ REM **** OBTAIN THE LINE NUMBER
1050 IF I%=2 THEN I9%=-I9% \ REM **** KEY WORD 'GOSUB'
1060 FOR J%=0 TO S5%
1070 IF S4%(J%,0)<>I2% THEN 1140
1080 K%=S4%(J%,1)
1090 IF S2%(K%,1)=0 THEN 1120
1100 K%=S2%(K%,1)
1110 GO TO 1090
1120 S2%(K%,1)=S3%
1130 GO TO 1180
1140 NEXT J%
1150 S4%(S5%,0)=I2%
1160 S4%(S5%,1)=S3%
1170 S5%=S5%+1
1180 S2%(S3%,0)=I9%
1190 S3%=S3%+1
1200 I9%=ABS(I9%)
1210 GO TO 990
1220 NEXT I%
1230 REM *******************************
1240 REM **** REPLACE 'AS' WITH '?' ****
1250 REM *******************************
1260 I1%=POS(B$,'AS',1)
1270 IF I1%=0 THEN 1330
1280 B$=SEG$(B$,1,I1%-1)&'?'&SEG$(B$,I1%+2,255)
1290 GO TO 1260
1300 REM *******************************
1310 REM **** REPLACE 'IF' WITH '@' ****
1320 REM *******************************
1330 I1%=POS(B$,'IF',1)
1340 IF I1%=0 THEN 1400
1350 B$=SEG$(B$,1,I1%-1)&'@'&SEG$(B$,I1%+2,255)
1360 GO TO 1330
1370 REM **************************************
1380 REM **** FIND AND SAVE VARIABLE NAMES ****
1390 REM **************************************
1400 I3%=0
1410 I2%=LEN(B$)
1420 IF I2%=0 THEN 610 \ REM **** GET ANOTHER LINE
1430 I1%=1
1440 J5%=I3%
1450 J1%=0
1460 J2%=0
1470 J3%=0
1480 I3%=ASC(SEG$(B$,1,1))
1490 GOSUB 2850 \ REM **** REMOVE SINGLE CHARACTER
1500 IF I3%<65 THEN 1410 \ REM **** 'A'
1510 IF I3%>90 THEN 1410 \ REM **** 'Z'
1520 J1%=I3%
1530 IF I2%=1 THEN 1700
1540 I4%=ASC(SEG$(B$,1,1))
1550 IF I4%=36 THEN 1660 \ REM **** '$'
1560 IF I4%=37 THEN 1680 \ REM **** '%'
1570 IF I4%>57 THEN 1700 \ REM **** '9'
1580 IF I4%<48 THEN 1700 \ REM **** '0'
1590 J2%=I4%
1600 GOSUB 2850 \ REM **** REMOVE SINGLE CHARACTER
1610 IF I2%=2 THEN 1700
1620 I4%=ASC(SEG$(B$,1,1))
1630 IF I4%<36 THEN 1700 \ REM **** '$'
1640 IF I4%>37 THEN 1700 \ REM **** '%'
1650 IF I4%=37 THEN 1680
1660 J3%=1
1670 GO TO 1690
1680 J3%=2
1690 GOSUB 2850 \ REM **** REMOVE SINGLE CHARACTER
1700 IF LEN(B$)<1 THEN 1790
1710 IF J5%=64 THEN 1790 \ REM **** '@' (IF )
1720 Z$=SEG$(B$,1,1)
1730 IF Z$='=' THEN 1780
1740 IF Z$<>'(' THEN 1790
1750 I4%=POS(B$,')',1)
1760 IF I4%=0 THEN 1790
1770 IF SEG$(B$,I4%+1,I4%+1)<>'=' THEN 1790
1780 I9%=-I9% \ REM **** VARIABLE PRECEEDED BY '='
1790 IF J2%=0 THEN J2%=47
1800 J9%=J3%*286+(J1%-65)*11+J2%-47
1810 IF S1%(J9%)<>0 THEN 1870
1820 S1%(J9%)=S3%
1830 S2%(S3%,0)=I9%
1840 I9%=ABS(I9%)
1850 S3%=S3%+1
1860 GO TO 1410 \ REM **** LOOK FOR ANOTHER VARIABLE
1870 I1%=S1%(J9%)
1880 IF S2%(I1%,1)<>0 THEN 1910
1890 S2%(I1%,1)=S3%
1900 GO TO 1830
1910 I1%=S2%(I1%,1)
1920 GO TO 1880
1930 REM ******************************
1940 REM **** END OF MAIN SEQUENCE ****
1950 REM ******************************
1960 REM *****************************************
1970 REM **** PRINT THE VARIABLES USAGE TABLE ****
1980 REM *****************************************
1990 CLOSE #1
2000 PRINT #2,P$
2010 IF SEG$(F$,1,3)='TT:' THEN PRINT #2
2020 PRINT #2,T$
2030 PRINT #2
2040 PRINT #2,'VARIABLE   LINE(S) WHERE USED:';
2050 PRINT #2,' IF PRECEEDED BY -, VAR FOLLOWED BY ='
2060 PRINT #2
2070 FOR K%=0 TO 285
2080 FOR K1%=0 TO 572 STEP 286
2090 I%=K%+K1%
2100 IF S1%(I%)=0 THEN 2290
2110 V$=''
2120 J3%=I%
2130 J4%=INT(I%/286)
2140 IF J4%=0 THEN 2210
2150 IF J4%=1 THEN 2190
2160 V$='%'
2170 J3%=I%-572
2180 GO TO 2210
2190 V$='$'
2200 J3%=I%-286
2210 I3%=INT(J3%/11)
2220 I2%=J3%-I3%*11
2230 IF I2%=0 THEN 2250
2240 V$=CHR$(I2%+47)&V$
2250 V$=CHR$(I3%+65)&V$
2260 I1%=S1%(I%)
2270 PRINT #2,V$;
2280 GOSUB 2710 \ REM **** PRINT THE LINE NUMBERS USED
2290 NEXT K1%
2300 NEXT K%
2310 REM *****************************************
2320 REM **** PRINT THE CONTOL TRANSFER TABLE ****
2330 REM *****************************************
2340 K1%=1
2350 S6%=S5%-1
2360 PRINT #2,P$
2370 IF SEG$(F$,1,3)='TT:' THEN PRINT #2
2380 PRINT #2,T$
2390 PRINT #2
2400 PRINT #2,'CONTROL TRANSFER SECTION (GOSUB, GO TO & THEN):';
2410 PRINT #2,' GOSUB PRECEEDED BY -'
2420 PRINT #2
2430 K2%=32767
2440 FOR K%=0 TO S6%
2450 IF S4%(K%,0)<=K1% THEN 2490
2460 IF S4%(K%,0)>K2% THEN 2490
2470 K2%=S4%(K%,0)
2480 I1%=S4%(K%,1)
2490 NEXT K%
2500 IF K2%=32767 THEN 3280 \ REM **** CLOSEOUT ROUTINE
2510 K1%=K2%
2520 PRINT #2,K1%;
2530 GOSUB 2710 \ REM **** PRINT THE LINE NUMBERS USED
2540 GO TO 2430
2550 REM *********************
2560 REM **** SUBROUTINES ****
2570 REM ************************************
2580 REM **** S/R OBTAIN THE LINE NUMBER ****
2590 REM ************************************
2600 I2%=0
2610 IF I1%>LEN(B$) THEN 2690
2620 I3%=ASC(SEG$(B$,I1%,I1%))
2630 IF I3%=32 THEN 2670 \ REM **** 'SPACE'
2640 IF I3%<48 THEN 2690 \ REM **** '0'
2650 IF I3%>57 THEN 2690 \ REM **** '9'
2660 I2%=I2%*10+I3%-48
2670 GOSUB 2850 \ REM **** REMOVE SINGLE CHARACTER
2680 GO TO 2610
2690 RETURN
2700 REM *****************************************
2710 REM **** S/R PRINT THE LINE NUMBERS USED ****
2720 REM *****************************************
2730 I2%=5
2740 I2%=2+I2%+LEN(STR$(S2%(I1%,0)))
2750 IF I2%<60 THEN 2780
2760 PRINT #2
2770 GO TO 2730
2780 PRINT #2,TAB(10);S2%(I1%,0);
2790 I1%=S2%(I1%,1)
2800 IF I1%<>0 THEN 2740
2810 PRINT #2
2820 PRINT #2
2830 RETURN
2840 REM *************************************
2850 REM **** S/R REMOVE SINGLE CHARACTER ****
2860 REM *************************************
2870 B$=SEG$(B$,1,I1%-1)&SEG$(B$,I1%+1,255)
2880 RETURN
2890 REM **********************************************
2900 REM **** S/R REMOVE KEY WORD OR QUOTED STRING ****
2910 REM **********************************************
2920 B$=SEG$(B$,1,I1%-1)&SEG$(B$,I4%,255)
2930 RETURN
2940 REM *******************
2950 REM **** KEY WORDS ****
2960 REM ********************
2970 REM **** STATEMENTS ****
2980 REM ********************
2990 DATA 'GO TO','THEN','GOSUB'
3000 DATA 'AS FILE','CALL','CHAIN','CLOSE #','CLOSE','COMMON'
3010 DATA 'DATA','DEF','DIM #','DIM','DOUBLE BUF','FILESIZE'
3020 DATA 'FOR INPUT','FOR OUTPUT','FOR','IF END #','END',LINPUT #'
3030 DATA 'LINPUT','INPUT #','INPUT','KILL','LET','LINE'
3040 DATA 'MODE','NAME','NEXT','ON','OPEN','OVERLAY'
3050 DATA 'PRINT #','PRINT','RANDOMIZE','READ','RECORDSIZE','REM'
3060 DATA 'RESET #','RESET','RESTORE #','RESTORE','RETURN','STEP'
3070 DATA 'STOP','TO','USING'
3080 REM ******************************
3090 REM **** ARITHMETIC FUNCTIONS ****
3100 REM ******************************
3110 DATA 'ABS','ATN','COS','EXP','INT','LOG10'
3120 DATA 'LOG','PI','RND','SGN','SIN','SQR'
3130 DATA 'TAB'
3140 REM **************************
3150 REM **** STRING FUNCTIONS ****
3160 REM **************************
3170 DATA 'ASC','BIN','CHR$','CLK$','DAT$','LEN'
3180 DATA 'OCT','POS','SEG$','STR$','TRM$','VAL'
3190 REM ********************************
3200 REM **** USER DEFINED FUNCTIONS ****
3210 REM ********************************
3220 DATA 'FNA','FNB','FNC','FND','FNE','FNF'
3230 DATA 'FNG','FNH','FNI','FNJ','FNK','FNL'
3240 DATA 'FNM','FNN','FNO','FNP','FNQ','FNR'
3250 DATA 'FNS','FNT','FNU','FNV','FNW','FNX'
3260 DATA 'FNY','FNZ'
3270 REM **************************
3280 REM **** CLOSEOUT ROUTINE ****
3290 REM **************************
3300 PRINT #2,P$
3310 CLOSE #2
3320 END
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          