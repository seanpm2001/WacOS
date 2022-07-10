1000 rem
1010 rem Match snatch game program
1020 rem adapted from 'A PRIMER ON PASCAL'
1030 rem by conway, gries and zimmerman.
1040 rem
1050 rem Initalization
1060 rem Get game parameters
1070 rem
1080 print
1090 print "Welcome to match-snatch"
1100 print
1110 rem
1120 rem matches   - a
1130 rem movelimit - b
1140 rem whosemove - a$
1150 rem move      - c
1160 rem nextmove  - n$
1170 rem
1180 print "how many matches to start ? ";
1190 input a
1200 if a < 1 then print "Must be at least 1": goto 1190
1210 print "How many in 1 move ? ";
1220 input b
1230 if b < 1 then print "Must be at least 1": goto 1220
1240 if b > a then print "Not that many matches": goto 1220
1250 rem
1260 rem Determine who moves first
1270 rem
1280 print "who moves first -- you or me ? ";
1290 input a$
1300 if (a$ <> "me")*(a$ <> "you") then goto 1280
1310 print ""
1320 if a$ = "you" then n$ = "prog": goto 1340
1330 n$ = "user"
1340 rem
1350 rem alternate moves -- user and program
1360 rem
1370 if a = 0 then goto 1640
1380 if n$ = "prog" then goto 1540
1390 rem
1400 rem User's move
1410 rem
1430 print "how many do you take ? ";
1440 input c
1450 if (c >= 1)*(c <= a)*(c <= b) then goto 1500
1460 if c < 1 then print "'must take at least one"
1470 if c > b then print "that''s more than we agreed on"
1480 if c > a then print "there aren't that many"
1490 goto 1430
1500 a = a-c
1510 print "there are ", a, " left"
1520 n$ = "prog"
1530 goto 1370
1540 rem
1550 rem program's move
1560 rem
1570 c = (a-1) mod (b+1)
1580 if c = 0 then c = 1
1590 print "I take ", c, " matches"
1600 a = a-c
1610 print "There are ", a, " left"
1620 n$ = "user"
1630 goto 1370
1640 rem
1650 rem report outcome
1660 rem player who made last move lost
1670 rem
1680 if n$ = "user" then print "you won, nice going.": goto 1700
1690 print "I won, tough luck."
1700 stop
