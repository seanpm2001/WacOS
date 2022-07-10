1000 rem
1010 rem bouncing ball program
1020 rem
1030 ascroll 0
1040 curvis 0
1050 clear
1060 posx% = 1
1070 posy% = 1
1080 addx% = +1
1090 addy% = +1
1100 oldx% = posx%
1110 oldy% = posy%
1120 print "*";
1130 posx% = posx%+addx%
1140 posy% = posy%+addy%
1150 if (posx% = 1) or (posx% = maxx%) then addx% = -addx%
1160 if (posy% = 1) or (posy% = maxy%) then addy% = -addy%
1170 cursor oldx%, oldy%
1180 print " "; 
1190 cursor posx%, posy%
1200 goto 1100