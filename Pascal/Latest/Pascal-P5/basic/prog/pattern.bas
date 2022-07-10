1000 rem
1010 rem pattern program
1020 rem
1021 dim scnsav%(maxx%, maxy%)
1030 ascroll 0
1040 curvis 0
1050 clear
1051 for x% = 1 to maxx%
1052     for y% = 1 to maxy%
1053         scnsav%(x%, y%) = 0
1054     next y%
1055 next x%    
1060 posx% = 1
1070 posy% = 1
1080 addx% = +1
1090 addy% = +1
1120 if scnsav%(posx%, posy%) then print " " else print "*"
1121 scnsav%(posx%, posy%) = not scnsav%(posx%, posy%)
1130 posx% = posx%+addx%
1140 posy% = posy%+addy%
1150 if (posx% = 1) or (posx% = maxx%) then addx% = -addx%
1160 if (posy% = 1) or (posy% = maxy%) then addy% = -addy%
1190 cursor posx%, posy%
1200 goto 1120