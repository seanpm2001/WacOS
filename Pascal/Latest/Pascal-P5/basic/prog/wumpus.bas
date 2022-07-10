5 rem *** HUNT THE WUMPUS ***
10 dim p(5)
15 print "INSTRUCTIONS (Y-N)";
20 input i$
25 if (i$ = "N") or (i$ = "n") then 35
30 gosub 375
35 goto 80
80 rem *** SET UP CAVE (DODECAHEDRAL NODE LIST) ***
85 dim s(20,3)
90 for j = 1 to 20
95 for k = 1 to 3
100 read s(j,k)
105 next k
110 next j
115 data 2,5,8,1,3,10,2,4,12,3,5,14,1,4,6
120 data 5,7,15,6,8,17,1,7,9,8,10,18,2,9,11
125 data 10,12,19,3,11,13,12,14,20,4,13,15,6,14,16
130 data 15,17,20,7,16,18,9,17,19,11,18,20,13,16,19
135 def fnA(X)=INT(20*RND(1))+1
140 def fnB(X)=INT(3*RND(1))+1
145 def fnC(X)=INT(4*RND(1))+1
150 rem *** LOCATE L ARRAY ITEMS ***
155 rem *** 1-YOU, 2-WUMPUS, 3&4-PITS, 5&6-BATS ***
160 dim l(6)
165 dim m(6)
170 for j = 1 to 6
175 l(j) = fna(0)
180 m(j) = l(j)
185 next j
190 rem *** CHECK FOR CROSSOVERS (IE l(1)=l(2), ETC) ***
195 for j = 1 to 6
200 for k = 1 to 6
205 if j = k then 215
210 if l(j) = l(k) then 170
215 next k
220 next j
225 rem *** SET NO. OF ARROWS ***
230 a = 5
235 l = l(1)
240 rem *** RUN THE GAME ***
245 print "HUNT THE WUMPUS"
250 rem *** HAZARD WARNING AND LOCATION ***
255 gosub 585
260 rem *** MOVE OR SHOOT ***
265 gosub 670
270 on o goto 280,300
275 rem *** SHOOT ***
280 gosub 715
285 if f = 0 then 255
290 goto 310
295 rem *** MOVE ***
300 gosub 975
305 if f = 0 then 255
310 if f > 0 then 335
315 rem *** LOSE ***
320 print "HA HA HA - YOU LOSE!"
325 goto 340
330 rem *** WIN ***
335 print "HEE HEE HEE - THE WUMPUS'LL GET YOU NEXT TIME!!"
340 for j = 1 to 6
345 l(j) = m(j)
350 next j
355 print "SAME SETUP (Y-N)";
360 input i$
365 if (i$ <> "Y") and (i$ <> "y") then 170
370 goto 230
375 rem *** INSTRUCTIONS ***
380 print "WELCOME TO 'HUNT THE WUMPUS'"
385 print "  THE WUMPUS LIVES IN A CAVE OF 20 ROOMS. EACH ROOM"
390 print "HAS 3 TUNNELS LEADING TO OTHER ROOMS. (LOOK AT A"
395 print "DODECAHEDRON TO SEE HOW THIS WORKS-IF YOU DON'T KNOW"
400 print "WHAT A DODECAHEDRON IS, ASK SOMEONE)"
405 print
410 print "     HAZARDS:"
415 print " BOTTOMLESS PITS - TWO ROOMS HAVE BOTTOMLESS PITS IN THEM"
420 print "     IF YOU GO THERE, YOU FALL INTO THE PIT (& LOSE!)"
425 print " SUPER BATS - TWO OTHER ROOMS HAVE SUPER BATS. IF YOU"
430 print "     GO THERE, A BAT GRABS YOU AND TAKES YOU TO SOME OTHER"
435 print "     ROOM AT RANDOM. (WHICH MAY BE TROUBLESOME)"
440 input "HIT RETURN TO CONTINUE";a$
445 print "     WUMPUS:"
450 print " THE WUMPUS IS NOT BOTHERED BY HAZARDS (HE HAS SUCKER"
455 print " FEET AND IS TOO BIG FOR A BAT TO LIFT).  USUALLY"
460 print " HE IS ASLEEP.  TWO THINGS WAKE HIM UP: YOU SHOOTING AN"
465 print "ARROW OR YOU ENTERING HIS ROOM."
470 print "     IF THE WUMPUS WAKES HE MOVES (P=.75) ONE ROOM"
475 print " OR STAYS STILL (P=.25).  AFTER THAT, IF HE IS WHERE YOU"
480 print " ARE, HE EATS YOU UP AND YOU LOSE!"
485 print
490 print "     YOU:"
495 print " EACH TURN YOU MAY MOVE OR SHOOT A CROOKED ARROW"
500 print "   MOVING:  YOU CAN MOVE ONE ROOM (THRU ONE TUNNEL)"
505 print "   ARROWS:  YOU HAVE 5 ARROWS.  YOU LOSE WHEN YOU RUN OUT"
510 print "   EACH ARROW CAN GO FROM 1 TO 5 ROOMS. YOU AIM BY TELLING"
515 print "   THE COMPUTER THE ROOM#S YOU WANT THE ARROW TO GO TO."
520 print "   IF THE ARROW CAN'T GO THAT WAY (IF NO TUNNEL) IT MOVES"
525 print "   AT RANDOM TO THE NEXT ROOM."
530 print "     IF THE ARROW HITS THE WUMPUS, YOU WIN."
535 print "     IF THE ARROW HITS YOU, YOU LOSE."
540 input "HIT RETURN TO CONTINUE";a$
545 print "    WARNINGS:"
550 print "     WHEN YOU ARE ONE ROOM AWAY FROM A WUMPUS OR HAZARD,"
555 print "     THE COMPUTER SAYS:"
560 print " WUMPUS:  'I SMELL A WUMPUS'"
565 print " BAT   :  'BATS NEARBY'"
570 print " PIT   :  'I FEEL A DRAFT'"
575 print
580 return
585 rem *** PRINT LOCATION & HAZARD WARNINGS ***
590 print
595 for j = 2 to 6
600 for k = 1 to 3
605 if s(l(1),k) <> l(j) then 640
610 on j-1 goto 615,625,625,635,635
615 print "I SMELL A WUMPUS!"
620 goto 640
625 print "I FEEL A DRAFT"
630 goto 640
635 print "BATS NEARBY!"
640 next k
645 next j
650 print "YOU ARE IN ROOM ";l(1)
655 print "TUNNELS LEAD TO ";s(l,1);" ";s(l,2);" ";s(l,3)
660 print
665 return
670 rem *** CHOOSE OPTION ***
675 print "SHOOT OR MOVE (S-M)";
680 input i$
685 if (i$ <> "S") and (i$ <> "s") then 700
690 o = 1
695 return
700 if (i$ <> "M") and (i$ <> "m") then 675
705 o = 2
710 return
715 rem *** ARROW ROUTINE ***
720 f = 0
725 rem *** PATH OF ARROW ***
735 print "NO. OF ROOMS (1-5)";
740 input j9
745 if j9 < 1 then 735
750 if j9 > 5 then 735
755 for k = 1 to j9
760 print "ROOM #";
765 input p(k)
770 if k <= 2 then 790
775 if p(k) <> p(k-2) then 790
780 print "ARROWS AREN'T THAT CROOKED - TRY ANOTHER ROOM"
785 goto 760
790 next k
795 rem *** SHOOT ARROW ***
800 l = l(1)
805 for k = 1 to j9
810 for k1 = 1 to 3
815 if s(l,k1) = p(k) then 895
820 next k1
825 rem *** NO TUNNEL FOR ARROW ***
830 l = s(l,fnb(1))
835 goto 900
840 next k
845 print "MISSED"
850 l = l(1)
855 rem *** MOVE WUMPUS ***
860 gosub 935
865 rem *** AMMO CHECK ***
870 a = a-1
875 if a > 0 then 885
880 f = -1
885 return
890 rem *** SEE IF ARROW IS AT l(1) OR AT l(2)
895 l = p(k)
900 if l <> l(2) then 920
905 print "AHA! YOU GOT THE WUMPUS!"
910 f = 1
915 return
920 if l <> l(1) then 840
925 print "OUCH! ARROW GOT YOU!"
930 goto 880
935 rem *** MOVE WUMPUS ROUTINE ***
940 k = fnc(0)
945 if k = 4 then 955
950 l(2) = s(l(2),k)
955 if l(2) <> l then 970
960 print "TSK TSK TSK - WUMPUS GOT YOU!"
965 f = -1
970 return
975 rem *** MOVE ROUTINE ***
980 f = 0
985 print "WHERE TO";
990 input l
995 if l < 1 then 985
1000 if l > 20 then 985
1005 for k = 1 to 3
1010 rem *** CHECK IF LEGAL MOVE ***
1015 if s(l(1),k) = l then 1045
1020 next k
1025 if l = l(1) then 1045
1030 print "NOT POSSIBLE -";
1035 goto 985
1040 rem *** CHECK FOR HAZARDS ***
1045 l(1) = l
1050 rem *** WUMPUS ***
1055 if l <> l(2) then 1090
1060 print "... OOPS! BUMPED A WUMPUS!"
1065 rem *** MOVE WUMPUS ***
1070 gosub 940
1075 if f = 0 then 1090
1080 return
1085 rem *** PIT ***
1090 if l = l(3) then 1100
1095 if l <> l(4) then 1120
1100 print "YYYYIIIIEEEE . . . FELL IN PIT"
1105 f = -1
1110 return
1115 rem *** BATS ***
1120 if l = l(5) then 1130
1125 if l <> l(6) then 1145
1130 print "ZAP--SUPER BAT SNATCH! ELSEWHEREVILLE FOR YOU!"
1135 l = fna(1)
1140 goto 1045
1145 return
1150 end

