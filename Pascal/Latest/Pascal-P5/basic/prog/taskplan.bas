10 ' TASKPLAN     Version 1.0
30 ' (C) Copyright 1984  C. Lamar Williams
40 ' Williams Software & Services
50 ' 1114 Pusateri Way
60 ' San Jose, California 95121
70 '
80 '  TASKPLAN generates an integrated cost/task schedule.
90 '  Given the irregular time periods and costs of various overlapping
100 ' tasks, TASKPLAN solves for each period's incremental cost and the
110 ' cumulative cost.
120 '
130 CLEAR ,60864!,1000 : OPTION BASE 1 : SCREEN 0,0 : WIDTH 80 : CLOSE : KEY OFF : COLOR 0,6,6 : CLS
140 DEFINT A,I,J,N,P,R,S,X,F
150 DEFDBL C,T,Y
160 DEFSTR H,G
170 FLD = 0 :FL1 = 0 :FL2 = 0 :FL3 = 0 :FL4 = 0
180 BLK$ = "                    "
190 IT = 50 : GIT = "50"
200 JP = 60 : GJP = "60"
210 DIM BT(IT), ET(IT), CT(IT), CM1(IT), CM2(IT), TT$(IT), Y(IT,JP)
220 DIM X(JP), TK(JP), CI(JP), CC(JP), CTM(IT)
230 DIM H(79,13)
240 GPN = "" : RM$ = "" : CM$ = "" : CM11 = 1! : CM2 = 1! : CM1N$ = ""
250 FOR I = 1 TO IT
260 TT$(I) = "" :BT(I) = 0! :ET(I) = 1! :CT(I) = 0! :CM1(I) = 1!
270 NEXT I
280 LOCATE 2,1,0
290 PRINT SPC(20) "              TASKPLAN" : PRINT : PRINT
300 PRINT SPC(7) "            (C) Copyright 1984  C. Lamar Williams" : PRINT
310 PRINT SPC(19) "     Williams Software & Services"
320 PRINT SPC(7) "  1114 Pusateri Way    San Jose, CA  95121     (408)227-4238"
330 LOCATE 11,1,0
340 PRINT SPC(7) "   Given the irregular schedule and costs of various tasks,"
350 PRINT SPC(7) "   TASKPLAN solves for the cost of each time period."
360 PRINT
370 PRINT "      *****************************************************************"
380 PRINT "      *                  ";
390 COLOR 0,7
400 PRINT " -- SPECIAL CONDITIONS -- ";
410 COLOR 0,6,6
420 PRINT "                   *"
430 PRINT "      *                                                               *"
440 PRINT "      *   Users are encouraged to copy and distribute this program.   *"
450 PRINT "      *                                                               *"
460 PRINT "      *      If you use TASKPLAN and like it, please send $20 to      *"
470 PRINT "      *      Williams Software & Services.  -- THANKS --              *"
480 PRINT "      *****************************************************************"
490 GOSUB 4900
500 LOCATE 1,18,0
510 PRINT "Please read these instructions before starting" : PRINT
520 PRINT "1. Have on hand the general data for your project."
530 PRINT "     Ч project's name (up to 20 characters)"
540 PRINT "     Ч total number of tasks (2 to "; GIT; ")"
550 PRINT "     Ч total number of time periods (2 to "; GJP; ")"
560 PRINT "     Ч choice of rounding method"
570 PRINT "         Ч Normal,        +1 if fraction >= 0.5"
580 PRINT "         Ч Conservative,  +1 if fraction >  0.0"
590 PRINT "     Ч choice of Constant or Variable for cost multiplier M1"
600 PRINT "         Ч value of M1 if Constant is selected (xx.yyyzzz)"
610 PRINT "     Ч value of constant cost multiplier M2 (xx.yyyzzz)" : PRINT
620 PRINT "2. Have on hand the specific data for the individual tasks."
630 PRINT "     Ч task's name (up to 20 characters)"
640 PRINT "     Ч task's start time (xx.yy), range = 0.0 to <";GJP;".00"
650 PRINT "     Ч task's end time (xx.yy),   range = >start time to ";GJP;".00"
660 PRINT "     Ч task's cost (wxxxyyy.zz)"
670 PRINT "     Ч task's cost multiplier M1, if Variable option was selected (xx.yyyzzz)" : PRINT
680 PRINT "3. If using data from a previous TASKPLAN project, have on hand the ";
684 PRINT CHR$(34);"filename";CHR$(34); " under which the data were stored."
690 GOSUB 4900
700 LOCATE 1,28,0 : PRINT "Hints on using TASKPLAN" : PRINT
710 PRINT "1. Units of time are arbitrary (days, months, years)." : PRINT
720 PRINT "     Time ---------->  0   1   2   3 ....  input task's Start and End `Time'"
730 PRINT "                       Ё   Ё   Ё   Ё"
740 PRINT "     Time Period --->  цд1дед2дед3де ....  output costs per `Time Period'" : PRINT
750 PRINT "2. Cost multipliers M1 and M2 are useful."
760 PRINT "     Ч (task final cost) = (task input cost) x (M1) x (M2)"
770 PRINT "     Ч Use M1 = 1.0 (Constant) and M2 = 1.0 for simple cases."
780 PRINT "     Ч Use M1 = Variable for task-varying factors such as overhead."
790 PRINT "     Ч Use M2 = 0.001 to convert input costs in dollars to output costs"
800 PRINT "       in thousands-of-dollars." : PRINT
810 PRINT "3. Conservative rounding is useful in budgeting.  An input cost of $654,321"
820 PRINT "   converted (M2) to thousands-of-dollars would be 654 (normal rounding)"
830 PRINT "   or 655 (conservative rounding)." : PRINT
840 PRINT "4.  For introduction, use the provided data file SAMPLE.TPN"
850 PRINT "     Ч revise the `input' data as desired"
860 PRINT "     Ч try different output options"
870 GOSUB 4900
880 LOCATE 2,1
890 LINE INPUT;"Do you wish to use data from a previous project (Y/N)?  ";GRD
900 IF GRD >< "Y" AND GRD >< "y" GOTO 1070
910 FLD = 1
920 GOSUB 4970
930 PRINT : PRINT
940 LINE INPUT;"Enter the   d:filename.ext   for the data you wish to use. ";GNR
950 ON ERROR GOTO 1060
960 OPEN GNR FOR INPUT AS #1
970 INPUT #1, GPN, NT, NP, RM$, CM$, CM2
980 IF CM$ = "C" THEN INPUT #1, CM11
990 FOR I = 1 TO NT
1000 IF EOF(1) GOTO 1050
1010 INPUT #1, TT$(I), BT(I), ET(I), CT(I)
1020 IF CM$ = "V" THEN INPUT #1, CM1(I)
1030 NEXT I
1040 ON ERROR GOTO 0
1050 CLOSE #1
1060 IF ERR = 53 THEN BEEP :PRINT :PRINT "File not found" :PRINT :RESUME 920
1070 CLS
1080 FL1 = 0 : OPEN "SCRN:" FOR OUTPUT AS #1 : GOSUB 5060 : CLOSE #1
1090 IF FLD = 1 GOTO 1110
1100 GOSUB 5290 : GOSUB 5350 : GOSUB 5470 : GOSUB 5590 : GOSUB 5660 : GOSUB 5740 : GOSUB 5870
1110 REV$ = ""
1120 ITNO = 0
1130 LOCATE 24,1,0 : PRINT STRING$(79,32);
1140 LOCATE 24,19,1
1150 LINE INPUT; "Do you wish to change any of the above data (Y/N)? "; REV$
1160 IF REV$ = "" GOTO 1120
1170 IF REV$ = "y" OR REV$ = "Y" GOTO 1200
1180 IF REV$ = "n" OR REV$ = "N" GOTO 1310
1190 GOTO 1110
1200 FL2 = 1
1210 CMO$ = CM$
1220 LOCATE 24,1,1 : PRINT STRING$(79,32);
1230 GITNO = ""
1240 LOCATE 24,19,1
1250 LINE INPUT; "Type the ITEM Number (1-7), then press the Enter key. "; GITNO
1260 IF GITNO = "" GOTO 1240
1270 ITNO = VAL(GITNO)
1280 IF ITNO < 1 OR ITNO > 7 GOTO 1220
1290 ON ITNO GOSUB 5290,5350,5470,5590,5660,5740,5870
1300 GOTO 1110
1310 FL2 = 0
1320 CLS : LOCATE 2,9,0
1330 IF FLD = 0 THEN PRINT "Type the requested data, then press the Enter key."
1340 IF FLD = 1 THEN PRINT " "
1350 FL1 = 0
1360 OPEN "SCRN:" FOR OUTPUT AS #1
1370 GOSUB 5940
1380 CLOSE #1
1390 IA = CINT((NT/10) + .4)
1400 FOR ITT = 1 TO IA
1410 ITH = ITT*10
1420 ITL = ITH - 9
1430 IF NT < ITH THEN ITH = NT
1440 ITLN = 9
1450 FOR I = ITL TO ITH
1460 LOCATE ITLN,1,0 : PRINT I; STRING$(75,32);
1470 ITLN = ITLN + 2 - (I MOD 2)
1480 NEXT I
1490 FOR IB = ITLN TO 24
1500 LOCATE IB,1,0 : PRINT STRING$(79,32);
1510 NEXT IB
1520 ITLN = 9
1530 FOR I = ITL TO ITH
1540 IF FLD = 0 GOTO 1610
1550 LOCATE ITLN,8  : PRINT TT$(I);
1560 LOCATE ITLN,33 : PRINT USING "##.##"; BT(I);
1570 LOCATE ITLN,43 : PRINT USING "##.##"; ET(I);
1580 LOCATE ITLN,53 : PRINT USING "#######.##"; CT(I);
1590 IF CM$ = "V" THEN LOCATE ITLN,68 : PRINT USING "##.######"; CM1(I);
1600 GOTO 2040
1610 LOCATE ITLN,8,1 : LINE INPUT; TT$(I)
1620 IF TT$(I) = "" GOTO 1610
1630 TT$(I) = LEFT$(TT$(I),20)
1640 LOCATE ITLN,33 : LINE INPUT; BTT$
1650 LOCATE 24,1,0 : PRINT STRING$(79,32);
1660 IF BTT$ = "" GOTO 1640
1670 G = BTT$ : GOSUB 7030
1680 IF G = "0" THEN BT(I) = 0 : GOTO 1770
1690 BT(I) = VAL(BTT$)
1700 IF BT(I) > 0! AND BT(I) <= NP GOTO 1770
1710 IF BT(I)=0 GOTO 1760
1720 LOCATE 24,1,0
1730 IF BT(I) < 0! THEN PRINT "Pardon me, but Start Time must be > 0.0"; : GOTO 1750
1740 IF BT(I) > NP THEN PRINT "Pardon me, but Start Time must be <"; NP;
1750 FOR IP = 1 TO 3000 : NEXT IP
1760 BTT$ = "" : LOCATE ITLN,32 : PRINT STRING$(7,32); : GOTO 1640
1770 LOCATE ITLN,43 : LINE INPUT; ETT$
1780 LOCATE 24,1,0 : PRINT STRING$(79,32);
1790 IF ETT$ = "" GOTO 1770
1800 ET(I) = VAL(ETT$)
1810 IF ET(I) > BT(I) AND ET(I) <= NP GOTO 1880
1820 LOCATE 24,1,0 : BEEP
1830 IF ET(I) <= BT(I) THEN PRINT "Pardon me, but End Time must be >";
1830 IF ET(I) <= BT(I) THEN PRINT  BT(I); " for Task"; I; " ."; : GOTO 1850
1840 IF ET(I) > NP THEN PRINT "Pardon me, but End Time must be <="; NP; " .";
1850 FOR IP = 1 TO 3000 : NEXT IP
1860 ETT$ = "" : LOCATE ITLN,42 : PRINT STRING$(7,32);
1870 GOTO 1770
1880 LOCATE ITLN,53 : LINE INPUT; CTT$
1890 IF CTT$ = "" GOTO 1880
1900 G = CTT$ : GOSUB 7030
1910 IF G = "0" THEN CT(I) = 0 : GOTO 1940
1920 CT(I) = VAL(CTT$)
1930 IF CT(I)=0 THEN CTT$="":LOCATE ITLN,52:PRINT STRING$(12,32);:GOTO 1880
1940 IF CM$ = "C" GOTO 2040
1950 LOCATE ITLN,68 : LINE INPUT; CMU1$
1960 IF CMU1$ = "" GOTO 1950
1970 G = CMU1$ : GOSUB 7030
1980 IF G = "0" THEN CM1(I) = 0 : GOTO 2040
1990 CM1(I) = VAL(CMU1$)
2000 IF CM1(I) > 0 GOTO 2040
2010 CMU1$ = ""
2020 LOCATE ITLN,67 : PRINT STRING$(12,32);
2030 GOTO 1950
2040 ITLN = ITLN + 2 - (I MOD 2)
2050 NEXT I
2060 BEEP
2070 LOCATE 24,1,0 : PRINT STRING$(79,32);
2080 LOCATE 24,10,1
2090 REV$ = ""
2100 PRINT "Do you wish to change any of the above data (Y/N)?";
2110 LOCATE 24,61,1
2120 LINE INPUT; REV$
2130 IF REV$ = "Y" OR REV$ = "y" GOTO 2160
2140 IF REV$ = "N" OR REV$ = "n" GOTO 2500
2150 GOTO 2070
2160 REVT$ = ""
2170 LOCATE 24,1,0 : PRINT STRING$(79,32);
2180 LOCATE 24,10,1
2190 PRINT "Type the Task No. to be changed, then press the Enter key.";
2200 LOCATE 24,69,1
2210 LINE INPUT; REVT$
2220 ITR = VAL(REVT$)
2230 IF ITR >= ITL AND ITR <= ITH GOTO 2290
2240 LOCATE 24,1,0 : PRINT STRING$(79,32);
2250 LOCATE 24,10,0 : BEEP
2260 PRINT " Pardon me, but the Task No. must be from ";ITL; "to ";ITH;". ";
2270 FOR IL = 1 TO 2000 : NEXT IL
2280 GOTO 2160
2290 IF CM$ = "C" THEN GM1 = " or C" ELSE GM1 = "C, or M1"
2300 ILR = ITR - (ITT - 1)*10
2310 ILNR = 7 + ILR + FIX((ILR + 1)/2)
2320 COLOR 0,7
2330 GOSUB 6190
2340 LOCATE 24,1,1 : PRINT STRING$(79,32);
2350 LOCATE 24,1,1
2360 PRINT "Which item (D,S,E,";GM1;") do you wish to change for Task";ITR;"?";
2370 LOCATE 24,66,1
2380 LINE INPUT; REVIT$
2390 GTT="" : BTT$="" : ETT$="" : CTT$="" : CMU1$="" : FL2 = 0
2400 IF REVIT$ = "D" OR REVIT$ = "d" THEN FL2 = 1 : GOSUB 6270
2410 IF REVIT$ = "S" OR REVIT$ = "s" THEN FL2 = 1 : GOSUB 6320
2420 IF REVIT$ = "E" OR REVIT$ = "e" THEN FL2 = 1 : GOSUB 6460
2430 IF REVIT$ = "C" OR REVIT$ = "c" THEN FL2 = 1 : GOSUB 6570
2440 IF REVIT$ = "M1" OR REVIT$ = "m1" THEN GOSUB 6660
2450 IF FL2 = 0 GOTO 2340
2460 LOCATE 24,1,0 : PRINT STRING$(79,32);
2470 COLOR 0,6
2480 GOSUB 6190
2490 GOTO 2070
2500 NEXT ITT
2510 CLS : LOCATE 4,1,1
2520 LINE INPUT; "Do you wish to store the just-entered data (Y/N)? "; GSD
2530 IF GSD >< "Y" AND GSD >< "y" GOTO 2650
2540 GOSUB 4970
2550 PRINT : PRINT
2560 LINE INPUT; "Enter the  d:filename.ext  for storing the data. "; GNS
2570 OPEN GNS FOR OUTPUT AS #1
2580 WRITE #1, GPN, NT, NP, RM$, CM$, CM2
2590 IF CM$ = "C" THEN WRITE #1, CM11
2600 FOR I = 1 TO NT
2610 WRITE #1, TT$(I), BT(I), ET(I), CT(I)
2620 IF CM$ = "V" THEN WRITE #1, CM1(I)
2630 NEXT I
2640 CLOSE #1
2650 CLS : LOCATE 7,15,0
2660 PRINT "TASKPLAN"
2670 LOCATE 9,15 :  PRINT "Project ....................... ";GPN
2680 LOCATE 11,15 : PRINT "Number of Tasks ..............."; NT
2690 LOCATE 12,15 : PRINT "Number of Time Periods ........"; NP
2700 LOCATE 14,15 : PRINT "Please pardon this pause while TASKPLAN"
2710 LOCATE 15,15 : PRINT "solves for the incremental and"
2720 LOCATE 16,15 : PRINT "cumulative costs for each time period....."
2730 COLOR 16,6 : LOCATE 16,57 : PRINT "." : COLOR 0,6
2740 FOR I = 1 TO 4000 : NEXT I
2750 IF CM$ = "V" GOTO 2790
2760 FOR I = 1 TO NT
2770 CM1(I) = CM11
2780 NEXT I
2790 CT1 = 0 : CT2 = 0
2800 FOR I = 1 TO NT
2810 CT1 = CT1 + CT(I)
2820 CTM(I) = CT(I)*CM1(I)*CM2
2830 CT2 = CT2 + CTM(I)
2840 CTM(I) = CTM(I)/(ET(I) - BT(I))
2850 NEXT I
2860 FOR J = 1 TO NP
2870 TK(J) = 0!
2880 FOR I = 1 TO NT
2890 IF J < BT(I) OR J >= (ET(I) + 1) THEN Y(I,J) = 0! : GOTO 2930
2900 IF J < (BT(I) + 1) THEN Y(I,J) = CTM(I)*(J - BT(I)) : GOTO 2930
2910 IF J >= (BT(I) + 1) AND J <= ET(I) THEN Y(I,J) = CTM(I) : GOTO 2930
2920 Y(I,J) = CTM(I)*(ET(I) - J + 1)
2930 TK(J) = TK(J) + Y(I,J)
2940 NEXT I
2950 NEXT J
2960 CT3 = 0
2970 FOR J = 1 TO NP
2980 TK(J) = FIX(TK(J) + .5)
2990 CT3 = CT3 + TK(J)
3000 NEXT J
3010 IF RM$ = "N" THEN CT4 = FIX(CT2 + .5) : GOTO 3050
3020 CF = CT2 - FIX(CT2)
3030 IF CF = 0 THEN CT4 = CT2 : GOTO 3050
3040 CT4 = 1 + FIX(CT2)
3050 ROE = CT4 - CT3
3060 IF ROE = 0 GOTO 3140
3070 SROE = SGN(ROE)
3080 AROE = SROE*ROE
3090 RE1 = FIX(NP/AROE)
3100 RE2 = FIX(RE1/2)
3110 FOR J = (1 + RE2) TO (1 + RE2 + RE1*(AROE - 1)) STEP RE1
3120 TK(J) = TK(J) + SROE
3130 NEXT J
3140 CI(1) = TK(1)
3150 CC(1) = CI(1)
3160 FOR J = 2 TO NP
3170 CI(J) = TK(J)
3180 CC(J) = CC(J-1) + CI(J)
3190 NEXT J
3200 CLS : LOCATE 1,1,0 : FL1 = 0
3210 OPEN "SCRN:" FOR OUTPUT AS #1
3220 GOSUB 6760
3230 CLOSE #1
3240 JA = CINT((NP/10) + .4)
3250 FOR JTT = 1 TO JA
3260 JHI = JTT*10
3270 JLO = JHI - 9
3280 IF NP < JHI THEN JHI = NP
3290 JLN = 9
3300 FOR J = JLO TO JHI
3310 LOCATE JLN,4,0  : PRINT USING "##"; J;
3320 LOCATE JLN,15,0 : PRINT USING "##############,."; CI(J);
3330 IF CI(J) > 0! THEN PRINT ".";
3340 LOCATE JLN,42,0 : PRINT USING "##############,."; CC(J);
3350 IF CC(J) > 0! THEN PRINT ".";
3360 JLN = JLN + 2 - (J MOD 2)
3370 NEXT J
3380 FOR JB = JLN TO 24
3390 LOCATE JB,1,0 : PRINT STRING$(79,32);
3400 NEXT JB
3410 FL4 = 1
3420 GOSUB 4900
3430 '
3440 NEXT JTT
3450 CLS : LOCATE 4,1,1
3460 LINE INPUT; "Construct a histogram of the results (Y/N)? ";GHI
3470 IF GHI = "" GOTO 3460
3480 IF GHI >< "Y" AND GHI >< "y" GOTO 3890
3490 LOCATE 6,1,0 : FL3 = 1
3500 PRINT "Pausing while your histogram is being computed."
3510 CIM = CI(1) : CCM = CC(1)
3520 FOR J = 2 TO NP
3530 IF CIM < CI(J) THEN CIM = CI(J)
3540 IF CCM < CC(J) THEN CCM = CC(J)
3550 NEXT J
3560 FOR IX = 1 TO 79
3570 FOR IY = 1 TO 13
3580 H(IX,IY) = " "
3590 NEXT IY
3600 NEXT IX
3610 H(1,1)  = "1" :  H(2,1)  = "." :  H(3,1)  = "0"
3620 H(1,6)  = "0" :  H(2,6)  = "." :  H(3,6)  = "5"
3630 H(1,11) = "0" :  H(2,11) = "." :  H(3,11) = "0"
3640 H(5,13) = "1"
3650 FOR IY = 1 TO 11
3660 H(4,IY) = "_"
3670 NEXT IY
3680 FOR IX = 5 TO (NP+4)
3690 H(IX,12) = "-"
3700 NEXT IX
3710 GNP = STR$(NP)
3720 H(NP+4,13) = RIGHT$(GNP,1)
3730 IF NP > 9 THEN H(NP+3,13) = MID$(GNP,2,1)
3740 FOR J = 1 TO NP
3750 NI = 12 - CINT(10*(CI(J)/CIM))
3760 FOR IY = NI TO 11
3770 H(J+4,IY) = "I"
3780 NEXT IY
3790 NC = 12 - CINT(10*(CC(J)/CCM))
3800 IF H(J+4,NC) = " " THEN H(J+4,NC) = "C"
3810 IF H(J+4,NC) = "I" THEN H(J+4,NC) = "O"
3820 NEXT J
3830 CLS
3840 LOCATE 2,1,0 : FL1 = 0
3850 OPEN "SCRN:" FOR OUTPUT AS #1
3860 GOSUB 6890
3870 CLOSE #1
3880 FL4 = 0 : GOSUB 4900
3890 CLS
3900 LOCATE 2,1,1
3910 LINE INPUT; "Do you want a printed copy of this project's data (Y/N)? ";GPR
3920 IF GPR = "" GOTO 3910
3930 IF GPR >< "Y" AND GPR >< "y" GOTO 4700
3940 LOCATE 4,1,0
3950 PRINT "Please be sure your printer is ON and is ON LINE.
3960 PRINT "Please be sure your paper is aligned and at top-of-form."
3970 FL4 = 0 : GOSUB 4900
3980 LOCATE 1,1,0 : PRINT "Printing the general data....."
3990 LPRINT :LPRINT :LPRINT :LPRINT :LPRINT :LPRINT :LPRINT
4000 OPEN "LPT1:" FOR OUTPUT AS #1
4010 FL1 = 1 : GOSUB 5060
4020 CLOSE #1
4030 LPRINT CHR$(12);
4040 CLS : LOCATE 1,1,0 : PRINT "Printing the detailed task data....."
4050 LPRINT :LPRINT :LPRINT
4060 IA = 1
4070 IF NT > 40 THEN IA = 2
4080 FOR ITT = 1 TO IA
4090 OPEN "LPT1:" FOR OUTPUT AS #1
4100 GOSUB 5940
4110 CLOSE #1
4120 IF ITT = 1 THEN ILO = 1 : IHI = 40
4130 IF NT < 41 THEN IHI = NT
4140 IF ITT = 2 THEN ILO = 41 : IHI = NT
4150 FOR I = ILO TO IHI
4160 LPRINT TAB(2)  : LPRINT USING "##"; I;
4170 LPRINT TAB(8)  : LPRINT TT$(I);
4180 LPRINT TAB(33) : LPRINT USING "##.##"; BT(I);
4190 LPRINT TAB(43) : LPRINT USING "##.##"; ET(I);
4200 LPRINT TAB(53) : LPRINT USING "#######.##"; CT(I);
4210 IF CM$ = "C" THEN LPRINT " " : GOTO 4230
4220 LPRINT TAB(68) : LPRINT USING "##.######"; CM1(I)
4230 IF I MOD 10 = 0 THEN LPRINT
4240 NEXT I
4250 LPRINT CHR$(12);
4260 NEXT ITT
4270 LPRINT :LPRINT :LPRINT :LPRINT :LPRINT
4280 CLS : LOCATE 1,1,0 : PRINT "Printing the answers ....."
4290 OPEN "LPT1:" FOR OUTPUT AS #1
4300 FL1 = 1
4310 GOSUB 6760
4320 CLOSE #1
4330 JHI = 40
4340 IF NP < 41 THEN JHI = NP
4350 GT1 =" " :GT2 =" " :GT3 =" " :GT4 =" " :GT5 =" " : IF NP < 41 GOTO 4410
4360 GT1 = " | Time    Incremental      Cumulative"
4370 GT2 = " | Per.       Cost             Cost"
4380 GT3 = " | ---- ---------------- ----------------"
4390 GT4 = SPACE$(38) + " | "
4400 GT5 = " | "
4410 LPRINT "Time    Incremental      Cumulative   "; GT1
4420 LPRINT "Per.       Cost             Cost      "; GT2
4430 LPRINT "---- ---------------- ----------------"; GT3
4440 LPRINT GT4
4450 FOR J = 1 TO JHI
4460 LPRINT TAB(2)  : LPRINT USING "##"; J;
4470 LPRINT TAB(6)  : LPRINT USING "##############,."; CI(J);
4480 IF CI(J) > 0! THEN LPRINT ".";
4490 LPRINT TAB(23) : LPRINT USING "##############,."; CC(J);
4500 IF CC(J) > 0! THEN LPRINT ".";
4510 LPRINT GT5;
4520 IF NP < 41 THEN LPRINT " " : GOTO 4590
4530 IF (J + 40) > NP THEN LPRINT " " : GOTO 4590
4540 LPRINT TAB(43) : LPRINT USING "##"; J+40;
4550 LPRINT TAB(47) : LPRINT USING "##############,."; CI(J+40);
4560 IF CI(J+40) > 0! THEN LPRINT ".";
4570 LPRINT TAB(64) : LPRINT USING "##############,."; CC(J+40);
4580 IF CC(J+40) > 0! THEN LPRINT "." ELSE LPRINT " "
4590 IF J MOD 10 = 0 THEN LPRINT GT4
4600 NEXT J
4610 LPRINT CHR$(12);
4620 IF FL3 = 0 GOTO 4690
4630 CLS : LOCATE 1,1,0 : PRINT " Printing the histogram ....."
4640 LPRINT :LPRINT :LPRINT :LPRINT :LPRINT :LPRINT
4650 OPEN "LPT1:" FOR OUTPUT AS #1
4660 GOSUB 6890
4670 CLOSE #1
4680 LPRINT CHR$(12);
4690 LPRINT CHR$(12);
4700 CLS : LOCATE 1,1,0
4710 PRINT "All calculations and printings are completed."
4720 PRINT : PRINT : PRINT
4730 PRINT "EXIT options are as follows:" : PRINT : PRINT
4740 PRINT "1 - Revise the current project" : PRINT
4750 PRINT "2 - Use TASKPLAN for a new project" : PRINT
4760 PRINT "3 - Exit to BASICA (the TASKPLAN program will be lost)" : PRINT
4770 PRINT "4 - Exit to DOS (the operating system)"
4780 LOCATE 20,1,1
4790 LINE INPUT; "Please Enter the Exit Option No. of your choice ----> "; OP$
4800 IF OP$ = "" OR VAL(OP$)<1 OR VAL(OP$)>4 THEN LOCATE 20,54 : PRINT "       " : GOTO 4780
4810 OPN% = VAL(OP$)
4820 IF OPN% > 1 GOTO 4840
4830 FLD = 1 :FL1 = 0 :FL2 = 1 :FL3 = 0 :FL4 = 0 : CLS : GOTO 1070
4840 IF OPN% > 2 GOTO 4860
4850 FLD = 0 :FL1 = 0 :FL2 = 0 :FL3 = 0 :FL4 = 0 : CLS : GOTO 240
4860 IF OPN% > 3 GOTO 4880
4870 CLS : KEY ON : NEW
4880 COLOR 7,0,0 : CLS : SYSTEM
4890 END
4900 LOCATE 24,19,0
4910 COLOR 0,7
4920 PRINT " ***** PRESS ANY KEY TO CONTINUE ***** ";
4930 G = INKEY$ : IF G = "" THEN 4930
4940 COLOR 0,6,6
4950 IF FL4 = 0 THEN CLS
4960 RETURN
4970 PRINT : PRINT
4980 LINE INPUT; "Do you wish to display existing filenames (Y/N)? ";GDF
4990 IF GDF >< "Y" AND GDF >< "y" GOTO 5050
5000 PRINT
5010 LINE INPUT; "Which drive (A, B, C, etc)? -------------------> "; GDR
5020 PRINT : PRINT
5030 FILES GDR + ":*.*"
5040 GOTO 4970
5050 RETURN
5060 IF FL1 = 1  GOTO 5090
5070 IF FLD = 0 THEN PRINT #1, "  Type the requested data, then press Enter."
5074 IF FLD = 0 THEN GOTO 5090
5080 IF FLD = 1 THEN PRINT #1, " "
5090 PRINT #1, "ITEM"
5100 PRINT #1, " NO."
5110 PRINT #1, " "
5120 PRINT #1, " 1. Project's Name, up to 20 characters ----------------->";" ";GPN: PRINT #1, " "
5130 PRINT #1, " 2. Total Number of Tasks, 2 to"; IT; "---------------------->";NT : PRINT #1, " "
5140 IF FLD = 0 AND FL1 = 0 THEN LOCATE 7,60,0 : PRINT "      " : PRINT
5150 PRINT #1, " 3. Total Number of Time Periods, 2 to"; JP; "--------------->";NP : PRINT #1, " "
5160 IF FLD = 0 AND FL1 = 0 THEN LOCATE 9,60,0 : PRINT "      " : PRINT
5170 PRINT #1, " 4. Choice of Rounding Method (N or C) ------------------>";" ";RM$
5180 PRINT #1, "      - N (normal),       +1 if fraction >= 0.5"
5190 PRINT #1, "      - C (conservative), +1 if fraction >  0.0":PRINT #1," "
5200 PRINT #1, " 5. Choice of Type for Cost Multiplier M1 (C or V) ------>";" ";CM$
5210 PRINT #1, "      - C (constant), same for all tasks"
5220 PRINT #1, "      - V (variable), different for each task":PRINT #1, " "
5230 IF CM$ = "C" THEN CM1N$ = RIGHT$( STR$(CM11), LEN(STR$(CM11)) - 1 )
5240 IF CM$ = "V" THEN CM1N$ = "Variable"
5250 PRINT #1, " 6. Value for Constant Cost Multiplier M1 (xx.yyyzzz) --->";" ";CM1N$ : PRINT #1, " "
5260 PRINT #1, " 7. Value for Constant Cost Multiplier M2 (xx.yyyzzz) --->";CM2
5270 IF FLD = 0 AND FL1 = 0 THEN LOCATE 21,60,0 : PRINT "      "
5280 RETURN
5290 GPN = ""
5300 LOCATE 5,60,0 : PRINT BLK$
5310 LOCATE 5,60,1 : LINE INPUT GPN
5320 IF GPN = "" GOTO 5290
5330 GPN = LEFT$(GPN,20)
5340 RETURN
5350 GNT = ""
5360 LOCATE 7,60,0 : PRINT BLK$
5370 LOCATE 7,60,1 : LINE INPUT GNT
5380 LOCATE 24,1,0 : PRINT STRING$(79,32);
5390 IF GNT = "" GOTO 5350
5400 NT = VAL(GNT)
5410 IF NT < 2 OR NT > IT GOTO 5430
5420 RETURN
5430 LOCATE 24,1,0 : BEEP
5440 PRINT "Pardon me, but the Number of Tasks must be from 2 to ";GIT;".";
5450 FOR IP = 1 TO 3000 : NEXT IP
5460 GOTO 5350
5470 GNP = ""
5480 LOCATE 9,60,0 : PRINT BLK$
5490 LOCATE 9,60,1 : LINE INPUT GNP
5500 LOCATE 24,1,0 : PRINT STRING$(79,32);
5510 IF GNP = "" GOTO 5470
5520 NP = VAL(GNP)
5530 IF NP < 2 OR NP > JP GOTO 5550
5540 RETURN
5550 LOCATE 24,1,0 : BEEP
5560 PRINT "Pardon me, but Number of Time Periods must be from 2 to ";GJP;".";
5570 FOR IP = 1 TO 3000 : NEXT IP
5580 GOTO 5470
5590 LOCATE 11,60,0 : PRINT BLK$
5600 LOCATE 11,60,1 : LINE INPUT RM$
5610 IF RM$ = "n" THEN RM$ = "N" : GOTO 5640
5620 IF RM$ = "c" THEN RM$ = "C" : GOTO 5640
5630 IF RM$ >< "N" AND RM$ >< "C" THEN GOTO 5590
5640 LOCATE 11,60,0 : PRINT RM$
5650 RETURN
5660 LOCATE 15,60,0 : PRINT BLK$
5670 LOCATE 15,60,1 : LINE INPUT CM$
5680 IF CM$ = "c" THEN CM$ = "C" : GOTO 5710
5690 IF CM$ = "v" THEN CM$ = "V" : GOTO 5710
5700 IF CM$ >< "C" AND CM$ >< "V" THEN GOTO 5660
5710 LOCATE 15,60,0 : PRINT CM$
5720 IF FL2 = 1 AND CMO$ >< CM$ THEN LOCATE 24,1 : PRINT STRING$(79,32); : GOTO 5750
5730 RETURN
5740 IF FL2 = 1 AND CM$ = "V" GOTO 5830
5750 GCM1$ = ""
5760 LOCATE 19,60,0 : PRINT BLK$
5770 IF CM$ = "V" THEN LOCATE 19,60,0 : PRINT "Variable" : RETURN
5780 LOCATE 19,60,1 : LINE INPUT GCM1
5790 IF GCM1 = "" GOTO 5740
5800 CM11# = VAL(GCM1)
5810 IF CM11# = 0 GOTO 5750
5820 RETURN
5830 LOCATE 24,1,0 : PRINT STRING$(79,32);
5840 LOCATE 24,39,0 : PRINT "Please, first change ITEM 5";
5850 FOR I = 1 TO 8000 : NEXT I
5860 RETURN
5870 GM2 = ""
5880 LOCATE 21,60,0 : PRINT BLK$
5890 LOCATE 21,60,1 : LINE INPUT GM2
5900 IF GM2 = "" GOTO 5870
5910 CM2 = VAL(GM2)
5920 IF CM2 = 0 GOTO 5870
5930 RETURN
5940 IF CM$ = "V" GOTO 6020
5950 G1 = " "
5960 G2 = " "
5970 G3 = " "
5980 G4 = " "
5990 G5 = " "
6000 G6 = " "
6010 GOTO 6080
6020 G1 = STRING$(70,32) + "Cost"
6030 G2 = "    Multiplier"
6040 G3 = "        M1"
6050 G4 = "        --"
6060 G5 = "    xx.yyyzzz"
6070 G6 = "   ------------"
6080 IF FL1 = 0 GOTO 6110
6090 LPRINT :LPRINT :LPRINT
6100 LPRINT "                Project :   "; GPN  : LPRINT
6110 PRINT #1, G1
6120 PRINT #1, "Task                            Start     End          Task    "; G2
6130 PRINT #1, " No.       Description          Time      Time         Cost    "; G3$
6140 PRINT #1, " --            --                --        --           --     "; G4
6150 PRINT #1, " xx    vvvvwwwwxxxxyyyyzzzz     xx.yy     xx.yy     wxxxyyy.zz "; G5
6160 PRINT #1, "----  ----------------------   -------   -------   ------------"; G6
6170 PRINT #1, " "
6180 RETURN
6190 LOCATE 5,12,0 : PRINT "D";
6200 LOCATE 4,33,0 : PRINT "S";
6210 LOCATE 4,43,0 : PRINT "E";
6220 LOCATE 5,56,0 : PRINT "C";
6230 IF CM$ = "C" GOTO 6250
6240 LOCATE 5,72,0 : PRINT "M1";
6250 COLOR 0,6
6260 RETURN
6270 LOCATE ILNR,7,0 : PRINT STRING$(22,32);
6280 LOCATE ILNR,8,1 : LINE INPUT; GTT
6290 IF GTT = "" GOTO 6280
6300 TT$(ITR) = LEFT$(GTT,20)
6310 RETURN 2460
6320 LOCATE ILNR,32,0 : PRINT STRING$(7,32);
6330 LOCATE ILNR,33,1 : LINE INPUT; BTT$
6340 LOCATE 24,1,0 : PRINT STRING$(79,32);
6350 IF BTT$ = "" GOTO 6330
6360 G = BTT$ : GOSUB 7030
6370 IF G = "0" THEN BT(ITR) = 0 : RETURN 2460
6380 BT(ITR) = VAL(BTT$)
6390 IF BT(ITR) = 0 GOTO 6320
6400 IF BT(ITR) < ET(ITR) AND BT(ITR) >= 0! THEN RETURN 2460
6410 LOCATE 24,1,0 : BEEP
6420 IF BT(ITR) < 0! THEN PRINT "Start Time must be >= 0.0 ."; : GOTO 6440
6430 IF BT(ITR) >= ET(ITR) THEN PRINT "Start Time must be <"; ET(ITR); " for Task"; ITR; ".";
6440 FOR I = 1 TO 3000 : NEXT I
6450 GOTO 6320
6460 LOCATE ILNR,42,0 : PRINT STRING$(7,32);
6470 LOCATE ILNR,43,1 : LINE INPUT; ETT$
6480 LOCATE 24,1,0 : PRINT STRING$(79,32);
6490 IF ETT$ = "" GOTO 6470
6500 ET(ITR) = VAL(ETT$)
6510 IF ET(ITR) > BT(ITR) AND ET(ITR) <= NP THEN RETURN 2460
6520 LOCATE 24,1,0 : BEEP
6530 IF ET(ITR) <= BT(ITR) THEN PRINT "End Time must be >";BT(ITR);
6534 IF ET(ITR) <= BT(ITR) THEN PRINT " for Task"; ITR; " ."; : GOTO 6550
6540 IF ET(ITR) > NP THEN PRINT "Pardon me, but End Time must <="; NP; " .";
6550 FOR I = 1 TO 3000 : NEXT I
6560 GOTO 6460
6570 LOCATE ILNR,52,0 : PRINT STRING$(12,32);
6580 LOCATE ILNR,53,1 : LINE INPUT; CTT$
6590 IF CTT$ = "" GOTO 6580
6600 G = CTT$ : GOSUB 7030
6610 IF G = "0" THEN CT(ITR) = 0 : RETURN 2460
6620 CT(ITR) = VAL(CTT$)
6630 IF CT(ITR) > 0 THEN RETURN 2460
6640 CTT$ = "" : GOTO 6570
6650 RETURN 2460
6660 IF CM$ = "C" THEN RETURN 2340
6670 FL2 = 1
6680 LOCATE ILNR,67,0 : PRINT STRING$(12,32);
6690 LOCATE ILNR,68,1 : LINE INPUT; CMU1$
6700 IF CMU1$ = "" GOTO 6690
6710 G = CMU1$ : GOSUB 7030
6720 IF G = "0" THEN CM1(ITR) = 0 : RETURN 2460
6730 CM1(ITR) = VAL(CMU1$)
6740 IF CM1(ITR) > 0 THEN RETURN 2460
6750 CMU1$ = "" : GOTO 6660
6760 GRM = "Normal"
6770 IF RM$ = "C" THEN GRM = "Conservative"
6780 GCM1 = " Variable"
6790 IF CM$ = "C" THEN GCM1 = STR$(CM11)
6800 PRINT #1, " Project Name :  "; GPN
6810 PRINT #1, NT;"tasks over"; NP; "periods with "; GRM; " rounding"
6820 PRINT #1, " Cost :  Total Input ="; CT1; ", Multipliers : M1 ="; GCM1; ", M2 ="; CM2
6830 PRINT #1, " "
6840 IF FL1 = 1 GOTO 6880
6850 PRINT #1, "  Time           Incremental                Cumulative"
6860 PRINT #1, " Period             Cost                       Cost"
6870 PRINT #1, " ------   ------------------------   ------------------------"
6880 RETURN
6890 PRINT #1, SPC(25);"Project : "; GPN
6900 PRINT #1, " "
6910 PRINT #1, SPC(10);"I - Incremental cost ( maximum =";CIM;")"
6920 PRINT #1, SPC(10);"C - Cumulative cost  ( maximum =";CCM;")"
6930 PRINT #1, SPC(10);"O - C plotted over I"
6940 PRINT #1, " "
6950 PRINT #1, "Normalized Cost"
6960 FOR IY = 1 TO 13
6970 FOR IX = 1 TO (NP+3)
6980 PRINT #1, H(IX,IY);
6990 NEXT IX
7000 PRINT #1, H(NP+4,IY)
7010 NEXT IY
7020 RETURN
7030 IF G="00" OR G="0." OR G=".0" OR G=".00" OR G="00." OR G="0.0" THEN G = "0"
7034 IF G="00.0" OR G="00.00" OR G="0.00" THEN G = "0"
7040 RETURN
7050 END
