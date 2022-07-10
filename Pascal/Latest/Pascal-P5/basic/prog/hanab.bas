10 print "HAMURABI: Game of Hamurabi - Version 1.01": print
20 print "Corona Data Systems, Inc.": print
30 print
40 rem ORIGINAL HAMURABI IN IMSAI 4K BASIC
50 rem THIS VERSION REWRITTEN 12/11/77
60 rem RANDOMIZE
310 print "HAMURABI - ";
320 print "WHERE YOU GOVERN THE ";
321 print "ANCIENT KINGDOM OF SUMERIA."
330 print "THE OBJECT IS TO FIGURE ";
331 print "OUT HOW THE GAME WORKS!!"
340 print "(IF YOU WANT TO QUIT, ";
341 print "SELL ALL YOUR LAND.)"
400 let a1 = 100
410 let a2 = 5
420 let a3 = 0
430 let b1 = 2800
440 let b2 = 200
450 let b3 = 3
460 let b4 = 3000
470 let c1 = 1000
480 let j = 1
1010 print
1020 print "HAMURABI, I BEG TO REPORT ";
1021 print "THAT LAST YEAR "
1040 print a3;" PEOPLE STARVED AND ";a2;
1041 print " PEOPLE CAME TO THE CITY."
1050 if j > 0 then goto 1100
1060 let a1 = a1-int(a1/2)
1070 print "THE PLAGUE KILLED ";
1071 print "HALF THE PEOPLE."
1100 print "THE POPULATION IS NOW ";a1
1120 print "WE HARVESTED ";b4;" BUSHELS ";
1121 print "AT ";b3;" BUSHELS PER ACRE."
1130 print "RATS DESTROYED ";
1131 print b2;" BUSHELS LEAVING ";b1;
1140 print " BUSHELS IN"
1141 print "THE STOREHOUSES."
1160 print "THE CITY OWNS ";c1;
1161 print " ACRES OF LAND."
1170 let c2 = 17+int(6*rnd(1))
1180 print "LAND IS WORTH ";c2;" BUSHELS ";
1181 print "PER ACRE."
1210 print "HAMURABI . . ."
1310 print
1320 print "BUY HOW MANY ACRES";
1330 input i
1335 print
1340 let i = int(abs(i))
1350 if i = 0 then goto 1510
1360 let j = i*c2
1370 if j <= b1 then goto 1400
1380 gosub 9000
1390 goto 1310
1400 let b1 = b1-j
1410 let c1 = c1+i
1510 print "SELL HOW MANY ACRES";
1520 input i
1525 print
1530 let i = (abs(i))
1540 if i = 0 then goto 1710
1550 if i < c1 then goto 1600
1560 if i = c1 then end
1570 gosub 9000
1580 goto 1510
1600 let c1 = c1-i
1610 let b1 = b1+c2*i
1710 print "HOW MANY BUSHELS SHALL ";
1711 print "WE DISTRIBUTE AS FOOD? ";
1720 input i
1725 print
1730 let i = int(abs(i))
1740 if i <= b1 then goto 1770
1750 gosub 9000
1760 goto 1710
1770 let b1 = b1-i
1780 let a3 = a1-int(i/20)
1790 let a2 = 0
1800 if a3 >= 0 then goto 1910
1810 let a2 = -a3/2
1820 let a3 = 0
1910 print "HOW MANY ACRES SHALL ";
1911 print "WE PLANT? ";
1920 input i
1925 print
1930 let i = int(abs(i))
1935 if i > c1 then goto 1960
1940 let j = int(i/2)
1950 if j <= b1 then goto 1980
1960 gosub 9000
1970 goto 1910
1980 if i > 10*a1 then goto 1960
1990 let b1 = b1-j
2010 let b3 = int(5*rnd(1))+1
2020 let b4 = b3*i
2030 let b2 = int((b1+b4)*0.07*rnd(1))
2040 let b1 = b1-b2+b4
2050 let j = int(10*rnd(1))
2060 let a2 = int(a2+(5-b3)*b1/600+1)
2070 if a2 <= 50 then goto 2100
2080 let a2 = 50
2100 let a1 = a1+a2-a3
2110 goto 1010
9000 rem***ERROR ROUTINE
9010 print "HAMURABI, THINK AGAIN - ";
9011 print "YOU ONLY HAVE "
9020 print a1;" PEOPLE, ";c1;" ACRES, AND ";
9030 print b1;" BUSHELS IN STOREHOUSES."
9040 return
